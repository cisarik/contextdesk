#include "context/WorkspaceReceiver.h"
#include "core/Types.h"
#include "workspace/DesktopMutator.h"
#include "workspace/WorkspaceCheckpoint.h"
#include "workspace/WorkspacePlan.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QCoreApplication>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <functional>

using namespace contextdeck;

struct DesktopTuple {
    qint32 position = 0;
    QString id;
    QString name;
};

Q_DECLARE_METATYPE(DesktopTuple)

QDBusArgument &operator<<(QDBusArgument &argument, const DesktopTuple &tuple)
{
    argument.beginStructure();
    argument << tuple.position << tuple.id << tuple.name;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, DesktopTuple &tuple)
{
    argument.beginStructure();
    argument >> tuple.position >> tuple.id >> tuple.name;
    argument.endStructure();
    return argument;
}

namespace {

class FakeKwinManager : public QDBusVirtualObject
{
public:
    struct Call {
        QString method;
        quint32 position = 0;
        QString id;
        QString name;
        QString property;
        QVariant value;
    };

    explicit FakeKwinManager(QDBusConnection connection, QObject *parent = nullptr)
        : QDBusVirtualObject(parent)
        , m_connection(std::move(connection))
    {
        qDBusRegisterMetaType<DesktopTuple>();
        registerWorkspaceDesktopDBusTypes();
    }

    QString introspect(const QString &) const override
    {
        return QStringLiteral(
            "<interface name=\"org.kde.KWin.VirtualDesktopManager\">"
            "<property name=\"count\" type=\"u\" access=\"read\"/>"
            "<property name=\"current\" type=\"s\" access=\"readwrite\"/>"
            "<property name=\"desktops\" type=\"a(iss)\" access=\"read\"/>"
            "<property name=\"rows\" type=\"u\" access=\"readwrite\"/>"
            "<property name=\"navigationWrappingAround\" type=\"b\" access=\"readwrite\"/>"
            "<signal name=\"currentChanged\"><arg type=\"s\"/></signal>"
            "<signal name=\"countChanged\"><arg type=\"u\"/></signal>"
            "<signal name=\"desktopCreated\"><arg type=\"s\"/><arg type=\"(iss)\"/></signal>"
            "<signal name=\"desktopRemoved\"><arg type=\"s\"/></signal>"
            "<signal name=\"desktopDataChanged\"><arg type=\"s\"/></signal>"
            "<signal name=\"rowsChanged\"><arg type=\"u\"/></signal>"
            "<signal name=\"navigationWrappingAroundChanged\"><arg type=\"b\"/></signal>"
            "</interface>");
    }

    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override
    {
        const QString interface = message.interface();
        const QString member = message.member();
        if (interface == QLatin1String("org.freedesktop.DBus.Properties")) {
            if (member == QLatin1String("GetAll")) {
                return replyGetAll(message, connection);
            }
            if (member == QLatin1String("Set")) {
                return handleSet(message, connection);
            }
            return false;
        }
        if (member == QLatin1String("createDesktop")) {
            return handleCreate(message, connection);
        }
        if (member == QLatin1String("setDesktopName")) {
            return handleRename(message, connection);
        }
        if (member == QLatin1String("removeDesktop")) {
            return handleRemove(message, connection);
        }
        return false;
    }

    void seed(const QVector<QPair<QString, QString>> &desktops)
    {
        m_desktops.clear();
        for (int i = 0; i < desktops.size(); ++i) {
            m_desktops.push_back(DesktopTuple{static_cast<qint32>(i), desktops.at(i).first, desktops.at(i).second});
        }
        if (!m_desktops.isEmpty()) {
            m_current = m_desktops.first().id;
        }
    }

    void setRows(uint rows) { m_rows = rows; }
    void setWrapping(bool wrapping) { m_wrapping = wrapping; }
    void setFailMethod(const QString &method, int callNumber)
    {
        m_failMethod = method;
        m_failCall = callNumber;
    }
    void appendDesktop(const QString &id, const QString &name)
    {
        m_desktops.push_back(DesktopTuple{static_cast<qint32>(m_desktops.size()), id, name});
    }

    [[nodiscard]] QVector<Call> callsOf(const QString &method) const
    {
        QVector<Call> result;
        for (const Call &call : m_calls) {
            if (call.method == method) {
                result.push_back(call);
            }
        }
        return result;
    }
    [[nodiscard]] QVector<Call> calls() const { return m_calls; }
    [[nodiscard]] int callCount(const QString &method) const { return callsOf(method).size(); }
    [[nodiscard]] bool containsId(const QString &id) const
    {
        for (const DesktopTuple &row : m_desktops) {
            if (row.id == id) {
                return true;
            }
        }
        return false;
    }
    [[nodiscard]] QString nameOf(const QString &id) const
    {
        for (const DesktopTuple &row : m_desktops) {
            if (row.id == id) {
                return row.name;
            }
        }
        return {};
    }
    [[nodiscard]] int desktopCount() const { return static_cast<int>(m_desktops.size()); }
    [[nodiscard]] uint rows() const { return m_rows; }
    [[nodiscard]] bool wrapping() const { return m_wrapping; }
    [[nodiscard]] QString currentId() const { return m_current; }

    void emitDesktopCreated(const QString &id, int position)
    {
        VirtualDesktopDBus row;
        row.position = position;
        row.id = id;
        row.name = nameOf(id);
        QDBusMessage signal = QDBusMessage::createSignal(QStringLiteral("/VirtualDesktopManager"),
                                                         QStringLiteral("org.kde.KWin.VirtualDesktopManager"),
                                                         QStringLiteral("desktopCreated"));
        signal << id << QVariant::fromValue(row);
        m_connection.send(signal);
    }

    void emitCurrentChanged()
    {
        QDBusMessage signal = QDBusMessage::createSignal(QStringLiteral("/VirtualDesktopManager"),
                                                         QStringLiteral("org.kde.KWin.VirtualDesktopManager"),
                                                         QStringLiteral("currentChanged"));
        signal << m_current;
        m_connection.send(signal);
    }

    void removeDesktopDirect(const QString &id)
    {
        for (int i = 0; i < m_desktops.size(); ++i) {
            if (m_desktops.at(i).id != id) {
                continue;
            }
            m_desktops.removeAt(i);
            for (int j = 0; j < m_desktops.size(); ++j) {
                m_desktops[j].position = static_cast<qint32>(j);
            }
            if (m_current == id && !m_desktops.isEmpty()) {
                m_current = m_desktops.first().id;
            }
            QDBusMessage removed = QDBusMessage::createSignal(QStringLiteral("/VirtualDesktopManager"),
                                                             QStringLiteral("org.kde.KWin.VirtualDesktopManager"),
                                                             QStringLiteral("desktopRemoved"));
            removed << id;
            m_connection.send(removed);
            QDBusMessage countSignal = QDBusMessage::createSignal(QStringLiteral("/VirtualDesktopManager"),
                                                                  QStringLiteral("org.kde.KWin.VirtualDesktopManager"),
                                                                  QStringLiteral("countChanged"));
            countSignal << static_cast<uint>(m_desktops.size());
            m_connection.send(countSignal);
            return;
        }
    }

    std::function<void()> onBeforeMutation;

private:
    bool recordAndMaybeFail(const Call &call, const QDBusMessage &message, const QDBusConnection &connection)
    {
        m_calls.push_back(call);
        if (call.method == m_failMethod && callCount(call.method) == m_failCall) {
            connection.send(message.createErrorReply(QStringLiteral("org.kde.KWin.VirtualDesktopManager.Error"),
                                                     QStringLiteral("injected failure")));
            return true;
        }
        return false;
    }

    bool handleCreate(const QDBusMessage &message, const QDBusConnection &connection)
    {
        const QVariantList args = message.arguments();
        if (args.size() != 2) {
            return connection.send(message.createErrorReply(QDBusError::InvalidArgs, QStringLiteral("bad args")));
        }
        Call call;
        call.method = QStringLiteral("createDesktop");
        call.position = args.at(0).toUInt();
        call.name = args.at(1).toString();
        if (onBeforeMutation) {
            onBeforeMutation();
        }
        if (recordAndMaybeFail(call, message, connection)) {
            return true;
        }
        if (call.position > static_cast<quint32>(m_desktops.size())) {
            return connection.send(message.createErrorReply(QStringLiteral("org.kde.KWin.VirtualDesktopManager.Error"),
                                                            QStringLiteral("bad position")));
        }
        const QString id = QStringLiteral("gen-%1").arg(++m_generated);
        m_desktops.insert(static_cast<int>(call.position),
                          DesktopTuple{static_cast<qint32>(call.position), id, call.name});
        for (int i = 0; i < m_desktops.size(); ++i) {
            m_desktops[i].position = static_cast<qint32>(i);
        }
        emitDesktopCreated(id, static_cast<int>(call.position));
        QDBusMessage countSignal = QDBusMessage::createSignal(QStringLiteral("/VirtualDesktopManager"),
                                                              QStringLiteral("org.kde.KWin.VirtualDesktopManager"),
                                                              QStringLiteral("countChanged"));
        countSignal << static_cast<uint>(m_desktops.size());
        m_connection.send(countSignal);
        return connection.send(message.createReply());
    }

    bool handleRename(const QDBusMessage &message, const QDBusConnection &connection)
    {
        const QVariantList args = message.arguments();
        if (args.size() != 2) {
            return connection.send(message.createErrorReply(QDBusError::InvalidArgs, QStringLiteral("bad args")));
        }
        Call call;
        call.method = QStringLiteral("setDesktopName");
        call.id = args.at(0).toString();
        call.name = args.at(1).toString();
        if (recordAndMaybeFail(call, message, connection)) {
            return true;
        }
        for (DesktopTuple &row : m_desktops) {
            if (row.id == call.id) {
                row.name = call.name;
                return connection.send(message.createReply());
            }
        }
        return connection.send(message.createErrorReply(QStringLiteral("org.kde.KWin.VirtualDesktopManager.Error"),
                                                        QStringLiteral("no such desktop")));
    }

    bool handleRemove(const QDBusMessage &message, const QDBusConnection &connection)
    {
        const QVariantList args = message.arguments();
        if (args.size() != 1) {
            return connection.send(message.createErrorReply(QDBusError::InvalidArgs, QStringLiteral("bad args")));
        }
        Call call;
        call.method = QStringLiteral("removeDesktop");
        call.id = args.at(0).toString();
        if (recordAndMaybeFail(call, message, connection)) {
            return true;
        }
        for (int i = 0; i < m_desktops.size(); ++i) {
            if (m_desktops.at(i).id != call.id) {
                continue;
            }
            m_desktops.removeAt(i);
            for (int j = 0; j < m_desktops.size(); ++j) {
                m_desktops[j].position = static_cast<qint32>(j);
            }
            QDBusMessage removed = QDBusMessage::createSignal(QStringLiteral("/VirtualDesktopManager"),
                                                             QStringLiteral("org.kde.KWin.VirtualDesktopManager"),
                                                             QStringLiteral("desktopRemoved"));
            removed << call.id;
            m_connection.send(removed);
            return connection.send(message.createReply());
        }
        return connection.send(message.createErrorReply(QStringLiteral("org.kde.KWin.VirtualDesktopManager.Error"),
                                                        QStringLiteral("no such desktop")));
    }

    bool handleSet(const QDBusMessage &message, const QDBusConnection &connection)
    {
        const QVariantList args = message.arguments();
        if (args.size() != 3) {
            return connection.send(message.createErrorReply(QDBusError::InvalidArgs, QStringLiteral("bad args")));
        }
        Call call;
        call.method = QStringLiteral("Set");
        call.property = args.at(1).toString();
        call.value = args.at(2).canConvert<QDBusVariant>() ? qvariant_cast<QDBusVariant>(args.at(2)).variant()
                                                           : args.at(2);
        if (recordAndMaybeFail(call, message, connection)) {
            return true;
        }
        if (call.property == QLatin1String("rows")) {
            m_rows = call.value.toUInt();
        } else if (call.property == QLatin1String("navigationWrappingAround")) {
            m_wrapping = call.value.toBool();
        } else if (call.property == QLatin1String("current")) {
            const QString current = call.value.toString();
            if (!containsId(current)) {
                return connection.send(message.createErrorReply(
                    QStringLiteral("org.kde.KWin.VirtualDesktopManager.Error"), QStringLiteral("no such desktop")));
            }
            m_current = current;
        } else {
            return connection.send(message.createErrorReply(QDBusError::UnknownProperty, call.property));
        }
        return connection.send(message.createReply());
    }

    bool replyGetAll(const QDBusMessage &message, const QDBusConnection &connection) const
    {
        QDBusArgument mapArg;
        mapArg.beginMap(QMetaType::fromType<QString>(), QMetaType::fromType<QDBusVariant>());
        auto put = [&](const QString &key, const QVariant &value) {
            mapArg.beginMapEntry();
            mapArg << key << QDBusVariant(value);
            mapArg.endMapEntry();
        };
        put(QStringLiteral("count"), QVariant::fromValue(static_cast<uint>(m_desktops.size())));
        put(QStringLiteral("current"), QVariant(m_current));
        put(QStringLiteral("rows"), QVariant::fromValue(m_rows));
        put(QStringLiteral("navigationWrappingAround"), QVariant(m_wrapping));
        QDBusArgument desktopsArg;
        desktopsArg.beginArray(qMetaTypeId<VirtualDesktopDBus>());
        for (const DesktopTuple &tuple : m_desktops) {
            VirtualDesktopDBus row;
            row.position = tuple.position;
            row.id = tuple.id;
            row.name = tuple.name;
            desktopsArg << row;
        }
        desktopsArg.endArray();
        put(QStringLiteral("desktops"), QVariant::fromValue(desktopsArg));
        mapArg.endMap();

        QDBusMessage reply = message.createReply();
        reply << QVariant::fromValue(mapArg);
        return connection.send(reply);
    }

    QDBusConnection m_connection;
    QVector<DesktopTuple> m_desktops;
    QString m_current;
    uint m_rows = 1;
    bool m_wrapping = false;
    int m_generated = 0;
    QString m_failMethod;
    int m_failCall = -1;
    QVector<Call> m_calls;
};

QDBusConnection makeClientBus()
{
    static int serial = 0;
    return QDBusConnection::connectToBus(QDBusConnection::SessionBus,
                                         QStringLiteral("contextdeck-mutator-test-%1").arg(++serial));
}

bool registerFake(QDBusConnection &bus, FakeKwinManager *fake)
{
    bus.unregisterService(QStringLiteral("org.kde.KWin"));
    bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));
    if (!bus.registerService(QStringLiteral("org.kde.KWin"))) {
        return false;
    }
    return bus.registerVirtualObject(QStringLiteral("/VirtualDesktopManager"), fake, QDBusConnection::SingleNode);
}

WorkspaceSession makeSession(const QStringList &names, std::optional<int> rows = {},
                             std::optional<bool> wrapping = {})
{
    WorkspaceSession session;
    session.id = QStringLiteral("coding");
    session.displayName = QStringLiteral("Coding");
    session.rows = rows;
    session.navigationWrapping = wrapping;
    for (int i = 0; i < names.size(); ++i) {
        session.desktops.push_back(WorkspaceDesktopEntry{i + 1, names.at(i)});
    }
    return session;
}

ProfileDocument documentFor(const WorkspaceSession &session)
{
    ProfileDocument document;
    document.schemaVersion = kSchemaVersion;
    document.preferences.workspaceManagementEnabled = true;
    document.preferences.activeWorkspaceSessionId = session.id;
    document.workspaceSessions.push_back(session);
    return document;
}

} // namespace

class TestWorkspaceMutator : public QObject
{
    Q_OBJECT

private slots:
    void defaultApplyCreatesConditionalRenamesSetsRowsAndWrapping()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        FakeKwinManager fake(bus);
        fake.seed({{QStringLiteral("one"), QStringLiteral("One")}, {QStringLiteral("two"), QStringLiteral("Two")}});
        fake.setRows(1);
        fake.setWrapping(false);
        QVERIFY(registerFake(bus, &fake));
        QDBusConnection client = makeClientBus();
        WorkspaceReceiver receiver(client);
        receiver.setTimingForTest(200, {20, 40});
        QVERIFY(receiver.start());
        QTRY_COMPARE(receiver.state().availability, WorkspaceAvailability::Available);

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        DesktopMutator mutator(client);
        mutator.setCheckpointPath(dir.path() + QStringLiteral("/workspace-checkpoint.json"));
        connect(&receiver, &WorkspaceReceiver::desktopCreatedObserved, &mutator, &DesktopMutator::recordCreatedDesktop);

        const WorkspaceSession session =
            makeSession({QStringLiteral("Build"), QStringLiteral("Browse"), QStringLiteral("Test")}, 2, true);
        const ProfileDocument document = documentFor(session);
        const WorkspacePlan plan = computeWorkspacePlan(document, session.id, receiver.state(), {});
        const WorkspaceMutationResult result = mutator.apply(plan, receiver.state(), {});

        QVERIFY(result.ok);
        QVERIFY(!result.noChanges);
        QCOMPARE(result.createdCount, 1);
        QCOMPARE(result.renamedCount, 2);
        QCOMPARE(result.removedCount, 0);
        QCOMPARE(result.currentSwitched, false);
        QCOMPARE(result.createdIds, QVector<QString>{QStringLiteral("gen-1")});

        const QVector<FakeKwinManager::Call> creates = fake.callsOf(QStringLiteral("createDesktop"));
        QCOMPARE(creates.size(), 1);
        QCOMPARE(creates.at(0).position, quint32(2));
        QCOMPARE(creates.at(0).name, QStringLiteral("Test"));
        const QVector<FakeKwinManager::Call> renames = fake.callsOf(QStringLiteral("setDesktopName"));
        QCOMPARE(renames.size(), 2);
        QCOMPARE(renames.at(0).id, QStringLiteral("one"));
        QCOMPARE(renames.at(0).name, QStringLiteral("Build"));
        QCOMPARE(renames.at(1).id, QStringLiteral("two"));
        QCOMPARE(renames.at(1).name, QStringLiteral("Browse"));
        QCOMPARE(fake.callCount(QStringLiteral("removeDesktop")), 0);

        const QVector<FakeKwinManager::Call> sets = fake.callsOf(QStringLiteral("Set"));
        QCOMPARE(sets.size(), 2);
        QCOMPARE(sets.at(0).property, QStringLiteral("rows"));
        QCOMPARE(sets.at(0).value.toUInt(), quint32(2));
        QCOMPARE(sets.at(1).property, QStringLiteral("navigationWrappingAround"));
        QCOMPARE(sets.at(1).value.toBool(), true);

        const QVector<FakeKwinManager::Call> all = fake.calls();
        int createIndex = -1;
        int firstRenameIndex = -1;
        int rowsIndex = -1;
        int wrappingIndex = -1;
        for (int i = 0; i < all.size(); ++i) {
            if (all.at(i).method == QLatin1String("createDesktop") && createIndex < 0) {
                createIndex = i;
            }
            if (all.at(i).method == QLatin1String("setDesktopName") && firstRenameIndex < 0) {
                firstRenameIndex = i;
            }
            if (all.at(i).method == QLatin1String("Set") && all.at(i).property == QLatin1String("rows")
                && rowsIndex < 0) {
                rowsIndex = i;
            }
            if (all.at(i).method == QLatin1String("Set")
                && all.at(i).property == QLatin1String("navigationWrappingAround") && wrappingIndex < 0) {
                wrappingIndex = i;
            }
        }
        QVERIFY(createIndex >= 0);
        QVERIFY(firstRenameIndex > createIndex);
        QVERIFY(rowsIndex > firstRenameIndex);
        QVERIFY(wrappingIndex > rowsIndex);

        QCOMPARE(fake.desktopCount(), 3);
        QCOMPARE(fake.nameOf(QStringLiteral("gen-1")), QStringLiteral("Test"));
        bus.unregisterService(QStringLiteral("org.kde.KWin"));
        bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));
    }

    void matchingPlanWritesNothing()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        FakeKwinManager fake(bus);
        fake.seed({{QStringLiteral("one"), QStringLiteral("One")}, {QStringLiteral("two"), QStringLiteral("Two")}});
        fake.setRows(1);
        fake.setWrapping(false);
        QVERIFY(registerFake(bus, &fake));
        QDBusConnection client = makeClientBus();
        WorkspaceReceiver receiver(client);
        receiver.setTimingForTest(200, {20, 40});
        QVERIFY(receiver.start());
        QTRY_COMPARE(receiver.state().availability, WorkspaceAvailability::Available);

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        DesktopMutator mutator(client);
        const QString checkpointPath = dir.path() + QStringLiteral("/workspace-checkpoint.json");
        mutator.setCheckpointPath(checkpointPath);

        const WorkspaceSession session = makeSession({QStringLiteral("One"), QStringLiteral("Two")}, 1, false);
        const ProfileDocument document = documentFor(session);
        const WorkspacePlan plan = computeWorkspacePlan(document, session.id, receiver.state(), {});
        const WorkspaceMutationResult result = mutator.apply(plan, receiver.state(), {});

        QVERIFY(result.ok);
        QVERIFY(result.noChanges);
        QCOMPARE(fake.callCount(QStringLiteral("createDesktop")), 0);
        QCOMPARE(fake.callCount(QStringLiteral("setDesktopName")), 0);
        QCOMPARE(fake.callCount(QStringLiteral("Set")), 0);
        QCOMPARE(fake.callCount(QStringLiteral("removeDesktop")), 0);
        QVERIFY(!QFile::exists(checkpointPath));
        bus.unregisterService(QStringLiteral("org.kde.KWin"));
        bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));
    }

    void excludedRemoveAndCurrentByDefault()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        FakeKwinManager fake(bus);
        fake.seed({{QStringLiteral("one"), QStringLiteral("One")},
                   {QStringLiteral("two"), QStringLiteral("Two")},
                   {QStringLiteral("extra"), QStringLiteral("Extra")}});
        QVERIFY(registerFake(bus, &fake));
        QDBusConnection client = makeClientBus();
        WorkspaceReceiver receiver(client);
        receiver.setTimingForTest(200, {20, 40});
        QVERIFY(receiver.start());
        QTRY_COMPARE(receiver.state().availability, WorkspaceAvailability::Available);

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        DesktopMutator mutator(client);
        const QString checkpointPath = dir.path() + QStringLiteral("/workspace-checkpoint.json");
        mutator.setCheckpointPath(checkpointPath);

        const WorkspaceSession session = makeSession({QStringLiteral("One"), QStringLiteral("Two")});
        const ProfileDocument document = documentFor(session);
        const WorkspacePlan plan = computeWorkspacePlan(document, session.id, receiver.state(), {});
        const WorkspaceMutationResult result = mutator.apply(plan, receiver.state(), {});

        QVERIFY(result.ok);
        QVERIFY(result.noChanges);
        QCOMPARE(fake.callCount(QStringLiteral("removeDesktop")), 0);
        for (const FakeKwinManager::Call &call : fake.callsOf(QStringLiteral("Set"))) {
            QVERIFY(call.property != QLatin1String("current"));
        }
        QVERIFY(!QFile::exists(checkpointPath));
        QCOMPARE(fake.desktopCount(), 3);
        bus.unregisterService(QStringLiteral("org.kde.KWin"));
        bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));
    }

    void optInCurrentAndRemoveRunLast()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        FakeKwinManager fake(bus);
        fake.seed({{QStringLiteral("one"), QStringLiteral("One")},
                   {QStringLiteral("two"), QStringLiteral("Two")},
                   {QStringLiteral("extra"), QStringLiteral("Extra")}});
        QVERIFY(registerFake(bus, &fake));
        QDBusConnection client = makeClientBus();
        WorkspaceReceiver receiver(client);
        receiver.setTimingForTest(200, {20, 40});
        QVERIFY(receiver.start());
        QTRY_COMPARE(receiver.state().availability, WorkspaceAvailability::Available);

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        DesktopMutator mutator(client);
        mutator.setCheckpointPath(dir.path() + QStringLiteral("/workspace-checkpoint.json"));

        const WorkspaceSession session = makeSession({QStringLiteral("One"), QStringLiteral("Two")});
        const ProfileDocument document = documentFor(session);
        const WorkspacePlan plan = computeWorkspacePlan(document, session.id, receiver.state(), {});
        WorkspaceMutationOptions options;
        options.switchCurrent = true;
        options.removeExtras = true;
        const WorkspaceMutationResult result = mutator.apply(plan, receiver.state(), options);

        QVERIFY(result.ok);
        QVERIFY(result.currentSwitched);
        QCOMPARE(result.removedCount, 1);
        const QVector<FakeKwinManager::Call> all = fake.calls();
        int currentIndex = -1;
        int removeIndex = -1;
        for (int i = 0; i < all.size(); ++i) {
            if (all.at(i).method == QLatin1String("Set") && all.at(i).property == QLatin1String("current")) {
                currentIndex = i;
                QCOMPARE(all.at(i).value.toString(), QStringLiteral("one"));
            }
            if (all.at(i).method == QLatin1String("removeDesktop")) {
                removeIndex = i;
                QCOMPARE(all.at(i).id, QStringLiteral("extra"));
            }
        }
        QVERIFY(currentIndex >= 0);
        QVERIFY(removeIndex > currentIndex);
        QCOMPARE(fake.desktopCount(), 2);
        QVERIFY(!fake.containsId(QStringLiteral("extra")));
        bus.unregisterService(QStringLiteral("org.kde.KWin"));
        bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));
    }

    void abortOnRenameErrorRunsRevert()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        FakeKwinManager fake(bus);
        fake.seed({{QStringLiteral("one"), QStringLiteral("One")}, {QStringLiteral("two"), QStringLiteral("Two")}});
        fake.setRows(1);
        fake.setWrapping(false);
        fake.setFailMethod(QStringLiteral("setDesktopName"), 2);
        QVERIFY(registerFake(bus, &fake));
        QDBusConnection client = makeClientBus();
        WorkspaceReceiver receiver(client);
        receiver.setTimingForTest(200, {20, 40});
        QVERIFY(receiver.start());
        QTRY_COMPARE(receiver.state().availability, WorkspaceAvailability::Available);

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        DesktopMutator mutator(client);
        const QString checkpointPath = dir.path() + QStringLiteral("/workspace-checkpoint.json");
        mutator.setCheckpointPath(checkpointPath);
        connect(&receiver, &WorkspaceReceiver::desktopCreatedObserved, &mutator, &DesktopMutator::recordCreatedDesktop);

        const WorkspaceSession session =
            makeSession({QStringLiteral("Build"), QStringLiteral("Browse"), QStringLiteral("Test")}, 2, true);
        const ProfileDocument document = documentFor(session);
        const WorkspacePlan plan = computeWorkspacePlan(document, session.id, receiver.state(), {});
        const WorkspaceMutationResult result = mutator.apply(plan, receiver.state(), {});

        QVERIFY(!result.ok);
        QCOMPARE(result.failureClass, QStringLiteral("mutation-error"));
        QVERIFY(result.reverted);
        QVERIFY(!result.revertFailed);
        QVERIFY(result.residualClass.isEmpty());
        QCOMPARE(fake.desktopCount(), 2);
        QVERIFY(!fake.containsId(QStringLiteral("gen-1")));
        QCOMPARE(fake.nameOf(QStringLiteral("one")), QStringLiteral("One"));
        QCOMPARE(fake.nameOf(QStringLiteral("two")), QStringLiteral("Two"));
        QCOMPARE(fake.rows(), quint32(1));
        QCOMPARE(fake.wrapping(), false);
        QVERIFY(!QFile::exists(checkpointPath));
        bus.unregisterService(QStringLiteral("org.kde.KWin"));
        bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));
    }

    void checkpointWrittenBeforeFirstMutationAndUserOnly()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        FakeKwinManager fake(bus);
        fake.seed({{QStringLiteral("one"), QStringLiteral("One")}, {QStringLiteral("two"), QStringLiteral("Two")}});
        QVERIFY(registerFake(bus, &fake));
        QDBusConnection client = makeClientBus();
        WorkspaceReceiver receiver(client);
        receiver.setTimingForTest(200, {20, 40});
        QVERIFY(receiver.start());
        QTRY_COMPARE(receiver.state().availability, WorkspaceAvailability::Available);

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        DesktopMutator mutator(client);
        const QString checkpointPath = dir.path() + QStringLiteral("/workspace-checkpoint.json");
        mutator.setCheckpointPath(checkpointPath);
        connect(&receiver, &WorkspaceReceiver::desktopCreatedObserved, &mutator, &DesktopMutator::recordCreatedDesktop);

        bool checkpointExistedAtFirstMutation = false;
        fake.onBeforeMutation = [&checkpointExistedAtFirstMutation, &checkpointPath]() {
            checkpointExistedAtFirstMutation = QFile::exists(checkpointPath);
        };

        const WorkspaceSession session =
            makeSession({QStringLiteral("One"), QStringLiteral("Two"), QStringLiteral("Test")});
        const ProfileDocument document = documentFor(session);
        const WorkspacePlan plan = computeWorkspacePlan(document, session.id, receiver.state(), {});
        const WorkspaceMutationResult result = mutator.apply(plan, receiver.state(), {});

        QVERIFY(result.ok);
        QVERIFY(checkpointExistedAtFirstMutation);
        QVERIFY(QFile::exists(checkpointPath));
        const QFile::Permissions permissions = QFile::permissions(checkpointPath);
        QVERIFY(!(permissions & (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ReadOther
                                 | QFileDevice::WriteOther | QFileDevice::ExeGroup | QFileDevice::ExeOther)));
        const WorkspaceCheckpoint checkpoint(checkpointPath);
        const std::optional<WorkspaceCheckpointData> data = checkpoint.load();
        QVERIFY(data.has_value());
        QCOMPARE(data->desktops.size(), 2);
        QCOMPARE(data->createdIds, QStringList{QStringLiteral("gen-1")});
        bus.unregisterService(QStringLiteral("org.kde.KWin"));
        bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));
    }

    void checkpointOverwrittenPerApply()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        FakeKwinManager fake(bus);
        fake.seed({{QStringLiteral("one"), QStringLiteral("One")}, {QStringLiteral("two"), QStringLiteral("Two")}});
        QVERIFY(registerFake(bus, &fake));
        QDBusConnection client = makeClientBus();
        WorkspaceReceiver receiver(client);
        receiver.setTimingForTest(200, {20, 40});
        QVERIFY(receiver.start());
        QTRY_COMPARE(receiver.state().availability, WorkspaceAvailability::Available);

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        DesktopMutator mutator(client);
        const QString checkpointPath = dir.path() + QStringLiteral("/workspace-checkpoint.json");
        mutator.setCheckpointPath(checkpointPath);
        connect(&receiver, &WorkspaceReceiver::desktopCreatedObserved, &mutator, &DesktopMutator::recordCreatedDesktop);

        WorkspaceSession session = makeSession({QStringLiteral("One"), QStringLiteral("Two"), QStringLiteral("Test")});
        ProfileDocument document = documentFor(session);
        WorkspacePlan plan = computeWorkspacePlan(document, session.id, receiver.state(), {});
        QVERIFY(mutator.apply(plan, receiver.state(), {}).ok);
        QTRY_COMPARE(fake.desktopCount(), 3);

        QTRY_VERIFY(receiver.state().desktops.size() == 3);
        session.desktops.push_back(WorkspaceDesktopEntry{4, QStringLiteral("Next")});
        document.workspaceSessions[0] = session;
        plan = computeWorkspacePlan(document, session.id, receiver.state(), {});
        QVERIFY(mutator.apply(plan, receiver.state(), {}).ok);

        const WorkspaceCheckpoint checkpoint(checkpointPath);
        const std::optional<WorkspaceCheckpointData> data = checkpoint.load();
        QVERIFY(data.has_value());
        QCOMPARE(data->createdIds, QStringList{QStringLiteral("gen-2")});
        QCOMPARE(data->desktops.size(), 3);
        QCOMPARE(fake.desktopCount(), 4);
        bus.unregisterService(QStringLiteral("org.kde.KWin"));
        bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));
    }

    void revertRemovesOnlyThisApplyUuids()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        FakeKwinManager fake(bus);
        fake.seed({{QStringLiteral("one"), QStringLiteral("One")}, {QStringLiteral("two"), QStringLiteral("Two")}});
        QVERIFY(registerFake(bus, &fake));
        QDBusConnection client = makeClientBus();
        WorkspaceReceiver receiver(client);
        receiver.setTimingForTest(200, {20, 40});
        QVERIFY(receiver.start());
        QTRY_COMPARE(receiver.state().availability, WorkspaceAvailability::Available);

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        DesktopMutator mutator(client);
        const QString checkpointPath = dir.path() + QStringLiteral("/workspace-checkpoint.json");
        mutator.setCheckpointPath(checkpointPath);
        connect(&receiver, &WorkspaceReceiver::desktopCreatedObserved, &mutator, &DesktopMutator::recordCreatedDesktop);

        const WorkspaceSession session =
            makeSession({QStringLiteral("One"), QStringLiteral("Two"), QStringLiteral("Test")});
        const ProfileDocument document = documentFor(session);
        WorkspacePlan plan = computeWorkspacePlan(document, session.id, receiver.state(), {});
        QVERIFY(mutator.apply(plan, receiver.state(), {}).ok);
        QTRY_COMPARE(fake.desktopCount(), 3);

        fake.appendDesktop(QStringLiteral("user-x"), QStringLiteral("User"));
        fake.emitDesktopCreated(QStringLiteral("user-x"), 3);
        QTRY_COMPARE(receiver.state().desktops.size(), 4);

        const WorkspaceMutationResult result = mutator.revert(receiver.state());
        QVERIFY(result.ok);
        QVERIFY(result.reverted);
        QVERIFY(!result.revertFailed);
        QVERIFY(!fake.containsId(QStringLiteral("gen-1")));
        QVERIFY(fake.containsId(QStringLiteral("user-x")));
        QCOMPARE(fake.desktopCount(), 3);
        QVERIFY(!QFile::exists(checkpointPath));
        bus.unregisterService(QStringLiteral("org.kde.KWin"));
        bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));
    }

    void currentRestoreSkippedWhenUuidAbsent()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        FakeKwinManager fake(bus);
        fake.seed({{QStringLiteral("one"), QStringLiteral("One")}, {QStringLiteral("two"), QStringLiteral("Two")}});
        QVERIFY(registerFake(bus, &fake));
        QDBusConnection client = makeClientBus();
        WorkspaceReceiver receiver(client);
        receiver.setTimingForTest(200, {20, 40});
        QVERIFY(receiver.start());
        QTRY_COMPARE(receiver.state().availability, WorkspaceAvailability::Available);

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        DesktopMutator mutator(client);
        const QString checkpointPath = dir.path() + QStringLiteral("/workspace-checkpoint.json");
        mutator.setCheckpointPath(checkpointPath);

        const WorkspaceSession session = makeSession({QStringLiteral("One"), QStringLiteral("Two")});
        const ProfileDocument document = documentFor(session);
        const WorkspacePlan plan = computeWorkspacePlan(document, session.id, receiver.state(), {});
        WorkspaceMutationOptions options;
        options.switchCurrent = true;
        const WorkspaceMutationResult applied = mutator.apply(plan, receiver.state(), options);
        QVERIFY(applied.ok);
        QVERIFY(applied.currentSwitched);
        QVERIFY(QFile::exists(checkpointPath));

        fake.removeDesktopDirect(QStringLiteral("one"));
        QTRY_VERIFY(receiver.state().desktops.size() == 1);

        const WorkspaceMutationResult result = mutator.revert(receiver.state());
        QVERIFY(result.ok);
        QVERIFY(result.reverted);
        QVERIFY(!result.revertFailed);
        QCOMPARE(result.residualClass, QStringLiteral("current-restore-skipped"));
        QVERIFY(!QFile::exists(checkpointPath));
        bus.unregisterService(QStringLiteral("org.kde.KWin"));
        bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));
    }

    void inTransactionTriggerClassification()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        FakeKwinManager fake(bus);
        fake.seed({{QStringLiteral("one"), QStringLiteral("One")}, {QStringLiteral("two"), QStringLiteral("Two")}});
        QVERIFY(registerFake(bus, &fake));
        QDBusConnection client = makeClientBus();
        WorkspaceReceiver receiver(client);
        receiver.setTimingForTest(200, {20, 40});
        QVERIFY(receiver.start());
        QTRY_COMPARE(receiver.state().availability, WorkspaceAvailability::Available);

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        DesktopMutator mutator(client);
        mutator.setCheckpointPath(dir.path() + QStringLiteral("/workspace-checkpoint.json"));
        connect(&receiver, &WorkspaceReceiver::desktopCreatedObserved, &mutator, &DesktopMutator::recordCreatedDesktop);
        QSignalSpy createdSpy(&mutator, &DesktopMutator::desktopCreatedInTransaction);

        const WorkspaceSession session =
            makeSession({QStringLiteral("One"), QStringLiteral("Two"), QStringLiteral("Test")});
        const ProfileDocument document = documentFor(session);
        const WorkspacePlan plan = computeWorkspacePlan(document, session.id, receiver.state(), {});
        QVERIFY(mutator.apply(plan, receiver.state(), {}).ok);
        QCOMPARE(createdSpy.count(), 1);
        QCOMPARE(createdSpy.at(0).at(0).toString(), QStringLiteral("gen-1"));
        QCOMPARE(createdSpy.at(0).at(1).toInt(), 2);

        fake.appendDesktop(QStringLiteral("user-x"), QStringLiteral("User"));
        fake.emitDesktopCreated(QStringLiteral("user-x"), 2);
        QCoreApplication::processEvents();
        QCOMPARE(createdSpy.count(), 1);

        fake.emitCurrentChanged();
        QCoreApplication::processEvents();
        QCOMPARE(createdSpy.count(), 1);
        bus.unregisterService(QStringLiteral("org.kde.KWin"));
        bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));
    }
};

QTEST_GUILESS_MAIN(TestWorkspaceMutator)
#include "test_workspace_mutator.moc"
