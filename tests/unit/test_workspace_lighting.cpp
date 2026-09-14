#include "app/AppController.h"
#include "context/ContextReceiver.h"
#include "context/WorkspaceReceiver.h"
#include "core/Resolver.h"
#include "rgb/OpenRgbClient.h"
#include "rgb/OpenRgbProtocol.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QCoreApplication>
#include <QMetaObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUuid>
#include <QVariantMap>

using namespace contextdeck;
using namespace contextdeck::openrgb;

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

Lighting defaultWorkspaceLayout()
{
    Lighting lighting;
    lighting.mode = LightingMode::Direct;
    lighting.baseColor = kDefaultEffectColor;
    std::array<ZoneValue, kZoneCount> zones{};
    for (int i = 0; i < 4; ++i) {
        zones[static_cast<size_t>(i)].role = ZoneRole::DesktopIndicator;
        zones[static_cast<size_t>(i)].color = Rgb{0x50, 0x00, 0x00};
    }
    zones[4].role = ZoneRole::AppColor;
    zones[4].color = Rgb{0x11, 0x22, 0x33};
    lighting.zones = zones;
    return lighting;
}

WorkspaceState twoDesktops(int current)
{
    WorkspaceState state;
    state.availability = WorkspaceAvailability::Available;
    for (int i = 0; i < 2; ++i) {
        WorkspaceDesktop desktop;
        desktop.position = i;
        desktop.ordinal = i + 1;
        desktop.id = QStringLiteral("id-%1").arg(i + 1);
        desktop.displayName = QStringLiteral("Plocha %1").arg(i + 1);
        state.desktops.push_back(desktop);
    }
    state.currentOrdinal = current;
    state.currentId = QStringLiteral("id-%1").arg(current);
    return state;
}

void collectDiagnosticStrings(const QVariant &value, QStringList &out)
{
    switch (value.typeId()) {
    case QMetaType::QVariantMap: {
        const QVariantMap map = value.toMap();
        for (auto it = map.cbegin(); it != map.cend(); ++it) {
            out.push_back(it.key());
            collectDiagnosticStrings(it.value(), out);
        }
        return;
    }
    case QMetaType::QVariantList: {
        const QVariantList list = value.toList();
        for (const QVariant &entry : list) {
            collectDiagnosticStrings(entry, out);
        }
        return;
    }
    default:
        out.push_back(value.toString());
        return;
    }
}

class FakeDesktopManager : public QDBusVirtualObject
{
public:
    explicit FakeDesktopManager(QDBusConnection connection, QObject *parent = nullptr)
        : QDBusVirtualObject(parent)
        , m_connection(std::move(connection))
    {
        qDBusRegisterMetaType<DesktopTuple>();
        registerWorkspaceDesktopDBusTypes();
        m_desktops.push_back(DesktopTuple{0, QStringLiteral("one"), QStringLiteral("One")});
        m_desktops.push_back(DesktopTuple{1, QStringLiteral("two"), QStringLiteral("Two")});
        m_current = QStringLiteral("one");
    }

    QString introspect(const QString &) const override { return QStringLiteral("<node/>"); }

    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override
    {
        if (message.interface() == QLatin1String("org.freedesktop.DBus.Properties")
            && message.member() == QLatin1String("GetAll")) {
            if (m_delayMs > 0) {
                QTimer::singleShot(m_delayMs, this, [this, message, connection]() {
                    sendAll(message, connection);
                });
                return true;
            }
            return sendAll(message, connection);
        }
        return false;
    }

    void setDelayMs(int delayMs) { m_delayMs = delayMs; }
    void setCurrent(const QString &id) { m_current = id; }
    void setName(int index, const QString &name) { m_desktops[index].name = name; }

    void emitCurrentChanged()
    {
        QDBusMessage signal = QDBusMessage::createSignal(QStringLiteral("/VirtualDesktopManager"),
                                                         QStringLiteral("org.kde.KWin.VirtualDesktopManager"),
                                                         QStringLiteral("currentChanged"));
        signal << m_current;
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
        map.insert(QStringLiteral("count"), QVariant::fromValue(static_cast<uint>(m_desktops.size())));
        map.insert(QStringLiteral("current"), m_current);
        map.insert(QStringLiteral("rows"), QVariant::fromValue(uint(1)));
        QList<VirtualDesktopDBus> rows;
        for (const DesktopTuple &tuple : m_desktops) {
            VirtualDesktopDBus row;
            row.position = tuple.position;
            row.id = tuple.id;
            row.name = tuple.name;
            rows.push_back(row);
        }
        map.insert(QStringLiteral("desktops"), QVariant::fromValue(rows));
        return connection.send(message.createReply(QVariantList{QVariant::fromValue(map)}));
    }

private:
    QDBusConnection m_connection;
    QVector<DesktopTuple> m_desktops;
    QString m_current;
    int m_delayMs = 0;
};

int g_lightingClientIndex = 0;

QDBusConnection makeLightingClientBus()
{
    return QDBusConnection::connectToBus(QDBusConnection::SessionBus,
                                         QStringLiteral("contextdeck-ws-light-%1-%2")
                                             .arg(++g_lightingClientIndex)
                                             .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
}

bool registerLightingFake(QDBusConnection &bus, FakeDesktopManager *fake)
{
    bus.unregisterService(QStringLiteral("org.kde.KWin"));
    bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));
    if (!bus.registerService(QStringLiteral("org.kde.KWin"))) {
        return false;
    }
    return bus.registerVirtualObject(QStringLiteral("/VirtualDesktopManager"), fake, QDBusConnection::SingleNode);
}

} // namespace

class TestWorkspaceLighting : public QObject
{
    Q_OBJECT

private slots:
    void syntheticContextEncodesFiveExactColors()
    {
        ProfileDocument document;
        document.globalLighting = defaultWorkspaceLayout();
        ApplicationProfile profile;
        profile.id = QStringLiteral("app");
        profile.displayName = QStringLiteral("App");
        profile.match.resourceClass = QStringLiteral("Foo");
        Lighting appLighting;
        appLighting.mode = LightingMode::Direct;
        appLighting.baseColor = Rgb{0xaa, 0xbb, 0xcc};
        profile.lighting = appLighting;
        document.applications.push_back(profile);

        ApplicationIdentity identity;
        identity.resourceClass = QStringLiteral("Foo");
        const LightingResolution resolution = resolveContextLighting(document, identity, twoDesktops(1));
        QCOMPARE(resolution.desired.mode, LightingMode::Direct);
        QCOMPARE(resolution.desired.colors[0].r, quint8(0x50));
        QCOMPARE(resolution.desired.colors[1].r, quint8(0x50 / 5));
        QCOMPARE(resolution.desired.colors[4].r, quint8(0xaa));

        const QByteArray packet = encodeUpdateLeds(0, resolution.desired.colors);
        DecodeError error;
        const auto header = decodeHeader(packet, &error);
        QVERIFY2(header.has_value(), qPrintable(error.reason));
        QCOMPARE(header->packetId, PacketId::UpdateLeds);
        const auto decoded = decodeUpdateLedsPayload(packet.mid(kHeaderSize), &error);
        QVERIFY(decoded.has_value());
        QCOMPARE(decoded->at(0).r, quint8(0x50));
        QCOMPARE(decoded->at(4).b, quint8(0xcc));

        QVector<ControllerMode> modes;
        ControllerMode direct;
        direct.name = QStringLiteral("Direct");
        modes.push_back(direct);
        const auto frames = encodeDesiredStateFrames(0, resolution.desired, modes, kProtocolVersion, &error);
        QVERIFY2(frames.has_value(), qPrintable(error.reason));
        QCOMPARE(frames->size(), 2);
        QCOMPARE(decodeHeader(frames->at(0), &error)->packetId, PacketId::UpdateMode);
        QCOMPARE(decodeHeader(frames->at(1), &error)->packetId, PacketId::UpdateLeds);
    }

    void controllerSuppressesUnchangedOutputAndHonorsOverrideDuringRefresh()
    {
        QVERIFY(QDBusConnection::sessionBus().isConnected());
        QDBusConnection bus = QDBusConnection::sessionBus();
        FakeDesktopManager fake(bus);
        QVERIFY(registerLightingFake(bus, &fake));

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ContextReceiver context;
        OpenRgbClient rgb;
        PowerActions power;
        AppController controller(&context, &rgb, &power, nullptr, dir.path());
        QDBusConnection client = makeLightingClientBus();
        WorkspaceReceiver workspace(client);
        workspace.setTimingForTest(80, {20, 40});
        controller.setWorkspaceReceiver(&workspace);
        QVERIFY(workspace.start());
        QTRY_COMPARE(workspace.state().availability, WorkspaceAvailability::Available);

        controller.useDefaultWorkspaceLayout();
        QCoreApplication::processEvents();
        QVERIFY(controller.workspaceLayoutActive());
        const quint64 firstUpdates = controller.diagnostics().value(QStringLiteral("lightingUpdates")).toULongLong();
        controller.useDefaultWorkspaceLayout();
        QCoreApplication::processEvents();
        QCOMPARE(controller.diagnostics().value(QStringLiteral("lightingUpdates")).toULongLong(), firstUpdates);

        fake.setDelayMs(40);
        fake.emitCurrentChanged();
        controller.lightsOff();
        QCOMPARE(controller.sessionLighting(), QStringLiteral("off"));
        QCOMPARE(rgb.desiredState().mode, LightingMode::Off);

        bus.unregisterService(QStringLiteral("org.kde.KWin"));
        bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));
    }

    void controllerCoalescesBridgeLossPair()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ContextReceiver context;
        OpenRgbClient rgb;
        PowerActions power;
        AppController controller(&context, &rgb, &power, nullptr, dir.path());
        controller.load();
        const quint64 before = controller.diagnostics().value(QStringLiteral("identityUpdates")).toULongLong();
        const int identitySignal = context.metaObject()->indexOfSignal("currentIdentityChanged()");
        const int lostSignal = context.metaObject()->indexOfSignal("bridgeLost()");
        QVERIFY(identitySignal >= 0);
        QVERIFY(lostSignal >= 0);
        QMetaObject::activate(&context, identitySignal, nullptr);
        QMetaObject::activate(&context, lostSignal, nullptr);
        QCoreApplication::processEvents();
        QCOMPARE(controller.diagnostics().value(QStringLiteral("identityUpdates")).toULongLong(), before + 1);
    }

    void simultaneousDesktopAndApplicationInputsUseLatest()
    {
        QVERIFY(QDBusConnection::sessionBus().isConnected());
        QDBusConnection bus = QDBusConnection::sessionBus();
        FakeDesktopManager fake(bus);
        QVERIFY(registerLightingFake(bus, &fake));

        QTemporaryDir dir;
        ContextReceiver context;
        OpenRgbClient rgb;
        PowerActions power;
        AppController controller(&context, &rgb, &power, nullptr, dir.path());
        QDBusConnection client = makeLightingClientBus();
        WorkspaceReceiver workspace(client);
        workspace.setTimingForTest(80, {20, 40});
        controller.setWorkspaceReceiver(&workspace);
        QVERIFY(workspace.start());
        QTRY_COMPARE(workspace.state().availability, WorkspaceAvailability::Available);
        controller.useDefaultWorkspaceLayout();
        fake.setCurrent(QStringLiteral("two"));
        fake.emitCurrentChanged();
        controller.setTemporaryColor(QStringLiteral("#010203"));
        QTRY_COMPARE(controller.sessionLighting(), QStringLiteral("temporary"));
        QCOMPARE(rgb.desiredState().colors[0].r, quint8(0x01));
        bus.unregisterService(QStringLiteral("org.kde.KWin"));
        bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));
    }

    void metadataOnlyNameChangeDoesNotSubmitLighting()
    {
        QVERIFY(QDBusConnection::sessionBus().isConnected());
        QDBusConnection bus = QDBusConnection::sessionBus();
        FakeDesktopManager fake(bus);
        QVERIFY(registerLightingFake(bus, &fake));

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ContextReceiver context;
        OpenRgbClient rgb;
        PowerActions power;
        AppController controller(&context, &rgb, &power, nullptr, dir.path());
        QDBusConnection client = makeLightingClientBus();
        WorkspaceReceiver workspace(client);
        workspace.setTimingForTest(200, {20, 40});
        controller.setWorkspaceReceiver(&workspace);
        QVERIFY(workspace.start());
        QTRY_COMPARE(workspace.state().availability, WorkspaceAvailability::Available);
        controller.useDefaultWorkspaceLayout();
        QCoreApplication::processEvents();
        QVERIFY(controller.workspaceLayoutActive());
        const quint64 lightingUpdates =
            controller.diagnostics().value(QStringLiteral("lightingUpdates")).toULongLong();
        QSignalSpy presentation(&controller, &AppController::presentationChanged);
        fake.setName(0, QStringLiteral("Renamed"));
        fake.emitDesktopDataChanged();
        QTRY_COMPARE(workspace.state().desktops.at(0).displayName, QStringLiteral("Renamed"));
        QTRY_VERIFY(presentation.count() >= 1);
        QCOMPARE(controller.diagnostics().value(QStringLiteral("lightingUpdates")).toULongLong(), lightingUpdates);
        QCOMPARE(workspace.state().currentOrdinal, 1);
        bus.unregisterService(QStringLiteral("org.kde.KWin"));
        bus.unregisterObject(QStringLiteral("/VirtualDesktopManager"));
    }

    void diagnosticsOmitWorkspacePrivacySentinels()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const QString sessionSentinel = QStringLiteral("contextdeck-sentinel-session-4f9c1a37");
        const QString desktopSentinel = QStringLiteral("contextdeck-sentinel-desktop-4f9c1a37");
        const QString patternSentinel = QStringLiteral("contextdeck-sentinel-title-pattern-4f9c1a37");

        ProfileStore store(dir.path());
        ProfileDocument document;
        document.globalLighting = defaultWorkspaceLayout();

        WorkspaceSession session;
        session.id = QStringLiteral("sentinel-session-4f9c1a37");
        session.displayName = sessionSentinel;
        WorkspaceDesktopEntry desktop;
        desktop.ordinal = 1;
        desktop.name = desktopSentinel;
        session.desktops.push_back(desktop);
        document.workspaceSessions.push_back(session);

        ApplicationProfile profile;
        profile.id = QStringLiteral("sentinel-application-4f9c1a37");
        profile.displayName = QStringLiteral("Sentinel Application");
        profile.match.resourceClass = QStringLiteral("SentinelResource4f9c1a37");
        WorkspaceAssignment assignment;
        assignment.sessionId = session.id;
        assignment.desktopOrdinal = 1;
        TitleFallback fallback;
        fallback.enabled = true;
        fallback.mode = TitleMatchMode::Contains;
        fallback.pattern = patternSentinel;
        assignment.titleFallback = fallback;
        profile.workspace = assignment;
        document.applications.push_back(profile);

        document.preferences.workspaceManagementEnabled = true;
        document.preferences.titleFallbackEnabled = true;
        document.preferences.activeWorkspaceSessionId = session.id;

        const SaveOutcome saved = store.save(document);
        QVERIFY2(saved.ok, qPrintable(saved.error.reason));

        ContextReceiver context;
        OpenRgbClient rgb;
        PowerActions power;
        AppController controller(&context, &rgb, &power, nullptr, dir.path());
        controller.load();

        QCOMPARE(controller.document().workspaceSessions.size(), 1);
        QCOMPARE(controller.document().workspaceSessions.at(0).displayName, sessionSentinel);
        QCOMPARE(controller.document().workspaceSessions.at(0).desktops.at(0).name, desktopSentinel);
        QCOMPARE(controller.document().applications.size(), 1);
        const std::optional<WorkspaceAssignment> &loadedAssignment =
            controller.document().applications.at(0).workspace;
        QVERIFY(loadedAssignment.has_value());
        QVERIFY(loadedAssignment->titleFallback.has_value());
        QVERIFY(loadedAssignment->titleFallback->enabled);
        QCOMPARE(loadedAssignment->titleFallback->pattern, patternSentinel);
        QVERIFY(controller.workspaceManagementEnabled());
        QVERIFY(controller.titleFallbackEnabled());

        const QVariantMap diagnostics = controller.diagnostics();
        QStringList diagnosticsStrings;
        for (auto it = diagnostics.cbegin(); it != diagnostics.cend(); ++it) {
            diagnosticsStrings.push_back(it.key());
            collectDiagnosticStrings(it.value(), diagnosticsStrings);
        }
        const QStringList sentinels{sessionSentinel, desktopSentinel, patternSentinel};
        for (const QString &sentinel : sentinels) {
            for (const QString &text : diagnosticsStrings) {
                QVERIFY2(!text.contains(sentinel),
                         qPrintable(QStringLiteral("diagnostics leaked a sensitive sentinel: %1").arg(text)));
            }
        }

        const QStringList forbiddenKeyFragments{QStringLiteral("pattern"), QStringLiteral("caption"),
                                                QStringLiteral("desktopname"), QStringLiteral("desktopuuid"),
                                                QStringLiteral("desktopid"), QStringLiteral("uuid")};
        for (auto it = diagnostics.cbegin(); it != diagnostics.cend(); ++it) {
            QString normalized = it.key().toLower();
            normalized.remove(QLatin1Char('_'));
            normalized.remove(QLatin1Char('-'));
            for (const QString &fragment : forbiddenKeyFragments) {
                QVERIFY2(!normalized.contains(fragment),
                         qPrintable(QStringLiteral("diagnostics key exposes a sensitive category: %1").arg(it.key())));
            }
        }
    }
};

QTEST_GUILESS_MAIN(TestWorkspaceLighting)
#include "test_workspace_lighting.moc"
