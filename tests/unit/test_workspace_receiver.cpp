#include "context/WorkspaceReceiver.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <QVariantMap>

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
            "<signal name=\"currentChanged\"><arg type=\"s\"/></signal>"
            "<signal name=\"countChanged\"><arg type=\"u\"/></signal>"
            "<signal name=\"desktopCreated\"><arg type=\"s\"/></signal>"
            "<signal name=\"desktopRemoved\"><arg type=\"s\"/></signal>"
            "<signal name=\"desktopDataChanged\"><arg type=\"s\"/></signal>"
            "</interface>");
    }

    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override
    {
        if (message.interface() == QLatin1String("org.freedesktop.DBus.Properties")
            && message.member() == QLatin1String("GetAll")) {
            ++m_getAllCount;
            if (m_delayMs > 0) {
                const int delay = m_delayMs;
                QTimer::singleShot(delay, this, [this, message, connection]() {
                    sendAll(message, connection);
                });
                return true;
            }
            return sendAll(message, connection);
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

    void setCurrent(const QString &id) { m_current = id; }
    void setMalformed(bool malformed) { m_malformed = malformed; }
    void setMissingCurrent(bool missing) { m_missingCurrent = missing; }
    void setUnsignedPositions(bool enabled) { m_unsigned = enabled; }
    void setDelayMs(int delayMs) { m_delayMs = delayMs; }
    void setEmptyName(bool enabled) { m_emptyName = enabled; }
    void setName(int index, const QString &name) { m_desktops[index].name = name; }
    [[nodiscard]] int getAllCount() const { return m_getAllCount; }

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

    bool sendAll(const QDBusMessage &message, const QDBusConnection &connection) const
    {
        QVariantMap map;
        if (m_malformed) {
            map.insert(QStringLiteral("count"), QVariant::fromValue(uint(2)));
            map.insert(QStringLiteral("current"), m_current);
            map.insert(QStringLiteral("desktops"), QStringLiteral("nope"));
        } else {
            map.insert(QStringLiteral("count"), QVariant::fromValue(static_cast<uint>(m_desktops.size())));
            map.insert(QStringLiteral("current"), m_missingCurrent ? QStringLiteral("missing") : m_current);
            map.insert(QStringLiteral("rows"), QVariant::fromValue(uint(1)));
            if (m_unsigned) {
                QList<VirtualDesktopDBusUnsigned> rows;
                for (const DesktopTuple &tuple : m_desktops) {
                    VirtualDesktopDBusUnsigned row;
                    row.position = static_cast<quint32>(tuple.position);
                    row.id = tuple.id;
                    row.name = m_emptyName ? QString() : tuple.name;
                    rows.push_back(row);
                }
                map.insert(QStringLiteral("desktops"), QVariant::fromValue(rows));
            } else {
                QList<VirtualDesktopDBus> rows;
                for (const DesktopTuple &tuple : m_desktops) {
                    VirtualDesktopDBus row;
                    row.position = tuple.position;
                    row.id = tuple.id;
                    row.name = m_emptyName ? QString() : tuple.name;
                    rows.push_back(row);
                }
                map.insert(QStringLiteral("desktops"), QVariant::fromValue(rows));
            }
        }
        return connection.send(message.createReply(QVariantList{QVariant::fromValue(map)}));
    }

private:
    QDBusConnection m_connection;
    QVector<DesktopTuple> m_desktops;
    QString m_current;
    bool m_malformed = false;
    bool m_missingCurrent = false;
    bool m_unsigned = false;
    bool m_emptyName = false;
    int m_delayMs = 0;
    int m_getAllCount = 0;
};

bool registerFake(QDBusConnection &bus, FakeDesktopManager *fake)
{
    bus.unregisterService(QStringLiteral("org.kde.KWin"));
    bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));
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
        fake.setDelayMs(30);
        QVERIFY(registerFake(bus, &fake));
        QDBusConnection client = makeClientBus();
        WorkspaceReceiver receiver(client);
        receiver.setTimingForTest(200, {20, 40});
        QVERIFY(receiver.start());
        fake.emitCountChanged();
        fake.emitDesktopRemoved();
        fake.emitDesktopDataChanged();
        QTRY_VERIFY(receiver.invalidationCount() >= 3);
        QTRY_COMPARE(receiver.state().availability, WorkspaceAvailability::Available);
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
};

QTEST_GUILESS_MAIN(TestWorkspaceReceiver)
#include "test_workspace_receiver.moc"
