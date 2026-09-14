#include "context/WorkspaceReceiver.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <QVariantMap>

#include <algorithm>
#include <optional>

using namespace contextdeck;

struct DesktopTuple {
    qint32 position = 0;
    QString id;
    QString name;
};

struct UnsignedDesktopTuple {
    quint32 position = 0;
    QString id;
    QString name;
};

Q_DECLARE_METATYPE(DesktopTuple)
Q_DECLARE_METATYPE(UnsignedDesktopTuple)

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

QDBusArgument &operator<<(QDBusArgument &argument, const UnsignedDesktopTuple &tuple)
{
    argument.beginStructure();
    argument << tuple.position << tuple.id << tuple.name;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, UnsignedDesktopTuple &tuple)
{
    argument.beginStructure();
    argument >> tuple.position >> tuple.id >> tuple.name;
    argument.endStructure();
    return argument;
}

namespace {

class FakeDesktopManager : public QDBusVirtualObject
{
public:
    explicit FakeDesktopManager(QDBusConnection connection, QObject *parent = nullptr)
        : QDBusVirtualObject(parent)
        , m_connection(std::move(connection))
    {
        qDBusRegisterMetaType<DesktopTuple>();
        qDBusRegisterMetaType<UnsignedDesktopTuple>();
        registerWorkspaceDesktopDBusTypes();
        addDesktop(0, QStringLiteral("one"), QStringLiteral("One"));
        addDesktop(1, QStringLiteral("two"), QStringLiteral("Two"));
        m_current = QStringLiteral("one");
    }

    QString introspect(const QString &) const override
    {
        return QStringLiteral(
            "<interface name=\"org.kde.KWin.VirtualDesktopManager\">"
            "<property name=\"count\" type=\"u\" access=\"read\"/>"
            "<property name=\"current\" type=\"s\" access=\"read\"/>"
            "<property name=\"desktops\" type=\"a(iss)\" access=\"read\"/>"
            "<property name=\"rows\" type=\"u\" access=\"read\"/>"
            "<property name=\"navigationWrappingAround\" type=\"b\" access=\"read\"/>"
            "<signal name=\"currentChanged\"><arg type=\"s\"/></signal>"
            "<signal name=\"countChanged\"><arg type=\"u\"/></signal>"
            "<signal name=\"desktopCreated\"><arg type=\"s\"/></signal>"
            "<signal name=\"desktopRemoved\"><arg type=\"s\"/></signal>"
            "<signal name=\"desktopDataChanged\"><arg type=\"s\"/></signal>"
            "<signal name=\"rowsChanged\"><arg type=\"u\"/></signal>"
            "<signal name=\"navigationWrappingAroundChanged\"><arg type=\"b\"/></signal>"
            "</interface>");
    }

    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override
    {
        if (message.interface() == QLatin1String("org.freedesktop.DBus.Properties")
            && message.member() == QLatin1String("GetAll")) {
            ++m_getAllCount;
            ++m_outstandingHandlers;
            m_maxOutstandingHandlers = std::max(m_maxOutstandingHandlers, m_outstandingHandlers);
            if (m_holdReplies) {
                m_held.push_back(PendingGetAll{message, connection});
                return true;
            }
            if (m_delayMs > 0) {
                const int delay = m_delayMs;
                QTimer::singleShot(delay, this, [this, message, connection]() {
                    finishGetAll(message, connection);
                });
                return true;
            }
            return finishGetAll(message, connection);
        }
        return false;
    }

    void addDesktop(int position, const QString &id, const QString &name)
    {
        DesktopTuple tuple;
        tuple.position = static_cast<qint32>(position);
        tuple.id = id;
        tuple.name = name;
        m_desktops.push_back(tuple);
    }

    void setDesktops(const QVector<DesktopTuple> &desktops) { m_desktops = desktops; }
    void resetDesktops() { m_desktops.clear(); }
    void removeDesktop(const QString &id)
    {
        m_desktops.erase(std::remove_if(m_desktops.begin(), m_desktops.end(),
                                        [&id](const DesktopTuple &row) { return row.id == id; }),
                         m_desktops.end());
    }
    void setCurrent(const QString &id) { m_current = id; }
    void setMalformed(bool malformed) { m_malformed = malformed; }
    void setMissingCurrent(bool missing) { m_missingCurrent = missing; }
    void setEmptyCurrent(bool enabled) { m_emptyCurrent = enabled; }
    void setUnsignedPositions(bool enabled) { m_unsigned = enabled; }
    void setDelayMs(int delayMs) { m_delayMs = delayMs; }
    void setEmptyName(bool enabled) { m_emptyName = enabled; }
    void setHoldReplies(bool enabled) { m_holdReplies = enabled; }
    void setCountOverride(int count) { m_countOverride = count; }
    void setRows(int rows) { m_rows = static_cast<uint>(rows); }
    void setWrapping(bool enabled) { m_wrapping = enabled; }
    void setMalformedWrapping(bool enabled) { m_malformedWrapping = enabled; }
    void setExtraKey(bool enabled) { m_extraKey = enabled; }
    void setName(int index, const QString &name) { m_desktops[index].name = name; }
    [[nodiscard]] int getAllCount() const { return m_getAllCount; }
    [[nodiscard]] int desktopCount() const { return static_cast<int>(m_desktops.size()); }
    [[nodiscard]] int outstandingHandlers() const { return m_outstandingHandlers; }
    [[nodiscard]] int maxOutstandingHandlers() const { return m_maxOutstandingHandlers; }

    void releaseHeld()
    {
        const QVector<PendingGetAll> held = m_held;
        m_held.clear();
        for (const PendingGetAll &pending : held) {
            finishGetAll(pending.message, pending.connection);
        }
    }

    void releaseOldestHeld()
    {
        if (m_held.isEmpty()) {
            return;
        }
        const PendingGetAll pending = m_held.takeFirst();
        finishGetAll(pending.message, pending.connection);
    }

    void emitCurrentChanged()
    {
        QDBusMessage signal = QDBusMessage::createSignal(QStringLiteral("/VirtualDesktopManager"),
                                                         QStringLiteral("org.kde.KWin.VirtualDesktopManager"),
                                                         QStringLiteral("currentChanged"));
        signal << m_current;
        m_connection.send(signal);
    }

    void emitCountChanged()
    {
        QDBusMessage signal = QDBusMessage::createSignal(QStringLiteral("/VirtualDesktopManager"),
                                                         QStringLiteral("org.kde.KWin.VirtualDesktopManager"),
                                                         QStringLiteral("countChanged"));
        signal << static_cast<uint>(m_desktops.size());
        m_connection.send(signal);
    }

    void emitDesktopRemoved()
    {
        QDBusMessage signal = QDBusMessage::createSignal(QStringLiteral("/VirtualDesktopManager"),
                                                         QStringLiteral("org.kde.KWin.VirtualDesktopManager"),
                                                         QStringLiteral("desktopRemoved"));
        signal << QStringLiteral("two");
        m_connection.send(signal);
    }

    void emitDesktopDataChanged()
    {
        QDBusMessage signal = QDBusMessage::createSignal(QStringLiteral("/VirtualDesktopManager"),
                                                         QStringLiteral("org.kde.KWin.VirtualDesktopManager"),
                                                         QStringLiteral("desktopDataChanged"));
        signal << QStringLiteral("one");
        m_connection.send(signal);
    }

    void emitRowsChanged()
    {
        QDBusMessage signal = QDBusMessage::createSignal(QStringLiteral("/VirtualDesktopManager"),
                                                         QStringLiteral("org.kde.KWin.VirtualDesktopManager"),
                                                         QStringLiteral("rowsChanged"));
        signal << m_rows;
        m_connection.send(signal);
    }

    void emitNavigationWrappingChanged()
    {
        QDBusMessage signal = QDBusMessage::createSignal(QStringLiteral("/VirtualDesktopManager"),
                                                         QStringLiteral("org.kde.KWin.VirtualDesktopManager"),
                                                         QStringLiteral("navigationWrappingAroundChanged"));
        signal << m_wrapping;
        m_connection.send(signal);
    }

    bool finishGetAll(const QDBusMessage &message, const QDBusConnection &connection)
    {
        const bool ok = sendAll(message, connection);
        if (m_outstandingHandlers > 0) {
            --m_outstandingHandlers;
        }
        return ok;
    }

    bool sendAll(const QDBusMessage &message, const QDBusConnection &connection) const
    {
        QDBusArgument mapArg;
        mapArg.beginMap(QMetaType::fromType<QString>(), QMetaType::fromType<QDBusVariant>());
        auto put = [&](const QString &key, const QVariant &value) {
            mapArg.beginMapEntry();
            mapArg << key << QDBusVariant(value);
            mapArg.endMapEntry();
        };

        if (m_malformed) {
            put(QStringLiteral("count"), QVariant::fromValue(uint(2)));
            put(QStringLiteral("current"), QVariant(m_current));
            put(QStringLiteral("desktops"), QVariant(QStringLiteral("nope")));
        } else {
            const int count = m_countOverride.value_or(static_cast<int>(m_desktops.size()));
            put(QStringLiteral("count"), QVariant::fromValue(static_cast<uint>(count)));
            QString current = m_current;
            if (m_emptyCurrent) {
                current.clear();
            } else if (m_missingCurrent) {
                current = QStringLiteral("missing");
            }
            put(QStringLiteral("current"), QVariant(current));
            put(QStringLiteral("rows"), QVariant::fromValue(m_rows));
            if (m_malformedWrapping) {
                put(QStringLiteral("navigationWrappingAround"), QVariant(QStringLiteral("yes")));
            } else {
                put(QStringLiteral("navigationWrappingAround"), QVariant(m_wrapping));
            }
            if (m_extraKey) {
                put(QStringLiteral("futureProperty"), QVariant(1));
            }

            QDBusArgument desktopsArg;
            if (m_unsigned) {
                desktopsArg.beginArray(qMetaTypeId<VirtualDesktopDBusUnsigned>());
                for (const DesktopTuple &tuple : m_desktops) {
                    VirtualDesktopDBusUnsigned row;
                    row.position = static_cast<quint32>(tuple.position);
                    row.id = tuple.id;
                    row.name = m_emptyName ? QString() : tuple.name;
                    desktopsArg << row;
                }
                desktopsArg.endArray();
            } else {
                desktopsArg.beginArray(qMetaTypeId<VirtualDesktopDBus>());
                for (const DesktopTuple &tuple : m_desktops) {
                    VirtualDesktopDBus row;
                    row.position = tuple.position;
                    row.id = tuple.id;
                    row.name = m_emptyName ? QString() : tuple.name;
                    desktopsArg << row;
                }
                desktopsArg.endArray();
            }
            put(QStringLiteral("desktops"), QVariant::fromValue(desktopsArg));
        }
        mapArg.endMap();

        QDBusMessage reply = message.createReply();
        reply << QVariant::fromValue(mapArg);
        return connection.send(reply);
    }

private:
    struct PendingGetAll {
        QDBusMessage message;
        QDBusConnection connection;
    };

    QDBusConnection m_connection;
    QVector<DesktopTuple> m_desktops;
    QString m_current;
    bool m_malformed = false;
    bool m_missingCurrent = false;
    bool m_emptyCurrent = false;
    bool m_unsigned = false;
    bool m_emptyName = false;
    bool m_holdReplies = false;
    bool m_malformedWrapping = false;
    bool m_extraKey = false;
    uint m_rows = 1;
    bool m_wrapping = false;
    int m_delayMs = 0;
    int m_getAllCount = 0;
    int m_outstandingHandlers = 0;
    int m_maxOutstandingHandlers = 0;
    std::optional<int> m_countOverride;
    QVector<PendingGetAll> m_held;
};

bool registerFake(QDBusConnection &bus, FakeDesktopManager *fake)
{
    bus.unregisterService(QStringLiteral("org.kde.KWin"));
    bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));
    QDBusConnection::sessionBus().unregisterService(QStringLiteral("org.kde.KWin"));
    QDBusConnection::sessionBus().unregisterObject(QStringLiteral("/VirtualDesktopManager"));
    if (!bus.registerService(QStringLiteral("org.kde.KWin"))) {
        return false;
    }
    return bus.registerVirtualObject(QStringLiteral("/VirtualDesktopManager"), fake, QDBusConnection::SingleNode);
}

QDBusConnection makeClientBus()
{
    static int serial = 0;
    const QString name = QStringLiteral("contextdeck-ws-test-%1").arg(++serial);
    return QDBusConnection::connectToBus(QDBusConnection::SessionBus, name);
}

} // namespace

class TestWorkspaceReceiver : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(QDBusConnection::sessionBus().isConnected());
        registerWorkspaceDesktopDBusTypes();
    }

    void initialSnapshotAndEmptyNameFallback()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        FakeDesktopManager fake(bus);
        fake.setEmptyName(true);
        QVERIFY(registerFake(bus, &fake));

        QDBusConnection client = makeClientBus();
        WorkspaceReceiver receiver(client);
        receiver.setTimingForTest(200, {20, 40});
        QSignalSpy spy(&receiver, &WorkspaceReceiver::stateChanged);
        QVERIFY(receiver.start());
        QTRY_COMPARE(receiver.state().availability, WorkspaceAvailability::Available);
        QCOMPARE(receiver.state().desktops.size(), 2);
        QCOMPARE(receiver.state().currentOrdinal, 1);
        QCOMPARE(receiver.state().desktops.at(0).displayName, QStringLiteral("Plocha 1"));
        QVERIFY(spy.count() >= 1);
        QCOMPARE(fake.getAllCount(), 1);
        QCOMPARE(receiver.logicalRequestCount(), quint64(1));
        bus.unregisterService(QStringLiteral("org.kde.KWin"));
        bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));
    }

    void unsignedPositionsAreAccepted()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        FakeDesktopManager fake(bus);
        fake.setUnsignedPositions(true);
        QVERIFY(registerFake(bus, &fake));
        QDBusConnection client = makeClientBus();
        WorkspaceReceiver receiver(client);
        receiver.setTimingForTest(200, {20, 40});
        QVERIFY(receiver.start());
        QTRY_COMPARE(receiver.state().availability, WorkspaceAvailability::Available);
        QCOMPARE(receiver.state().desktops.at(1).position, 1);
        bus.unregisterService(QStringLiteral("org.kde.KWin"));
        bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));
    }

    void subscribeBeforeSnapshotRace()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        FakeDesktopManager fake(bus);
        fake.setDelayMs(40);
        QVERIFY(registerFake(bus, &fake));
        QDBusConnection client = makeClientBus();
        WorkspaceReceiver receiver(client);
        receiver.setTimingForTest(200, {20, 40});
        QVERIFY(receiver.start());
        fake.setCurrent(QStringLiteral("two"));
        fake.emitCurrentChanged();
        QTRY_COMPARE(receiver.state().currentOrdinal, 2);
        QVERIFY(fake.getAllCount() >= 2);
        bus.unregisterService(QStringLiteral("org.kde.KWin"));
        bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));
    }

    void staleReplyAndInvalidationBurst()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        FakeDesktopManager fake(bus);
        fake.setDelayMs(80);
        QVERIFY(registerFake(bus, &fake));
        QDBusConnection client = makeClientBus();
        WorkspaceReceiver receiver(client);
        receiver.setTimingForTest(500, {20, 40});
        QVERIFY(receiver.start());
        fake.emitCountChanged();
        fake.emitDesktopRemoved();
        fake.emitDesktopDataChanged();
        QTRY_VERIFY(receiver.invalidationCount() >= 3);
        QTRY_COMPARE(receiver.state().availability, WorkspaceAvailability::Available);
        QCOMPARE(receiver.logicalRequestCount(), quint64(2));
        QCOMPARE(fake.getAllCount(), 2);
        bus.unregisterService(QStringLiteral("org.kde.KWin"));
        bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));
    }

    void malformedAndMissingCurrentBecomeUnknown()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        FakeDesktopManager fake(bus);
        fake.setMalformed(true);
        QVERIFY(registerFake(bus, &fake));
        QDBusConnection client = makeClientBus();
        WorkspaceReceiver receiver(client);
        receiver.setTimingForTest(200, {20, 40});
        QVERIFY(receiver.start());
        QTRY_COMPARE(receiver.errorClass(), QStringLiteral("desktops-type"));
        QCOMPARE(receiver.state().availability, WorkspaceAvailability::Unknown);
        bus.unregisterService(QStringLiteral("org.kde.KWin"));
        bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));
    }

    void missingCurrentIdIsUnknown()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        FakeDesktopManager fake(bus);
        fake.setMissingCurrent(true);
        QVERIFY(registerFake(bus, &fake));
        QDBusConnection client = makeClientBus();
        WorkspaceReceiver receiver(client);
        receiver.setTimingForTest(200, {20, 40});
        QVERIFY(receiver.start());
        QTRY_COMPARE(receiver.errorClass(), QStringLiteral("current-missing"));
        QCOMPARE(receiver.state().availability, WorkspaceAvailability::Unknown);
        bus.unregisterService(QStringLiteral("org.kde.KWin"));
        bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));
    }

    void serviceLossAndDuplicateSnapshot()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        FakeDesktopManager fake(bus);
        QVERIFY(registerFake(bus, &fake));
        QDBusConnection client = makeClientBus();
        WorkspaceReceiver receiver(client);
        receiver.setTimingForTest(200, {20, 40});
        QVERIFY(receiver.start());
        QTRY_COMPARE(receiver.state().availability, WorkspaceAvailability::Available);
        const quint64 notifications = receiver.notificationCount();
        fake.emitDesktopDataChanged();
        QTRY_VERIFY(receiver.snapshotCount() >= 2);
        QCOMPARE(receiver.notificationCount(), notifications);
        QVERIFY(bus.unregisterService(QStringLiteral("org.kde.KWin")));
        QTRY_COMPARE(receiver.state().availability, WorkspaceAvailability::Unknown);
        QCOMPARE(receiver.errorClass(), QStringLiteral("service-lost"));
        bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));
    }

    void ownerReplacementDeadlineAndRetryBudget()
    {
        QDBusConnection first = QDBusConnection::sessionBus();
        auto *fake = new FakeDesktopManager(first);
        QVERIFY(registerFake(first, fake));
        QDBusConnection client = makeClientBus();
        WorkspaceReceiver receiver(client);
        receiver.setTimingForTest(50, {20, 30, 40});
        QVERIFY(receiver.start());
        QTRY_COMPARE(receiver.state().availability, WorkspaceAvailability::Available);

        QVERIFY(first.unregisterService(QStringLiteral("org.kde.KWin")));
        first.unregisterObject(QStringLiteral("/VirtualDesktopManager"));
        QTRY_COMPARE(receiver.state().availability, WorkspaceAvailability::Unknown);

        QDBusConnection second = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("kwin-owner-2"));
        QVERIFY(second.isConnected());
        auto *fake2 = new FakeDesktopManager(second);
        QVERIFY(second.registerService(QStringLiteral("org.kde.KWin")));
        QVERIFY(second.registerVirtualObject(QStringLiteral("/VirtualDesktopManager"), fake2, QDBusConnection::SingleNode));
        QTRY_COMPARE(receiver.state().availability, WorkspaceAvailability::Available);

        fake2->setDelayMs(80);
        fake2->emitCurrentChanged();
        QTRY_COMPARE(receiver.state().availability, WorkspaceAvailability::Unknown);
        QCOMPARE(receiver.errorClass(), QStringLiteral("refresh-deadline"));
        QTRY_VERIFY(receiver.recoveryAttempt() >= 1);

        fake2->setDelayMs(0);
        receiver.setPaused(true);
        QCOMPARE(receiver.state().availability, WorkspaceAvailability::Unknown);
        receiver.setPaused(false);
        QTRY_COMPARE(receiver.state().availability, WorkspaceAvailability::Available);
        second.unregisterService(QStringLiteral("org.kde.KWin"));
        second.unregisterObject(QStringLiteral("/VirtualDesktopManager"));
        QDBusConnection::disconnectFromBus(QStringLiteral("kwin-owner-2"));
    }

    void metadataOnlyNameChangeNotifiesWithoutNewIdentity()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        FakeDesktopManager fake(bus);
        QVERIFY(registerFake(bus, &fake));
        QDBusConnection client = makeClientBus();
        WorkspaceReceiver receiver(client);
        receiver.setTimingForTest(200, {20, 40});
        QVERIFY(receiver.start());
        QTRY_COMPARE(receiver.state().availability, WorkspaceAvailability::Available);
        const quint64 notifications = receiver.notificationCount();
        fake.setName(0, QStringLiteral("Renamed"));
        fake.emitDesktopDataChanged();
        QTRY_COMPARE(receiver.state().desktops.at(0).displayName, QStringLiteral("Renamed"));
        QVERIFY(receiver.notificationCount() > notifications);
        QCOMPARE(receiver.state().currentOrdinal, 1);
        bus.unregisterService(QStringLiteral("org.kde.KWin"));
        bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));
    }

    void oneInitialGetAllWithoutSignalRace()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        FakeDesktopManager fake(bus);
        QVERIFY(registerFake(bus, &fake));
        QDBusConnection client = makeClientBus();
        WorkspaceReceiver receiver(client);
        receiver.setTimingForTest(200, {20, 40});
        QVERIFY(receiver.start());
        QCOMPARE(receiver.logicalRequestCount(), quint64(1));
        QCOMPARE(receiver.activeLogicalRequestId(), quint64(1));
        QTRY_COMPARE(receiver.state().availability, WorkspaceAvailability::Available);
        QCOMPARE(fake.getAllCount(), 1);
        QCOMPARE(receiver.logicalRequestCount(), quint64(1));
        QCOMPARE(receiver.activeLogicalRequestId(), quint64(0));
        bus.unregisterService(QStringLiteral("org.kde.KWin"));
        bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));
    }

    void invalidationDuringInitialRequestCoalescesOneSuccessor()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        FakeDesktopManager fake(bus);
        fake.setDelayMs(80);
        QVERIFY(registerFake(bus, &fake));
        QDBusConnection client = makeClientBus();
        WorkspaceReceiver receiver(client);
        receiver.setTimingForTest(400, {20, 40});
        QVERIFY(receiver.start());
        QTRY_COMPARE(fake.getAllCount(), 1);
        fake.setCurrent(QStringLiteral("two"));
        fake.emitCurrentChanged();
        fake.emitCountChanged();
        fake.emitDesktopDataChanged();
        QTRY_COMPARE(receiver.state().currentOrdinal, 2);
        QCOMPARE(receiver.logicalRequestCount(), quint64(2));
        QCOMPARE(fake.getAllCount(), 2);
        bus.unregisterService(QStringLiteral("org.kde.KWin"));
        bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));
    }

    void lateFirstWatcherAfterDeadlineDoesNotAdmitThirdRequest()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        FakeDesktopManager fake(bus);
        fake.setHoldReplies(true);
        QVERIFY(registerFake(bus, &fake));
        QDBusConnection client = makeClientBus();
        WorkspaceReceiver receiver(client);
        receiver.setTimingForTest(400, {50, 80});
        QVERIFY(receiver.start());
        QTRY_COMPARE(fake.getAllCount(), 1);
        QCOMPARE(receiver.logicalRequestCount(), quint64(1));
        QTRY_COMPARE(receiver.errorClass(), QStringLiteral("refresh-deadline"));
        QCOMPARE(receiver.state().availability, WorkspaceAvailability::Unknown);
        QCOMPARE(receiver.activeLogicalRequestId(), quint64(0));
        QTRY_COMPARE(receiver.logicalRequestCount(), quint64(2));
        const quint64 newerId = receiver.activeLogicalRequestId();
        QVERIFY(newerId != 0);
        QTRY_COMPARE(fake.getAllCount(), 2);
        QVERIFY(fake.outstandingHandlers() >= 1);
        const quint64 rejectedBefore = receiver.rejectedReplyCount();
        fake.releaseOldestHeld();
        QTRY_VERIFY(receiver.rejectedReplyCount() > rejectedBefore);
        QCOMPARE(receiver.activeLogicalRequestId(), newerId);
        QCOMPARE(receiver.logicalRequestCount(), quint64(2));
        QCOMPARE(fake.getAllCount(), 2);
        fake.releaseOldestHeld();
        QTRY_COMPARE(receiver.state().availability, WorkspaceAvailability::Available);
        QCOMPARE(receiver.logicalRequestCount(), quint64(2));
        QCOMPARE(receiver.activeLogicalRequestId(), quint64(0));
        bus.unregisterService(QStringLiteral("org.kde.KWin"));
        bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));
    }

    void formerOwnerReplyDoesNotClearReplacementRequest()
    {
        QDBusConnection first = QDBusConnection::sessionBus();
        auto *fake = new FakeDesktopManager(first);
        fake->setHoldReplies(true);
        QVERIFY(registerFake(first, fake));
        QDBusConnection client = makeClientBus();
        WorkspaceReceiver receiver(client);
        receiver.setTimingForTest(400, {20, 40});
        QVERIFY(receiver.start());
        QCOMPARE(receiver.logicalRequestCount(), quint64(1));
        QVERIFY(first.unregisterService(QStringLiteral("org.kde.KWin")));
        first.unregisterObject(QStringLiteral("/VirtualDesktopManager"));
        QTRY_COMPARE(receiver.errorClass(), QStringLiteral("service-lost"));
        QCOMPARE(receiver.state().availability, WorkspaceAvailability::Unknown);

        QDBusConnection second =
            QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("kwin-owner-late"));
        QVERIFY(second.isConnected());
        auto *fake2 = new FakeDesktopManager(second);
        fake2->setHoldReplies(true);
        QVERIFY(second.registerVirtualObject(QStringLiteral("/VirtualDesktopManager"), fake2,
                                             QDBusConnection::SingleNode));
        QVERIFY(second.registerService(QStringLiteral("org.kde.KWin")));
        QTRY_COMPARE(fake2->getAllCount(), 1);
        QTRY_COMPARE(receiver.logicalRequestCount(), quint64(2));
        const quint64 replacementId = receiver.activeLogicalRequestId();
        QVERIFY(replacementId != 0);

        fake->releaseHeld();
        QCoreApplication::processEvents();
        QCOMPARE(receiver.activeLogicalRequestId(), replacementId);
        QCOMPARE(receiver.logicalRequestCount(), quint64(2));
        QCOMPARE(receiver.state().availability, WorkspaceAvailability::Unknown);

        fake2->releaseHeld();
        QTRY_COMPARE(receiver.state().availability, WorkspaceAvailability::Available);
        QCOMPARE(receiver.logicalRequestCount(), quint64(2));
        QCOMPARE(receiver.activeLogicalRequestId(), quint64(0));
        second.unregisterService(QStringLiteral("org.kde.KWin"));
        second.unregisterObject(QStringLiteral("/VirtualDesktopManager"));
        QDBusConnection::disconnectFromBus(QStringLiteral("kwin-owner-late"));
    }

    void preStopWatcherCannotAlterResumedOwnership()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        FakeDesktopManager fake(bus);
        fake.setHoldReplies(true);
        QVERIFY(registerFake(bus, &fake));
        QDBusConnection client = makeClientBus();
        WorkspaceReceiver receiver(client);
        receiver.setTimingForTest(400, {20, 40});
        QVERIFY(receiver.start());
        QCOMPARE(receiver.logicalRequestCount(), quint64(1));
        const quint64 rejectedBeforePause = receiver.rejectedReplyCount();
        receiver.setPaused(true);
        QCOMPARE(receiver.state().availability, WorkspaceAvailability::Unknown);
        QCOMPARE(receiver.errorClass(), QStringLiteral("observation-paused"));
        QCOMPARE(receiver.activeLogicalRequestId(), quint64(0));
        fake.releaseHeld();
        QCoreApplication::processEvents();
        QTRY_VERIFY(receiver.rejectedReplyCount() >= rejectedBeforePause);
        QCOMPARE(receiver.state().availability, WorkspaceAvailability::Unknown);
        QCOMPARE(receiver.errorClass(), QStringLiteral("observation-paused"));
        QCOMPARE(receiver.activeLogicalRequestId(), quint64(0));
        QCOMPARE(receiver.logicalRequestCount(), quint64(1));

        fake.setHoldReplies(false);
        receiver.setPaused(false);
        QTRY_COMPARE(receiver.state().availability, WorkspaceAvailability::Available);
        QCOMPARE(receiver.logicalRequestCount(), quint64(2));
        QCOMPARE(receiver.activeLogicalRequestId(), quint64(0));
        bus.unregisterService(QStringLiteral("org.kde.KWin"));
        bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));
    }

    void productionRetryBudgetExhaustionThenEventRecovery()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        FakeDesktopManager fake(bus);
        fake.setDelayMs(120000);
        QVERIFY(registerFake(bus, &fake));
        QDBusConnection client = makeClientBus();
        WorkspaceReceiver receiver(client);
        const QVector<int> delays{1000, 2000, 4000, 8000, 16000, 30000};
        receiver.setTimingForTest(50, delays);
        QVERIFY(receiver.start());
        QTRY_COMPARE_WITH_TIMEOUT(receiver.errorClass(), QStringLiteral("refresh-deadline"), 400);
        QCOMPARE(receiver.state().availability, WorkspaceAvailability::Unknown);
        int expectedGets = 1;
        for (int i = 0; i < delays.size(); ++i) {
            ++expectedGets;
            QTRY_COMPARE_WITH_TIMEOUT(fake.getAllCount(), expectedGets, delays.at(i) + 800);
            QVERIFY(receiver.recoveryAttempt() >= i + 1);
            QCOMPARE(receiver.logicalRequestCount(), quint64(expectedGets));
            QTRY_COMPARE_WITH_TIMEOUT(receiver.activeLogicalRequestId(), quint64(0), 400);
            QCOMPARE(receiver.errorClass(), QStringLiteral("refresh-deadline"));
            QCOMPARE(receiver.state().availability, WorkspaceAvailability::Unknown);
        }
        QCOMPARE(fake.getAllCount(), 7);
        QCOMPARE(receiver.recoveryAttempt(), 6);
        QCOMPARE(receiver.logicalRequestCount(), quint64(7));
        QTest::qWait(400);
        QCOMPARE(fake.getAllCount(), 7);
        QCOMPARE(receiver.recoveryAttempt(), 6);

        fake.setDelayMs(0);
        fake.emitCurrentChanged();
        QTRY_COMPARE(receiver.state().availability, WorkspaceAvailability::Available);
        QCOMPARE(receiver.recoveryAttempt(), 0);
        QVERIFY(receiver.logicalRequestCount() >= quint64(8));
        bus.unregisterService(QStringLiteral("org.kde.KWin"));
        bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));
    }

    void successfulRecoveryResetsBudget()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        FakeDesktopManager fake(bus);
        fake.setDelayMs(120);
        QVERIFY(registerFake(bus, &fake));
        QDBusConnection client = makeClientBus();
        WorkspaceReceiver receiver(client);
        receiver.setTimingForTest(30, {20, 40, 80});
        QVERIFY(receiver.start());
        QTRY_COMPARE(receiver.errorClass(), QStringLiteral("refresh-deadline"));
        QTRY_VERIFY(receiver.recoveryAttempt() >= 1);
        fake.setDelayMs(0);
        QTRY_COMPARE(receiver.state().availability, WorkspaceAvailability::Available);
        QCOMPARE(receiver.recoveryAttempt(), 0);
        bus.unregisterService(QStringLiteral("org.kde.KWin"));
        bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));
    }

    void duplicateValidSnapshotsDoNotNotify()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        FakeDesktopManager fake(bus);
        QVERIFY(registerFake(bus, &fake));
        QDBusConnection client = makeClientBus();
        WorkspaceReceiver receiver(client);
        receiver.setTimingForTest(200, {20, 40});
        QVERIFY(receiver.start());
        QTRY_COMPARE(receiver.state().availability, WorkspaceAvailability::Available);
        const quint64 notifications = receiver.notificationCount();
        const quint64 snapshots = receiver.snapshotCount();
        fake.emitDesktopDataChanged();
        QTRY_VERIFY(receiver.snapshotCount() > snapshots);
        QCOMPARE(receiver.notificationCount(), notifications);
        QCOMPARE(receiver.state().availability, WorkspaceAvailability::Available);
        bus.unregisterService(QStringLiteral("org.kde.KWin"));
        bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));
    }

    void malformedCountMismatchAndPositions()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        FakeDesktopManager mismatch(bus);
        mismatch.setCountOverride(1);
        QVERIFY(registerFake(bus, &mismatch));
        QDBusConnection client = makeClientBus();
        WorkspaceReceiver receiver(client);
        receiver.setTimingForTest(200, {20, 40});
        QVERIFY(receiver.start());
        QTRY_COMPARE(receiver.errorClass(), QStringLiteral("count-mismatch"));
        QCOMPARE(receiver.state().availability, WorkspaceAvailability::Unknown);
        receiver.stop();
        bus.unregisterService(QStringLiteral("org.kde.KWin"));
        bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));

        FakeDesktopManager negative(bus);
        negative.resetDesktops();
        negative.addDesktop(-1, QStringLiteral("one"), QStringLiteral("One"));
        QVERIFY(registerFake(bus, &negative));
        QDBusConnection clientNegative = makeClientBus();
        WorkspaceReceiver receiverNegative(clientNegative);
        receiverNegative.setTimingForTest(200, {20, 40});
        QVERIFY(receiverNegative.start());
        QTRY_COMPARE(receiverNegative.errorClass(), QStringLiteral("position-invalid"));
        receiverNegative.stop();
        bus.unregisterService(QStringLiteral("org.kde.KWin"));
        bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));

        FakeDesktopManager signedHigh(bus);
        signedHigh.resetDesktops();
        signedHigh.addDesktop(33, QStringLiteral("one"), QStringLiteral("One"));
        QVERIFY(registerFake(bus, &signedHigh));
        QDBusConnection clientSignedHigh = makeClientBus();
        WorkspaceReceiver receiverSignedHigh(clientSignedHigh);
        receiverSignedHigh.setTimingForTest(200, {20, 40});
        QVERIFY(receiverSignedHigh.start());
        QTRY_COMPARE(receiverSignedHigh.errorClass(), QStringLiteral("position-invalid"));
        receiverSignedHigh.stop();
        bus.unregisterService(QStringLiteral("org.kde.KWin"));
        bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));

        FakeDesktopManager unsignedHigh(bus);
        unsignedHigh.setUnsignedPositions(true);
        unsignedHigh.resetDesktops();
        unsignedHigh.addDesktop(33, QStringLiteral("one"), QStringLiteral("One"));
        QVERIFY(registerFake(bus, &unsignedHigh));
        QDBusConnection clientUnsignedHigh = makeClientBus();
        WorkspaceReceiver receiverUnsignedHigh(clientUnsignedHigh);
        receiverUnsignedHigh.setTimingForTest(200, {20, 40});
        QVERIFY(receiverUnsignedHigh.start());
        QTRY_COMPARE(receiverUnsignedHigh.errorClass(), QStringLiteral("position-invalid"));
        receiverUnsignedHigh.stop();
        bus.unregisterService(QStringLiteral("org.kde.KWin"));
        bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));

        FakeDesktopManager duplicatePosition(bus);
        duplicatePosition.resetDesktops();
        duplicatePosition.addDesktop(0, QStringLiteral("one"), QStringLiteral("One"));
        duplicatePosition.addDesktop(0, QStringLiteral("two"), QStringLiteral("Two"));
        QVERIFY(registerFake(bus, &duplicatePosition));
        QDBusConnection clientDupPos = makeClientBus();
        WorkspaceReceiver receiverDupPos(clientDupPos);
        receiverDupPos.setTimingForTest(200, {20, 40});
        QVERIFY(receiverDupPos.start());
        QTRY_COMPARE(receiverDupPos.errorClass(), QStringLiteral("position-duplicate"));
        receiverDupPos.stop();
        bus.unregisterService(QStringLiteral("org.kde.KWin"));
        bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));
    }

    void malformedIdentityMetadataAndCurrent()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        QDBusConnection client = makeClientBus();

        FakeDesktopManager duplicateId(bus);
        duplicateId.resetDesktops();
        duplicateId.addDesktop(0, QStringLiteral("one"), QStringLiteral("One"));
        duplicateId.addDesktop(1, QStringLiteral("one"), QStringLiteral("Two"));
        QVERIFY(registerFake(bus, &duplicateId));
        WorkspaceReceiver receiverDupId(client);
        receiverDupId.setTimingForTest(200, {20, 40});
        QVERIFY(receiverDupId.start());
        QTRY_COMPARE(receiverDupId.errorClass(), QStringLiteral("desktop-id-invalid"));
        receiverDupId.stop();
        bus.unregisterService(QStringLiteral("org.kde.KWin"));
        bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));

        FakeDesktopManager longId(bus);
        longId.resetDesktops();
        longId.addDesktop(0, QString(129, QLatin1Char('a')), QStringLiteral("One"));
        QVERIFY(registerFake(bus, &longId));
        QDBusConnection clientLongId = makeClientBus();
        WorkspaceReceiver receiverLongId(clientLongId);
        receiverLongId.setTimingForTest(200, {20, 40});
        QVERIFY(receiverLongId.start());
        QTRY_COMPARE(receiverLongId.errorClass(), QStringLiteral("desktop-id-invalid"));
        receiverLongId.stop();
        bus.unregisterService(QStringLiteral("org.kde.KWin"));
        bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));

        FakeDesktopManager longName(bus);
        longName.resetDesktops();
        longName.addDesktop(0, QStringLiteral("one"), QString(257, QLatin1Char('n')));
        QVERIFY(registerFake(bus, &longName));
        QDBusConnection clientLongName = makeClientBus();
        WorkspaceReceiver receiverLongName(clientLongName);
        receiverLongName.setTimingForTest(200, {20, 40});
        QVERIFY(receiverLongName.start());
        QTRY_COMPARE(receiverLongName.errorClass(), QStringLiteral("desktop-name-invalid"));
        receiverLongName.stop();
        bus.unregisterService(QStringLiteral("org.kde.KWin"));
        bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));

        FakeDesktopManager control(bus);
        control.resetDesktops();
        control.addDesktop(0, QStringLiteral("one"), QStringLiteral("One") + QChar(1));
        QVERIFY(registerFake(bus, &control));
        QDBusConnection clientControl = makeClientBus();
        WorkspaceReceiver receiverControl(clientControl);
        receiverControl.setTimingForTest(200, {20, 40});
        QVERIFY(receiverControl.start());
        QTRY_COMPARE(receiverControl.errorClass(), QStringLiteral("desktop-name-invalid"));
        receiverControl.stop();
        bus.unregisterService(QStringLiteral("org.kde.KWin"));
        bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));

        FakeDesktopManager maxSize(bus);
        maxSize.resetDesktops();
        const QString maxId = QString(128, QLatin1Char('a'));
        maxSize.addDesktop(0, maxId, QString(256, QLatin1Char('n')));
        maxSize.setCurrent(maxId);
        QVERIFY(registerFake(bus, &maxSize));
        QDBusConnection clientMaxSize = makeClientBus();
        WorkspaceReceiver receiverMaxSize(clientMaxSize);
        receiverMaxSize.setTimingForTest(200, {20, 40});
        QVERIFY(receiverMaxSize.start());
        QTRY_COMPARE(receiverMaxSize.state().availability, WorkspaceAvailability::Available);
        QCOMPARE(receiverMaxSize.state().desktops.at(0).id.size(), 128);
        receiverMaxSize.stop();
        bus.unregisterService(QStringLiteral("org.kde.KWin"));
        bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));

        FakeDesktopManager metadata(bus);
        metadata.resetDesktops();
        QString firstId;
        for (int i = 0; i < 43; ++i) {
            const QString id = QString::number(i).rightJustified(128, QLatin1Char('x'));
            if (i == 0) {
                firstId = id;
            }
            metadata.addDesktop(0, id, QString(256, QLatin1Char('n')));
        }
        metadata.setCurrent(firstId);
        metadata.setCountOverride(32);
        QCOMPARE(metadata.desktopCount(), 43);
        QVERIFY(registerFake(bus, &metadata));
        QDBusConnection clientMetadata = makeClientBus();
        WorkspaceReceiver receiverMetadata(clientMetadata);
        receiverMetadata.setTimingForTest(200, {20, 40});
        QVERIFY(receiverMetadata.start());
        QTRY_COMPARE(receiverMetadata.errorClass(), QStringLiteral("metadata-limit"));
        receiverMetadata.stop();
        bus.unregisterService(QStringLiteral("org.kde.KWin"));
        bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));

        FakeDesktopManager emptyCurrent(bus);
        emptyCurrent.setEmptyCurrent(true);
        QVERIFY(registerFake(bus, &emptyCurrent));
        QDBusConnection clientEmptyCurrent = makeClientBus();
        WorkspaceReceiver receiverEmptyCurrent(clientEmptyCurrent);
        receiverEmptyCurrent.setTimingForTest(200, {20, 40});
        QVERIFY(receiverEmptyCurrent.start());
        QTRY_COMPARE(receiverEmptyCurrent.errorClass(), QStringLiteral("current-invalid"));
        receiverEmptyCurrent.stop();
        bus.unregisterService(QStringLiteral("org.kde.KWin"));
        bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));
    }

    void desktopRemovalProducesRefreshedSnapshot()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        FakeDesktopManager fake(bus);
        QVERIFY(registerFake(bus, &fake));
        QDBusConnection client = makeClientBus();
        WorkspaceReceiver receiver(client);
        receiver.setTimingForTest(200, {20, 40});
        QVERIFY(receiver.start());
        QTRY_COMPARE(receiver.state().desktops.size(), 2);
        fake.removeDesktop(QStringLiteral("two"));
        fake.emitDesktopRemoved();
        QTRY_COMPARE(receiver.state().desktops.size(), 1);
        QCOMPARE(receiver.state().currentOrdinal, 1);
        QCOMPARE(receiver.state().currentId, QStringLiteral("one"));
        QCOMPARE(receiver.state().availability, WorkspaceAvailability::Available);
        bus.unregisterService(QStringLiteral("org.kde.KWin"));
        bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));
    }

    void rowsAndWrappingAreDecoded()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        FakeDesktopManager fake(bus);
        fake.setRows(2);
        fake.setWrapping(true);
        QVERIFY(registerFake(bus, &fake));
        QDBusConnection client = makeClientBus();
        WorkspaceReceiver receiver(client);
        receiver.setTimingForTest(200, {20, 40});
        QVERIFY(receiver.start());
        QTRY_COMPARE(receiver.state().availability, WorkspaceAvailability::Available);
        QCOMPARE(receiver.state().rows.value(), 2);
        QCOMPARE(receiver.state().navigationWrappingAround.value(), true);
        bus.unregisterService(QStringLiteral("org.kde.KWin"));
        bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));
    }

    void rowsChangedRequestsFreshSnapshot()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        FakeDesktopManager fake(bus);
        QVERIFY(registerFake(bus, &fake));
        QDBusConnection client = makeClientBus();
        WorkspaceReceiver receiver(client);
        receiver.setTimingForTest(200, {20, 40});
        QVERIFY(receiver.start());
        QTRY_COMPARE(receiver.state().availability, WorkspaceAvailability::Available);
        const quint64 snapshots = receiver.snapshotCount();
        fake.setRows(3);
        fake.emitRowsChanged();
        QTRY_COMPARE(receiver.state().rows.value(), 3);
        QVERIFY(receiver.snapshotCount() > snapshots);
        QCOMPARE(receiver.invalidationCount() >= 1, true);
        QCOMPARE(receiver.state().availability, WorkspaceAvailability::Available);
        bus.unregisterService(QStringLiteral("org.kde.KWin"));
        bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));
    }

    void navigationWrappingChangedRequestsFreshSnapshot()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        FakeDesktopManager fake(bus);
        QVERIFY(registerFake(bus, &fake));
        QDBusConnection client = makeClientBus();
        WorkspaceReceiver receiver(client);
        receiver.setTimingForTest(200, {20, 40});
        QVERIFY(receiver.start());
        QTRY_COMPARE(receiver.state().availability, WorkspaceAvailability::Available);
        const quint64 snapshots = receiver.snapshotCount();
        fake.setWrapping(true);
        fake.emitNavigationWrappingChanged();
        QTRY_COMPARE(receiver.state().navigationWrappingAround.value(), true);
        QVERIFY(receiver.snapshotCount() > snapshots);
        QCOMPARE(receiver.invalidationCount() >= 1, true);
        bus.unregisterService(QStringLiteral("org.kde.KWin"));
        bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));
    }

    void malformedWrappingBecomesUnknown()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        FakeDesktopManager fake(bus);
        fake.setMalformedWrapping(true);
        QVERIFY(registerFake(bus, &fake));
        QDBusConnection client = makeClientBus();
        WorkspaceReceiver receiver(client);
        receiver.setTimingForTest(200, {20, 40});
        QVERIFY(receiver.start());
        QTRY_COMPARE(receiver.errorClass(), QStringLiteral("wrapping-invalid"));
        QCOMPARE(receiver.state().availability, WorkspaceAvailability::Unknown);
        bus.unregisterService(QStringLiteral("org.kde.KWin"));
        bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));
    }

    void unknownSnapshotKeysAreIgnored()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        FakeDesktopManager fake(bus);
        fake.setExtraKey(true);
        QVERIFY(registerFake(bus, &fake));
        QDBusConnection client = makeClientBus();
        WorkspaceReceiver receiver(client);
        receiver.setTimingForTest(200, {20, 40});
        QVERIFY(receiver.start());
        QTRY_COMPARE(receiver.state().availability, WorkspaceAvailability::Available);
        QCOMPARE(receiver.state().desktops.size(), 2);
        QCOMPARE(receiver.errorClass(), QString());
        bus.unregisterService(QStringLiteral("org.kde.KWin"));
        bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));
    }
};

QTEST_GUILESS_MAIN(TestWorkspaceReceiver)
#include "test_workspace_receiver.moc"
