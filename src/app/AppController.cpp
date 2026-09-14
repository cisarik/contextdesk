#include "app/AppController.h"

#include "app/BrokerIpcClient.h"
#include "context/DBusNames.h"
#include "context/WorkspaceReceiver.h"
#include "core/ControlCatalog.h"
#include "core/Resolver.h"
#include "core/ZoneMap.h"
#include "workspace/PlacementResolver.h"
#include "workspace/WorkspacePlan.h"

#include <QDateTime>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QSet>
#include <QStringList>

#include <algorithm>

namespace contextdeck {

Q_LOGGING_CATEGORY(lcUi, "contextdeck.ui")

namespace {

Lighting defaultLighting()
{
    return untouchedLighting();
}

Rgb displayColor(const Lighting &lighting)
{
    if (lighting.baseColor) {
        return *lighting.baseColor;
    }
    if (lighting.zones) {
        return (*lighting.zones)[0].color;
    }
    return Rgb{};
}

QString hexOf(const Rgb &color)
{
    return QStringLiteral("#%1%2%3")
        .arg(color.r, 2, 16, QLatin1Char('0'))
        .arg(color.g, 2, 16, QLatin1Char('0'))
        .arg(color.b, 2, 16, QLatin1Char('0'));
}

QStringList zoneHexList(const Lighting &lighting)
{
    QStringList list;
    const std::array<Rgb, kZoneCount> colors = zoneColorsFromPreset(lighting);
    for (const Rgb &color : colors) {
        list.push_back(hexOf(color));
    }
    return list;
}

void ensureDirectColors(Lighting &lighting, const Rgb &fallback)
{
    if (!lighting.baseColor && !lighting.zones) {
        lighting.baseColor = fallback;
    }
}

bool applyMode(Lighting &lighting, LightingMode mode, const Rgb &fallback)
{
    lighting.mode = mode;
    if (mode == LightingMode::Direct) {
        ensureDirectColors(lighting, fallback);
    }
    if (mode == LightingMode::Breathing && !lighting.baseColor) {
        lighting.baseColor = fallback;
    }
    return true;
}

bool applyZoneColor(Lighting &lighting, int index, const Rgb &color, const Rgb &fallback)
{
    if (index < 0 || index >= kZoneCount) {
        return false;
    }
    if (lighting.zones.has_value()) {
        ZoneValue &zone = (*lighting.zones)[static_cast<size_t>(index)];
        if (zone.role == ZoneRole::Off) {
            return false;
        }
        zone.color = color;
        lighting.baseColor = color;
        lighting.mode = LightingMode::Direct;
        return true;
    }
    std::array<Rgb, kZoneCount> colors = zoneColorsFromPreset(lighting);
    if (!lighting.baseColor) {
        colors.fill(fallback);
    }
    colors[static_cast<size_t>(index)] = color;
    lighting.zones = zoneValuesFromColors(colors);
    lighting.baseColor = color;
    lighting.mode = LightingMode::Direct;
    return true;
}

Lighting sanitizeApplicationLighting(const Lighting &source)
{
    Lighting lighting = source;
    if (lighting.zones) {
        std::array<ZoneValue, kZoneCount> zones = *lighting.zones;
        for (ZoneValue &zone : zones) {
            if (zone.role == ZoneRole::Off) {
                zone.color = {};
            } else {
                zone.role = ZoneRole::Static;
            }
        }
        lighting.zones = zones;
    }
    if (lighting.mode == LightingMode::Untouched) {
        lighting.mode = LightingMode::Direct;
        if (!lighting.baseColor && !lighting.zones) {
            lighting.baseColor = kDefaultEffectColor;
        }
    }
    return lighting;
}

Lighting applicationLightingOrSanitized(const ApplicationProfile &profile, const Lighting &global)
{
    if (profile.lighting.has_value()) {
        return *profile.lighting;
    }
    return sanitizeApplicationLighting(global);
}

QString slotContributionName(SlotContribution contribution)
{
    switch (contribution) {
    case SlotContribution::SessionOverride:
        return QStringLiteral("session_override");
    case SlotContribution::Static:
        return QStringLiteral("static");
    case SlotContribution::DesktopIndicatorCurrent:
        return QStringLiteral("desktop_current");
    case SlotContribution::DesktopIndicatorInactive:
        return QStringLiteral("desktop_inactive");
    case SlotContribution::DesktopIndicatorAbsent:
        return QStringLiteral("desktop_absent");
    case SlotContribution::AppColor:
        return QStringLiteral("app_color");
    case SlotContribution::Off:
        return QStringLiteral("off");
    case SlotContribution::DeviceDefault:
        return QStringLiteral("device_default");
    case SlotContribution::None:
        break;
    }
    return QStringLiteral("none");
}

bool applyGradient(Lighting &lighting, const Rgb &start, const Rgb &end)
{
    lighting.zones = zoneValuesFromColors(gradientColors(start, end));
    lighting.baseColor = start;
    lighting.mode = LightingMode::Direct;
    return true;
}

QString sanitizeDisplayText(const QString &text, qsizetype maxBytes, const QString &fallback)
{
    QString cleaned;
    for (const QChar ch : text) {
        if (ch.category() != QChar::Other_Control) {
            cleaned.append(ch);
        }
    }
    cleaned = cleaned.trimmed();
    while (!cleaned.isEmpty() && cleaned.toUtf8().size() > maxBytes) {
        cleaned.chop(1);
    }
    if (cleaned.isEmpty()) {
        return fallback;
    }
    return cleaned;
}

} // namespace

AppController::AppController(ContextReceiver *context, OpenRgbClient *rgb, PowerActions *power, QObject *parent,
                             QString configRoot)
    : QObject(parent)
    , m_context(context)
    , m_rgb(rgb)
    , m_power(power)
    , m_store(std::move(configRoot))
{
    m_document.globalLighting = defaultLighting();
    m_mutator.setCheckpointPath(m_store.configRoot() + QStringLiteral("/workspace-checkpoint.json"));
    connect(m_context, &ContextReceiver::currentIdentityChanged, this, &AppController::onContextInputsChanged);
    connect(m_context, &ContextReceiver::bridgeLost, this, &AppController::onContextInputsChanged);
    connect(m_context, &ContextReceiver::bridgeConnectedChanged, this, &AppController::contextChanged);
    connect(m_context, &ContextReceiver::inventoryChanged, this, &AppController::onInventoryChanged);
    connect(m_context, &ContextReceiver::degradedChanged, this, &AppController::contextChanged);
    connect(&m_mutator, &DesktopMutator::desktopCreatedInTransaction,
            this, &AppController::onDesktopCreatedInTransaction);
    m_context->setPlacementHintProvider(
        [this](const ApplicationIdentity &identity, const std::optional<QString> &caption) {
            return placementHintFor(identity, caption);
        });
    connect(m_rgb, &OpenRgbClient::connectionStateChanged, this, &AppController::diagnosticsChanged);
    connect(m_rgb, &OpenRgbClient::lastErrorChanged, this, &AppController::diagnosticsChanged);
    connect(m_rgb, &OpenRgbClient::lightingEnabledChanged, this, &AppController::diagnosticsChanged);
    connect(this, &AppController::contextChanged, this, &AppController::presentationChanged);
    connect(this, &AppController::lightingModeChanged, this, &AppController::presentationChanged);
    connect(this, &AppController::diagnosticsChanged, this, &AppController::presentationChanged);
    connect(this, &AppController::documentChanged, this, &AppController::presentationChanged);
}

void AppController::setBrokerIpc(BrokerIpcClient *client)
{
    m_brokerIpc = client;
    if (m_brokerIpc == nullptr) {
        return;
    }
    connect(m_brokerIpc, &BrokerIpcClient::stateChanged, this, &AppController::diagnosticsChanged);
    connect(m_brokerIpc, &BrokerIpcClient::stateChanged, this, &AppController::presentationChanged);
}

void AppController::setWorkspaceReceiver(WorkspaceReceiver *receiver)
{
    m_workspace = receiver;
    if (m_workspace == nullptr) {
        return;
    }
    connect(m_workspace, &WorkspaceReceiver::stateChanged, this, &AppController::scheduleRecompute);
    connect(m_workspace, &WorkspaceReceiver::diagnosticsChanged, this, &AppController::diagnosticsChanged);
    connect(m_workspace, &WorkspaceReceiver::desktopCreatedObserved, this, &AppController::onWorkspaceDesktopCreated);
}

void AppController::load()
{
    const LoadOutcome loaded = m_store.load();
    if (loaded.ok && !loaded.missing) {
        m_document = loaded.document;
        qCInfo(lcUi) << "loaded profile document";
    } else if (loaded.missing) {
        m_document = ProfileDocument{};
        m_document.globalLighting = defaultLighting();
        qCInfo(lcUi) << "cold start: no profile document, pass-through, writing nothing";
    } else {
        qCWarning(lcUi) << "profile load refused:" << loaded.error.reason << loaded.error.jsonPath
                        << "preserved=" << loaded.error.preserved;
        m_document = ProfileDocument{};
        m_document.globalLighting = defaultLighting();
    }
    if (m_document.preferences.automaticEnabled) {
        m_sessionLighting = SessionLightingMode::Automatic;
    }
    m_context->setTitleFallbackEnabled(m_document.preferences.titleFallbackEnabled);
    m_applyStatus.clear();
    m_applyResidual.clear();
    m_lastApplyReverted = false;
    m_lastApplyCreated = 0;
    m_lastApplyRemoved = 0;
    rememberExternalContext();
    refreshResolvedProfile();
    recompute();
    emit documentChanged();
    emit contextChanged();
    emit presentationChanged();
}

QString AppController::currentApplication() const
{
    const ApplicationIdentity identity = m_context->identity();
    if (!identity.isIdentified()) {
        return QStringLiteral("(unidentified)");
    }
    if (!identity.desktopFileName.isEmpty()) {
        return identity.desktopFileName;
    }
    if (!identity.resourceClass.isEmpty()) {
        return identity.resourceClass;
    }
    return identity.resourceName;
}

QString AppController::currentProfile() const
{
    if (m_resolvedProfileId.isEmpty()) {
        return QStringLiteral("global");
    }
    return m_resolvedProfileId;
}

QString AppController::lightingMode() const
{
    if (m_sessionLighting == SessionLightingMode::TemporaryColor) {
        return QStringLiteral("temporary:") + lightingModeJsonName(effectiveLighting().mode);
    }
    return lightingModeJsonName(effectiveLighting().mode);
}

QString AppController::lightingLabel() const
{
    if (m_sessionLighting == SessionLightingMode::TemporaryColor) {
        return QStringLiteral("temporary override — %1").arg(lightingModeJsonName(effectiveLighting().mode));
    }
    if (m_sessionLighting == SessionLightingMode::LightsOff) {
        return QStringLiteral("off");
    }
    if (m_sessionLighting == SessionLightingMode::DeviceDefault
        || effectiveLighting().mode == LightingMode::Untouched) {
        return QStringLiteral("untouched — device default");
    }
    return lightingModeJsonName(effectiveLighting().mode);
}

bool AppController::temporaryOverrideActive() const
{
    return m_sessionLighting == SessionLightingMode::TemporaryColor;
}

QString AppController::sessionLighting() const
{
    switch (m_sessionLighting) {
    case SessionLightingMode::Automatic:
        return QStringLiteral("automatic");
    case SessionLightingMode::TemporaryColor:
        return QStringLiteral("temporary");
    case SessionLightingMode::LightsOff:
        return QStringLiteral("off");
    case SessionLightingMode::DeviceDefault:
        return QStringLiteral("device_default");
    }
    return QStringLiteral("automatic");
}

QString AppController::lightingConnection() const
{
    switch (m_rgb->connectionState()) {
    case LightingConnectionState::Disconnected:
        return QStringLiteral("disconnected");
    case LightingConnectionState::Connecting:
        return QStringLiteral("connecting");
    case LightingConnectionState::Negotiating:
        return QStringLiteral("negotiating");
    case LightingConnectionState::Ready:
        return QStringLiteral("ready");
    case LightingConnectionState::Failed:
        return QStringLiteral("failed");
    }
    return QStringLiteral("unknown");
}

QString AppController::lastError() const
{
    if (!m_rgb->lastError().isEmpty()) {
        return m_rgb->lastError();
    }
    if (!m_context->lastError().isEmpty()) {
        return m_context->lastError();
    }
    return m_power->lastError();
}

bool AppController::bridgeConnected() const
{
    return m_context->bridgeConnected();
}

bool AppController::degraded() const
{
    return m_context->isDegraded();
}

QString AppController::globalColor() const
{
    return toHex(displayColor(m_document.globalLighting));
}

QString AppController::globalMode() const
{
    return lightingModeJsonName(m_document.globalLighting.mode);
}

QStringList AppController::globalZones() const
{
    return zoneHexList(m_document.globalLighting);
}

QStringList AppController::zoneNames() const
{
    QStringList names;
    for (const char *name : kZoneNames) {
        names.push_back(QString::fromUtf8(name));
    }
    return names;
}

QStringList AppController::lightingPresets() const
{
    return {
        QStringLiteral("untouched"),
        QStringLiteral("wave"),
        QStringLiteral("cycle"),
        QStringLiteral("breathing"),
        QStringLiteral("off"),
        QStringLiteral("direct"),
    };
}

QStringList AppController::lightingPresetLabels() const
{
    return {
        QStringLiteral("Predvolené firmware (Wave)"),
        QStringLiteral("Wave"),
        QStringLiteral("Cycle"),
        QStringLiteral("Breathing"),
        QStringLiteral("Vypnuté"),
        QStringLiteral("Vlastné farby"),
    };
}

int AppController::globalSpeedPercent() const
{
    return speedToPercent(m_document.globalLighting);
}

QString AppController::globalBreathingColor() const
{
    return toHex(m_document.globalLighting.baseColor.value_or(kDefaultEffectColor));
}

QString AppController::statusSummary() const
{
    return QStringLiteral("%1 · kontext: %2 · svetlá: %3")
        .arg(openRgbPhrase(), contextDisplayName(), lightsPhrase());
}

bool AppController::isSelfWindow() const
{
    return isOwnSurface(m_context->identity());
}

QString AppController::lastExternalApplication() const
{
    return m_lastExternalApplication;
}

QString AppController::contextDisplayName() const
{
    if (isSelfWindow()) {
        if (!m_lastExternalApplication.isEmpty()) {
            return QStringLiteral("Posledná aplikácia: %1").arg(m_lastExternalApplication);
        }
        return QStringLiteral("ContextDeck (toto okno)");
    }
    const ApplicationIdentity identity = m_context->identity();
    if (!identity.isIdentified()) {
        return QStringLiteral("neidentifikovaný");
    }
    return friendlyApplicationName(identity);
}

bool AppController::hasSavedProfiles() const
{
    if (!m_document.applications.isEmpty()) {
        return true;
    }
    return m_document.globalLighting.mode != LightingMode::Untouched;
}

QString AppController::heroKind() const
{
    const Lighting lighting = effectiveLighting();
    if (lighting.mode == LightingMode::Off) {
        return QStringLiteral("off");
    }
    if (lighting.mode == LightingMode::Untouched) {
        return QStringLiteral("untouched");
    }
    if (lighting.mode == LightingMode::Direct) {
        return QStringLiteral("direct");
    }
    return QStringLiteral("effect");
}

QString AppController::heroBadge() const
{
    const Lighting lighting = effectiveLighting();
    if (m_sessionLighting == SessionLightingMode::TemporaryColor) {
        return QStringLiteral("Temporary override");
    }
    if (lighting.mode == LightingMode::Untouched) {
        QString restore = lightingModeJsonName(m_rgb->recordedRestoreMode());
        if (restore.isEmpty()) {
            restore = QStringLiteral("wave");
        }
        restore[0] = restore[0].toUpper();
        return QStringLiteral("Device default (%1)").arg(restore);
    }
    if (lighting.mode == LightingMode::Wave) {
        return QStringLiteral("Wave");
    }
    if (lighting.mode == LightingMode::Cycle) {
        return QStringLiteral("Cycle");
    }
    if (lighting.mode == LightingMode::Breathing) {
        return QStringLiteral("Breathing");
    }
    if (lighting.mode == LightingMode::Off) {
        return QStringLiteral("Off");
    }
    if (lighting.mode == LightingMode::Direct) {
        return QStringLiteral("Direct");
    }
    return lightingModeJsonName(lighting.mode);
}

QStringList AppController::heroZones() const
{
    QStringList list;
    for (const Rgb &color : m_resolution.previewColors) {
        list.push_back(toHex(color));
    }
    if (list.size() != kZoneCount) {
        return zoneHexList(effectiveLighting());
    }
    return list;
}

bool AppController::workspaceLayoutActive() const
{
    return m_resolution.workspaceLayoutActive;
}

bool AppController::workspaceObservationPaused() const
{
    return m_workspace != nullptr && m_workspace->isPaused();
}

QString AppController::workspaceSummary() const
{
    if (!m_resolution.workspaceLayoutActive) {
        return QStringLiteral("Workspace layout is inactive.");
    }
    if (m_resolution.workspaceUnavailable) {
        return QStringLiteral("Workspace observation unavailable — desired fallback is the recorded device default. This is not physical readback.");
    }
    if (currentWorkspaceState().refreshPending) {
        return QStringLiteral("Refreshing desktop snapshot; last desired preview is retained.");
    }
    QString text = QStringLiteral("Desired preview: desktop %1 of %2, indicator capacity %3")
                       .arg(currentWorkspaceState().currentOrdinal)
                       .arg(m_resolution.desktopCount)
                       .arg(m_resolution.indicatorCapacity);
    if (m_resolution.overflowCount > 0) {
        text += QStringLiteral(". Overflow: %1 desktops are not represented.").arg(m_resolution.overflowCount);
    }
    if (m_resolution.currentDesktopUnrepresented) {
        text += QStringLiteral(" Current desktop is not represented.");
    }
    return text;
}

QVariantList AppController::globalZoneSlots() const
{
    QVariantList list;
    const Lighting &lighting = m_document.globalLighting;
    for (int i = 0; i < kZoneCount; ++i) {
        QVariantMap map;
        ZoneRole role = ZoneRole::Static;
        Rgb color = kDefaultEffectColor;
        if (lighting.zones) {
            role = (*lighting.zones)[static_cast<size_t>(i)].role;
            color = (*lighting.zones)[static_cast<size_t>(i)].color;
        } else if (lighting.baseColor) {
            color = *lighting.baseColor;
        }
        QString roleName = QStringLiteral("static");
        switch (role) {
        case ZoneRole::DesktopIndicator:
            roleName = QStringLiteral("desktop_indicator");
            break;
        case ZoneRole::AppColor:
            roleName = QStringLiteral("app_color");
            break;
        case ZoneRole::Off:
            roleName = QStringLiteral("off");
            color = {};
            break;
        case ZoneRole::Static:
            break;
        }
        map.insert(QStringLiteral("role"), roleName);
        map.insert(QStringLiteral("color"), toHex(color));
        map.insert(QStringLiteral("ordinal"), m_resolution.representedOrdinals[static_cast<size_t>(i)]);
        map.insert(QStringLiteral("contribution"), slotContributionName(m_resolution.slotContributions[static_cast<size_t>(i)]));
        list.push_back(map);
    }
    return list;
}

QVariantList AppController::inventory() const
{
    QVariantList list;
    for (const InventoryEntry &entry : m_context->inventory()) {
        QVariantMap map;
        map.insert(QStringLiteral("desktopFileName"), entry.desktopFileName);
        map.insert(QStringLiteral("resourceClass"), entry.resourceClass);
        map.insert(QStringLiteral("resourceName"), entry.resourceName);
        const QString label = !entry.desktopFileName.isEmpty() ? entry.desktopFileName : entry.resourceClass;
        map.insert(QStringLiteral("label"), label);
        list.push_back(map);
    }
    return list;
}

QVariantList AppController::profiles() const
{
    QVariantList list;
    for (const ApplicationProfile &profile : m_document.applications) {
        QVariantMap map;
        map.insert(QStringLiteral("id"), profile.id);
        map.insert(QStringLiteral("displayName"), profile.displayName);
        map.insert(QStringLiteral("color"),
                   toHex(displayColor(profile.lighting.value_or(m_document.globalLighting))));
        const Lighting lighting = profile.lighting.value_or(m_document.globalLighting);
        map.insert(QStringLiteral("mode"), lightingModeJsonName(lighting.mode));
        map.insert(QStringLiteral("zones"), zoneHexList(lighting));
        map.insert(QStringLiteral("speedPercent"), speedToPercent(lighting));
        map.insert(QStringLiteral("breathingColor"), toHex(lighting.baseColor.value_or(kDefaultEffectColor)));
        if (profile.workspace) {
            const WorkspaceAssignment &workspace = *profile.workspace;
            map.insert(QStringLiteral("workspaceSessionId"), workspace.sessionId);
            map.insert(QStringLiteral("workspaceDesktopOrdinal"), workspace.desktopOrdinal);
            map.insert(QStringLiteral("workspaceLaunch"), workspace.launch);
            map.insert(QStringLiteral("workspaceMaximize"), workspace.maximize);
            map.insert(QStringLiteral("workspaceLaunchDesktopFile"), workspace.launchDesktopFile.value_or(QString()));
            map.insert(QStringLiteral("workspaceTitleFallbackEnabled"),
                       workspace.titleFallback.has_value() && workspace.titleFallback->enabled);
            map.insert(QStringLiteral("workspaceTitleFallbackMode"),
                       titleMatchModeJsonName(workspace.titleFallback.has_value() ? workspace.titleFallback->mode
                                                                                 : TitleMatchMode::Contains));
            map.insert(QStringLiteral("workspaceTitleFallbackPattern"),
                       workspace.titleFallback.has_value() ? workspace.titleFallback->pattern : QString());
        } else {
            map.insert(QStringLiteral("workspaceSessionId"), QString());
            map.insert(QStringLiteral("workspaceDesktopOrdinal"), 1);
            map.insert(QStringLiteral("workspaceLaunch"), false);
            map.insert(QStringLiteral("workspaceMaximize"), false);
            map.insert(QStringLiteral("workspaceLaunchDesktopFile"), QString());
            map.insert(QStringLiteral("workspaceTitleFallbackEnabled"), false);
            map.insert(QStringLiteral("workspaceTitleFallbackMode"), QStringLiteral("contains"));
            map.insert(QStringLiteral("workspaceTitleFallbackPattern"), QString());
        }
        list.push_back(map);
    }
    return list;
}

QVariantList AppController::controls() const
{
    QVariantList list;
    const ApplicationIdentity identity = m_context->identity();
    for (const ControlInfo &info : controlCatalog()) {
        const Assignment assignment = resolveAssignment(m_document, identity, info.id);
        QVariantMap map;
        map.insert(QStringLiteral("name"), QString::fromLatin1(info.jsonName));
        map.insert(QStringLiteral("conditional"), info.conditionalOnHardwareEvidence);
        map.insert(QStringLiteral("action"), actionJsonName(assignment.action));
        const ZoneMapEntry *zone = zoneMapEntry(info.id);
        if (zone != nullptr) {
            map.insert(QStringLiteral("zoneName"), QString::fromUtf8(zone->zoneName));
            map.insert(QStringLiteral("zoneVerified"), zone->verified);
            map.insert(QStringLiteral("zonePreview"),
                       zone->verified ? QStringLiteral("verified zone")
                                      : QStringLiteral("unverified preview — no accent writes"));
        }
        map.insert(QStringLiteral("note"),
                   info.conditionalOnHardwareEvidence
                       ? QStringLiteral("Conditional on hardware evidence (G1); not bound in M1.")
                       : QStringLiteral("emit_shortcut is stored but not active until the input broker exists."));
        list.push_back(map);
    }
    return list;
}

QVariantMap AppController::diagnostics() const
{
    QVariantMap map;
    map.insert(QStringLiteral("bridgeConnected"), m_context->bridgeConnected());
    map.insert(QStringLiteral("degraded"), m_context->isDegraded());
    map.insert(QStringLiteral("policyRevision"), m_context->policyRevision());
    map.insert(QStringLiteral("lightingConnection"), lightingConnection());
    map.insert(QStringLiteral("lightingLabel"), lightingLabel());
    map.insert(QStringLiteral("sessionLighting"), sessionLighting());
    map.insert(QStringLiteral("lightingEnabled"), m_rgb->lightingEnabled());
    map.insert(QStringLiteral("hasG213"), m_rgb->hasG213());
    map.insert(QStringLiteral("identityUpdates"), QVariant::fromValue(m_identityUpdates));
    map.insert(QStringLiteral("lightingUpdates"), QVariant::fromValue(m_lightingUpdates));
    map.insert(QStringLiteral("lastError"), lastError());
    map.insert(QStringLiteral("inventoryCount"), m_context->inventory().size());
    map.insert(QStringLiteral("dbusService"), QString(kServiceName));
    map.insert(QStringLiteral("dbusObjectPath"), QString(kContextObjectPath));
    map.insert(QStringLiteral("dbusInterface"), QString(kContextInterface));
    map.insert(QStringLiteral("bridgeId"), m_context->bridgeId());
    map.insert(QStringLiteral("currentIdentity"), m_context->currentIdentity());
    map.insert(QStringLiteral("socketState"), m_rgb->socketStateText());
    map.insert(QStringLiteral("sdkEndpoint"), m_rgb->sdkEndpoint());
    map.insert(QStringLiteral("brokerIpcState"), brokerIpcState());
    map.insert(QStringLiteral("isSelfWindow"), isSelfWindow());
    map.insert(QStringLiteral("lastExternalApplication"), m_lastExternalApplication);
    map.insert(QStringLiteral("workspaceLayoutActive"), workspaceLayoutActive());
    map.insert(QStringLiteral("workspaceUnavailable"), m_resolution.workspaceUnavailable);
    map.insert(QStringLiteral("workspaceRefreshPending"), currentWorkspaceState().refreshPending);
    map.insert(QStringLiteral("workspaceDesktopCount"), m_resolution.desktopCount);
    map.insert(QStringLiteral("workspaceCurrentOrdinal"), currentWorkspaceState().currentOrdinal);
    map.insert(QStringLiteral("workspaceIndicatorCapacity"), m_resolution.indicatorCapacity);
    map.insert(QStringLiteral("workspaceOverflow"), m_resolution.overflowCount);
    map.insert(QStringLiteral("workspaceErrorClass"), m_workspace != nullptr ? m_workspace->errorClass() : QString());
    map.insert(QStringLiteral("workspacePaused"), workspaceObservationPaused());
    map.insert(QStringLiteral("workspaceSnapshots"),
               m_workspace != nullptr ? QVariant::fromValue(m_workspace->snapshotCount()) : 0);
    const QVariantMap workspacePlan = workspacePlanMap();
    map.insert(QStringLiteral("workspaceManagementEnabled"), workspaceManagementEnabled());
    map.insert(QStringLiteral("titleFallbackEnabled"), titleFallbackEnabled());
    map.insert(QStringLiteral("workspaceSessionCount"), static_cast<int>(m_document.workspaceSessions.size()));
    map.insert(QStringLiteral("workspacePlanSessionFound"), workspacePlan.value(QStringLiteral("sessionFound")));
    map.insert(QStringLiteral("workspacePlanDrift"), workspacePlan.value(QStringLiteral("drift")));
    map.insert(QStringLiteral("workspaceObservedRows"), workspaceObserved().value(QStringLiteral("rows")));
    map.insert(QStringLiteral("workspaceObservedWrapping"), workspaceObserved().value(QStringLiteral("wrapping")));
    map.insert(QStringLiteral("workspaceApplyRunning"), m_applyRunning);
    map.insert(QStringLiteral("workspaceApplyStatus"), m_applyStatus);
    map.insert(QStringLiteral("workspaceCheckpointAvailable"), workspaceCheckpointAvailable());
    map.insert(QStringLiteral("workspaceLastResidual"), m_applyResidual);
    map.insert(QStringLiteral("workspaceLastMutationCreated"), m_lastApplyCreated);
    map.insert(QStringLiteral("workspaceLastMutationRemoved"), m_lastApplyRemoved);
    map.insert(QStringLiteral("workspaceLastApplyReverted"), m_lastApplyReverted);
    map.insert(QStringLiteral("workspaceLaunchAttempts"), m_launcher.launchAttemptsInTransaction());
    return map;
}

bool AppController::save()
{
    const SaveOutcome outcome = m_store.save(m_document);
    if (!outcome.ok) {
        m_saveStatus = outcome.error.reason;
        qCWarning(lcUi) << "save refused:" << outcome.error.reason << outcome.error.jsonPath
                        << "preserved=" << outcome.error.preserved;
        return false;
    }
    m_saveStatus = QStringLiteral("saved");
    emit documentChanged();
    applyLighting();
    return true;
}

void AppController::setGlobalColor(const QString &hex)
{
    const auto color = parseHex(hex);
    if (!color) {
        return;
    }
    m_document.globalLighting.baseColor = *color;
    m_document.globalLighting.mode = LightingMode::Direct;
    m_document.globalLighting.zones.reset();
    m_sessionLighting = SessionLightingMode::Automatic;
    emit documentChanged();
    applyLighting();
}

void AppController::setApplicationColor(const QString &id, const QString &hex)
{
    const auto color = parseHex(hex);
    if (!color) {
        return;
    }
    for (ApplicationProfile &profile : m_document.applications) {
        if (profile.id == id) {
            Lighting lighting = applicationLightingOrSanitized(profile, m_document.globalLighting);
            lighting.baseColor = *color;
            lighting.mode = LightingMode::Direct;
            lighting.zones.reset();
            profile.lighting = lighting;
            emit documentChanged();
            applyLighting();
            return;
        }
    }
}

void AppController::addProfileFromInventory(int index)
{
    const auto entries = m_context->inventory();
    if (index < 0 || index >= entries.size()) {
        return;
    }
    const InventoryEntry &entry = entries.at(index);
    const QString id = !entry.desktopFileName.isEmpty() ? entry.desktopFileName : entry.resourceClass;
    if (id.isEmpty()) {
        return;
    }
    for (const ApplicationProfile &existing : m_document.applications) {
        if (existing.id == id) {
            return;
        }
    }
    ApplicationProfile profile;
    profile.id = id;
    profile.displayName = id;
    if (!entry.desktopFileName.isEmpty()) {
        profile.match.desktopFileName = entry.desktopFileName;
    } else if (!entry.resourceClass.isEmpty()) {
        profile.match.resourceClass = entry.resourceClass;
    } else {
        profile.match.resourceName = entry.resourceName;
    }
    Lighting lighting = m_document.globalLighting;
    lighting.mode = LightingMode::Direct;
    profile.lighting = sanitizeApplicationLighting(lighting);
    m_document.applications.push_back(profile);
    emit documentChanged();
}

void AppController::removeProfile(const QString &id)
{
    for (int i = 0; i < m_document.applications.size(); ++i) {
        if (m_document.applications.at(i).id == id) {
            m_document.applications.removeAt(i);
            emit documentChanged();
            refreshResolvedProfile();
            applyLighting();
            return;
        }
    }
}

void AppController::setAutomatic(bool enabled)
{
    if (enabled) {
        restoreAutomatic();
    } else {
        lightsOff();
    }
}

void AppController::lightsOff()
{
    m_sessionLighting = SessionLightingMode::LightsOff;
    m_document.preferences.automaticEnabled = false;
    emit lightingModeChanged();
    applyLighting();
}

void AppController::restoreAutomatic()
{
    m_sessionLighting = SessionLightingMode::Automatic;
    m_document.preferences.automaticEnabled = true;
    emit lightingModeChanged();
    applyLighting();
}

void AppController::restoreDeviceDefault()
{
    m_sessionLighting = SessionLightingMode::DeviceDefault;
    m_document.preferences.automaticEnabled = true;
    emit lightingModeChanged();
    applyLighting();
}

void AppController::setTemporaryColor(const QString &hex)
{
    const auto color = parseHex(hex);
    if (!color) {
        return;
    }
    m_temporaryColor = *color;
    m_sessionLighting = SessionLightingMode::TemporaryColor;
    m_document.preferences.automaticEnabled = false;
    emit lightingModeChanged();
    applyLighting();
}

void AppController::setGlobalLightingMode(const QString &modeName)
{
    const auto mode = lightingModeFromJsonName(modeName);
    if (!mode) {
        return;
    }
    applyMode(m_document.globalLighting, *mode, Rgb{0x7c, 0x3a, 0xed});
    m_sessionLighting = SessionLightingMode::Automatic;
    emit lightingModeChanged();
    emit documentChanged();
    applyLighting();
}

void AppController::setGlobalZoneColor(int index, const QString &hex)
{
    const auto color = parseHex(hex);
    if (!color) {
        return;
    }
    if (!applyZoneColor(m_document.globalLighting, index, *color, Rgb{0x7c, 0x3a, 0xed})) {
        return;
    }
    m_sessionLighting = SessionLightingMode::Automatic;
    emit lightingModeChanged();
    emit documentChanged();
    applyLighting();
}

void AppController::applyGlobalGradient(const QString &startHex, const QString &endHex)
{
    const auto start = parseHex(startHex);
    const auto end = parseHex(endHex);
    if (!start || !end) {
        return;
    }
    applyGradient(m_document.globalLighting, *start, *end);
    m_sessionLighting = SessionLightingMode::Automatic;
    emit lightingModeChanged();
    emit documentChanged();
    recompute();
}

void AppController::setGlobalZoneRole(int index, const QString &roleName)
{
    if (index < 0 || index >= kZoneCount) {
        return;
    }
    ZoneRole role = ZoneRole::Static;
    if (roleName == QLatin1String("desktop_indicator")) {
        role = ZoneRole::DesktopIndicator;
    } else if (roleName == QLatin1String("app_color")) {
        role = ZoneRole::AppColor;
    } else if (roleName == QLatin1String("off")) {
        role = ZoneRole::Off;
    } else if (roleName != QLatin1String("static")) {
        return;
    }
    std::array<ZoneValue, kZoneCount> zones = m_document.globalLighting.zones.value_or(
        zoneValuesFromColors(zoneColorsFromPreset(m_document.globalLighting)));
    if (!m_document.globalLighting.zones && !m_document.globalLighting.baseColor) {
        for (ZoneValue &zone : zones) {
            zone.color = kDefaultEffectColor;
        }
    }
    zones[static_cast<size_t>(index)].role = role;
    if (role == ZoneRole::Off) {
        zones[static_cast<size_t>(index)].color = {};
    } else if (zones[static_cast<size_t>(index)].color == Rgb{}) {
        zones[static_cast<size_t>(index)].color = kDefaultEffectColor;
    }
    m_document.globalLighting.zones = zones;
    m_document.globalLighting.mode = LightingMode::Direct;
    if (!m_document.globalLighting.baseColor) {
        m_document.globalLighting.baseColor = kDefaultEffectColor;
    }
    m_sessionLighting = SessionLightingMode::Automatic;
    emit lightingModeChanged();
    emit documentChanged();
    recompute();
}

void AppController::useDefaultWorkspaceLayout()
{
    std::array<ZoneValue, kZoneCount> zones{};
    for (int i = 0; i < 4; ++i) {
        zones[static_cast<size_t>(i)].role = ZoneRole::DesktopIndicator;
        zones[static_cast<size_t>(i)].color = kDefaultEffectColor;
    }
    zones[4].role = ZoneRole::AppColor;
    zones[4].color = Rgb{0x40, 0x40, 0x40};
    m_document.globalLighting.mode = LightingMode::Direct;
    m_document.globalLighting.restoreMode = LightingMode::Wave;
    m_document.globalLighting.baseColor = kDefaultEffectColor;
    m_document.globalLighting.zones = zones;
    m_sessionLighting = SessionLightingMode::Automatic;
    emit lightingModeChanged();
    emit documentChanged();
    recompute();
}

void AppController::useStaticZoneLayout()
{
    std::array<Rgb, kZoneCount> colors = zoneColorsFromPreset(m_document.globalLighting);
    if (!m_document.globalLighting.zones && !m_document.globalLighting.baseColor) {
        colors.fill(kDefaultEffectColor);
    }
    m_document.globalLighting.zones = zoneValuesFromColors(colors);
    m_document.globalLighting.mode = LightingMode::Direct;
    if (!m_document.globalLighting.baseColor) {
        m_document.globalLighting.baseColor = colors.front();
    }
    m_sessionLighting = SessionLightingMode::Automatic;
    emit lightingModeChanged();
    emit documentChanged();
    recompute();
}

void AppController::setWorkspaceObservationPaused(bool paused)
{
    if (m_workspace == nullptr) {
        return;
    }
    m_workspace->setPaused(paused);
    emit diagnosticsChanged();
    recompute();
}

bool AppController::workspaceManagementEnabled() const
{
    return m_document.preferences.workspaceManagementEnabled;
}

bool AppController::titleFallbackEnabled() const
{
    return m_document.preferences.titleFallbackEnabled;
}

QString AppController::activeWorkspaceSession() const
{
    return m_document.preferences.activeWorkspaceSessionId.value_or(QString());
}

QVariantList AppController::workspaceSessions() const
{
    QVariantList list;
    for (const WorkspaceSession &session : m_document.workspaceSessions) {
        QVariantMap map;
        map.insert(QStringLiteral("id"), session.id);
        map.insert(QStringLiteral("displayName"), session.displayName);
        map.insert(QStringLiteral("hasRows"), session.rows.has_value());
        map.insert(QStringLiteral("rows"), session.rows.value_or(0));
        map.insert(QStringLiteral("hasWrapping"), session.navigationWrapping.has_value());
        map.insert(QStringLiteral("wrapping"), session.navigationWrapping.value_or(false));
        map.insert(QStringLiteral("desktopCount"), static_cast<int>(session.desktops.size()));
        QVariantList desktops;
        for (const WorkspaceDesktopEntry &desktop : session.desktops) {
            QVariantMap entry;
            entry.insert(QStringLiteral("ordinal"), desktop.ordinal);
            entry.insert(QStringLiteral("name"), desktop.name);
            desktops.push_back(entry);
        }
        map.insert(QStringLiteral("desktops"), desktops);
        map.insert(QStringLiteral("active"), session.id == activeWorkspaceSession());
        list.push_back(map);
    }
    return list;
}

QVariantList AppController::workspaceSessionOptions() const
{
    QVariantList list;
    QVariantMap none;
    none.insert(QStringLiteral("id"), QString());
    none.insert(QStringLiteral("label"), QStringLiteral("(žiadna)"));
    list.push_back(none);
    for (const WorkspaceSession &session : m_document.workspaceSessions) {
        QVariantMap map;
        map.insert(QStringLiteral("id"), session.id);
        map.insert(QStringLiteral("label"), session.displayName);
        list.push_back(map);
    }
    return list;
}

QVariantList AppController::workspaceDesktopEntries() const
{
    QVariantList list;
    for (const WorkspaceSession &session : m_document.workspaceSessions) {
        for (const WorkspaceDesktopEntry &desktop : session.desktops) {
            QVariantMap entry;
            entry.insert(QStringLiteral("sessionId"), session.id);
            entry.insert(QStringLiteral("sessionLabel"), session.displayName);
            entry.insert(QStringLiteral("ordinal"), desktop.ordinal);
            entry.insert(QStringLiteral("name"), desktop.name);
            list.push_back(entry);
        }
    }
    return list;
}

QVariantMap AppController::workspaceObserved() const
{
    const WorkspaceState state = currentWorkspaceState();
    QVariantMap map;
    map.insert(QStringLiteral("available"), state.availability == WorkspaceAvailability::Available);
    map.insert(QStringLiteral("refreshPending"), state.refreshPending);
    map.insert(QStringLiteral("count"), static_cast<int>(state.desktops.size()));
    map.insert(QStringLiteral("currentOrdinal"), state.currentOrdinal);
    map.insert(QStringLiteral("rows"), state.rows.has_value() ? QVariant(*state.rows) : QVariant());
    map.insert(QStringLiteral("wrapping"),
               state.navigationWrappingAround.has_value() ? QVariant(*state.navigationWrappingAround) : QVariant());
    map.insert(QStringLiteral("paused"), workspaceObservationPaused());
    map.insert(QStringLiteral("errorClass"), m_workspace != nullptr ? m_workspace->errorClass() : QString());
    return map;
}

QVariantMap AppController::workspacePlanPreview() const
{
    return workspacePlanMap();
}

QVariantMap AppController::workspacePlanMap() const
{
    const WorkspaceState observed = currentWorkspaceState();
    const WorkspacePlan plan =
        computeWorkspacePlan(m_document, activeWorkspaceSession(), observed, openWindowIdentities());
    m_lastPlan = plan;
    m_hasLastPlan = true;
    m_previewFingerprint = workspacePreviewFingerprint(plan, observed);
    m_previewOwnerGeneration = m_workspace != nullptr ? m_workspace->ownerGeneration() : 0;
    QVariantMap map;
    map.insert(QStringLiteral("sessionId"), plan.sessionId);
    map.insert(QStringLiteral("sessionFound"), plan.sessionFound);
    map.insert(QStringLiteral("managementEnabled"), plan.managementEnabled);
    map.insert(QStringLiteral("observationAvailable"), plan.observationAvailable);
    map.insert(QStringLiteral("desiredDesktopCount"), plan.desiredDesktopCount);
    map.insert(QStringLiteral("observedDesktopCount"), plan.observedDesktopCount);
    map.insert(QStringLiteral("drift"), plan.drift);
    map.insert(QStringLiteral("extraDesktop"), plan.extraDesktop);
    map.insert(QStringLiteral("rowsChange"), plan.rowsChange);
    map.insert(QStringLiteral("wrappingChange"), plan.wrappingChange);
    map.insert(QStringLiteral("desiredRows"), plan.desiredRows.has_value() ? QVariant(*plan.desiredRows) : QVariant());
    map.insert(QStringLiteral("observedRows"),
               plan.observedRows.has_value() ? QVariant(*plan.observedRows) : QVariant());
    map.insert(QStringLiteral("desiredWrapping"),
               plan.desiredWrapping.has_value() ? QVariant(*plan.desiredWrapping) : QVariant());
    map.insert(QStringLiteral("observedWrapping"),
               plan.observedWrapping.has_value() ? QVariant(*plan.observedWrapping) : QVariant());
    QVariantList desktops;
    for (const WorkspaceDesktopPlan &desktop : plan.desktops) {
        QVariantMap entry;
        entry.insert(QStringLiteral("ordinal"), desktop.ordinal);
        entry.insert(QStringLiteral("name"), desktop.name);
        entry.insert(QStringLiteral("observedName"), desktop.observedName);
        entry.insert(QStringLiteral("create"), desktop.create);
        entry.insert(QStringLiteral("rename"), desktop.rename);
        desktops.push_back(entry);
    }
    map.insert(QStringLiteral("desktops"), desktops);
    QVariantList launches;
    for (const WorkspaceLaunchPlan &launch : plan.launches) {
        QVariantMap entry;
        entry.insert(QStringLiteral("profileId"), launch.profileId);
        entry.insert(QStringLiteral("displayName"), launch.displayName);
        entry.insert(QStringLiteral("desktopOrdinal"), launch.desktopOrdinal);
        entry.insert(QStringLiteral("maximize"), launch.maximize);
        entry.insert(QStringLiteral("intent"), workspaceLaunchIntentName(launch.intent));
        launches.push_back(entry);
    }
    map.insert(QStringLiteral("launches"), launches);
    return map;
}

QVector<ApplicationIdentity> AppController::openWindowIdentities() const
{
    QVector<ApplicationIdentity> identities;
    for (const InventoryEntry &entry : m_context->inventory()) {
        ApplicationIdentity identity;
        identity.desktopFileName = entry.desktopFileName;
        identity.resourceClass = entry.resourceClass;
        identity.resourceName = entry.resourceName;
        identities.push_back(identity);
    }
    return identities;
}

bool AppController::workspaceSessionExists(const QString &id) const
{
    if (id.isEmpty()) {
        return false;
    }
    for (const WorkspaceSession &session : m_document.workspaceSessions) {
        if (session.id == id) {
            return true;
        }
    }
    return false;
}

int AppController::workspaceSessionDesktopCount(const QString &id) const
{
    for (const WorkspaceSession &session : m_document.workspaceSessions) {
        if (session.id == id) {
            return static_cast<int>(session.desktops.size());
        }
    }
    return 0;
}

WorkspaceAssignment *AppController::applicationWorkspace(const QString &id)
{
    for (ApplicationProfile &profile : m_document.applications) {
        if (profile.id != id) {
            continue;
        }
        if (!profile.workspace) {
            return nullptr;
        }
        return &*profile.workspace;
    }
    return nullptr;
}

void AppController::setWorkspaceManagementEnabled(bool enabled)
{
    if (m_document.preferences.workspaceManagementEnabled == enabled) {
        return;
    }
    m_document.preferences.workspaceManagementEnabled = enabled;
    emit documentChanged();
    emit presentationChanged();
}

void AppController::setTitleFallbackEnabled(bool enabled)
{
    if (m_document.preferences.titleFallbackEnabled == enabled) {
        return;
    }
    m_document.preferences.titleFallbackEnabled = enabled;
    m_context->setTitleFallbackEnabled(enabled);
    if (!enabled) {
        (void)m_context->takeTitleHint();
    }
    emit documentChanged();
    emit presentationChanged();
}

void AppController::setActiveWorkspaceSession(const QString &id)
{
    const QString normalized = workspaceSessionExists(id) ? id : QString();
    if (activeWorkspaceSession() == normalized) {
        return;
    }
    if (normalized.isEmpty()) {
        m_document.preferences.activeWorkspaceSessionId.reset();
    } else {
        m_document.preferences.activeWorkspaceSessionId = normalized;
    }
    emit documentChanged();
    emit presentationChanged();
}

void AppController::addWorkspaceSession(const QString &displayName)
{
    const QString name = sanitizeDisplayText(displayName, kMaxDisplayNameBytes, QStringLiteral("Session"));
    int suffix = 1;
    QString id = QStringLiteral("session-%1").arg(suffix);
    while (workspaceSessionExists(id)) {
        ++suffix;
        id = QStringLiteral("session-%1").arg(suffix);
    }
    WorkspaceSession session;
    session.id = id;
    session.displayName = name;
    WorkspaceDesktopEntry desktop;
    desktop.ordinal = 1;
    desktop.name = QStringLiteral("Plocha 1");
    session.desktops.push_back(desktop);
    m_document.workspaceSessions.push_back(session);
    if (!m_document.preferences.activeWorkspaceSessionId) {
        m_document.preferences.activeWorkspaceSessionId = id;
    }
    emit documentChanged();
    emit presentationChanged();
}

void AppController::removeWorkspaceSession(const QString &id)
{
    for (int i = 0; i < m_document.workspaceSessions.size(); ++i) {
        if (m_document.workspaceSessions.at(i).id != id) {
            continue;
        }
        m_document.workspaceSessions.removeAt(i);
        if (activeWorkspaceSession() == id) {
            m_document.preferences.activeWorkspaceSessionId.reset();
        }
        for (ApplicationProfile &profile : m_document.applications) {
            if (profile.workspace && profile.workspace->sessionId == id) {
                profile.workspace.reset();
            }
        }
        emit documentChanged();
        emit presentationChanged();
        return;
    }
}

void AppController::renameWorkspaceSession(const QString &id, const QString &displayName)
{
    const QString name = sanitizeDisplayText(displayName, kMaxDisplayNameBytes, QString());
    if (name.isEmpty()) {
        return;
    }
    for (WorkspaceSession &session : m_document.workspaceSessions) {
        if (session.id == id) {
            if (session.displayName == name) {
                return;
            }
            session.displayName = name;
            emit documentChanged();
            emit presentationChanged();
            return;
        }
    }
}

void AppController::setWorkspaceSessionRows(const QString &id, int rows)
{
    for (WorkspaceSession &session : m_document.workspaceSessions) {
        if (session.id != id) {
            continue;
        }
        if (rows <= 0) {
            if (!session.rows.has_value()) {
                return;
            }
            session.rows.reset();
        } else {
            const int clamped = std::clamp(rows, 1, kMaxWorkspaceDesktops);
            if (session.rows.has_value() && *session.rows == clamped) {
                return;
            }
            session.rows = clamped;
        }
        emit documentChanged();
        emit presentationChanged();
        return;
    }
}

void AppController::setWorkspaceSessionWrapping(const QString &id, bool enabled)
{
    for (WorkspaceSession &session : m_document.workspaceSessions) {
        if (session.id != id) {
            continue;
        }
        if (session.navigationWrapping.has_value() && *session.navigationWrapping == enabled) {
            return;
        }
        session.navigationWrapping = enabled;
        emit documentChanged();
        emit presentationChanged();
        return;
    }
}

void AppController::setWorkspaceSessionDesktopCount(const QString &id, int count)
{
    for (WorkspaceSession &session : m_document.workspaceSessions) {
        if (session.id != id) {
            continue;
        }
        const int clamped = std::clamp(count, 1, kMaxWorkspaceDesktops);
        if (session.desktops.size() == clamped) {
            return;
        }
        if (session.desktops.size() < clamped) {
            while (session.desktops.size() < clamped) {
                WorkspaceDesktopEntry desktop;
                desktop.ordinal = static_cast<int>(session.desktops.size()) + 1;
                desktop.name = QStringLiteral("Plocha %1").arg(desktop.ordinal);
                session.desktops.push_back(desktop);
            }
        } else {
            session.desktops.resize(clamped);
        }
        for (ApplicationProfile &profile : m_document.applications) {
            if (profile.workspace && profile.workspace->sessionId == id) {
                profile.workspace->desktopOrdinal = std::min(profile.workspace->desktopOrdinal, clamped);
            }
        }
        emit documentChanged();
        emit presentationChanged();
        return;
    }
}

void AppController::setWorkspaceSessionDesktopName(const QString &id, int ordinal, const QString &name)
{
    const QString sanitized = sanitizeDisplayText(name, kMaxDisplayNameBytes, QString());
    if (sanitized.isEmpty()) {
        return;
    }
    for (WorkspaceSession &session : m_document.workspaceSessions) {
        if (session.id != id) {
            continue;
        }
        for (WorkspaceDesktopEntry &desktop : session.desktops) {
            if (desktop.ordinal != ordinal) {
                continue;
            }
            if (desktop.name == sanitized) {
                return;
            }
            desktop.name = sanitized;
            emit documentChanged();
            emit presentationChanged();
            return;
        }
        return;
    }
}

void AppController::setApplicationWorkspaceSession(const QString &id, const QString &sessionId)
{
    for (ApplicationProfile &profile : m_document.applications) {
        if (profile.id != id) {
            continue;
        }
        if (!workspaceSessionExists(sessionId)) {
            if (profile.workspace) {
                profile.workspace.reset();
                emit documentChanged();
                emit presentationChanged();
            }
            return;
        }
        if (!profile.workspace) {
            profile.workspace = WorkspaceAssignment{};
        }
        profile.workspace->sessionId = sessionId;
        const int desktopCount = workspaceSessionDesktopCount(sessionId);
        profile.workspace->desktopOrdinal = std::clamp(profile.workspace->desktopOrdinal, 1, desktopCount);
        emit documentChanged();
        emit presentationChanged();
        return;
    }
}

void AppController::setApplicationWorkspaceDesktop(const QString &id, int ordinal)
{
    WorkspaceAssignment *workspace = applicationWorkspace(id);
    if (workspace == nullptr || !workspaceSessionExists(workspace->sessionId)) {
        return;
    }
    const int clamped = std::clamp(ordinal, 1, workspaceSessionDesktopCount(workspace->sessionId));
    if (workspace->desktopOrdinal == clamped) {
        return;
    }
    workspace->desktopOrdinal = clamped;
    emit documentChanged();
    emit presentationChanged();
}

void AppController::setApplicationWorkspaceLaunch(const QString &id, bool launch)
{
    WorkspaceAssignment *workspace = applicationWorkspace(id);
    if (workspace == nullptr || workspace->launch == launch) {
        return;
    }
    workspace->launch = launch;
    emit documentChanged();
    emit presentationChanged();
}

void AppController::setApplicationWorkspaceMaximize(const QString &id, bool maximize)
{
    WorkspaceAssignment *workspace = applicationWorkspace(id);
    if (workspace == nullptr || workspace->maximize == maximize) {
        return;
    }
    workspace->maximize = maximize;
    emit documentChanged();
    emit presentationChanged();
}

void AppController::setApplicationWorkspaceLaunchFile(const QString &id, const QString &desktopFile)
{
    WorkspaceAssignment *workspace = applicationWorkspace(id);
    if (workspace == nullptr) {
        return;
    }
    const QString trimmed = desktopFile.trimmed();
    if (trimmed.isEmpty()) {
        if (workspace->launchDesktopFile.has_value()) {
            workspace->launchDesktopFile.reset();
            emit documentChanged();
            emit presentationChanged();
        }
        return;
    }
    if (!workspaceDesktopIdLooksValid(trimmed)) {
        return;
    }
    if (workspace->launchDesktopFile.has_value() && *workspace->launchDesktopFile == trimmed) {
        return;
    }
    workspace->launchDesktopFile = trimmed;
    emit documentChanged();
    emit presentationChanged();
}

void AppController::setApplicationTitleFallback(const QString &id, bool enabled, const QString &mode,
                                                const QString &pattern)
{
    const auto matchMode = titleMatchModeFromJsonName(mode);
    if (!matchMode) {
        return;
    }
    WorkspaceAssignment *workspace = applicationWorkspace(id);
    if (workspace == nullptr) {
        return;
    }
    const QString sanitized = sanitizeDisplayText(pattern, kMaxTitlePatternBytes, QString());
    if (!workspace->titleFallback) {
        workspace->titleFallback = TitleFallback{};
    }
    if (workspace->titleFallback->enabled == enabled && workspace->titleFallback->mode == *matchMode
        && workspace->titleFallback->pattern == sanitized) {
        return;
    }
    workspace->titleFallback->enabled = enabled;
    workspace->titleFallback->mode = *matchMode;
    workspace->titleFallback->pattern = sanitized;
    emit documentChanged();
    emit presentationChanged();
}

void AppController::setGlobalSpeed(int percent)
{
    if (!applySpeedPercent(m_document.globalLighting, percent)) {
        return;
    }
    m_sessionLighting = SessionLightingMode::Automatic;
    emit lightingModeChanged();
    emit documentChanged();
    applyLighting();
}

void AppController::setGlobalBreathingColor(const QString &hex)
{
    const auto color = parseHex(hex);
    if (!color) {
        return;
    }
    applyBreathingColor(m_document.globalLighting, *color);
    m_sessionLighting = SessionLightingMode::Automatic;
    emit lightingModeChanged();
    emit documentChanged();
    applyLighting();
}

void AppController::setApplicationLightingMode(const QString &id, const QString &modeName)
{
    const auto mode = lightingModeFromJsonName(modeName);
    if (!mode) {
        return;
    }
    for (ApplicationProfile &profile : m_document.applications) {
        if (profile.id == id) {
            Lighting lighting = applicationLightingOrSanitized(profile, m_document.globalLighting);
            applyMode(lighting, *mode, Rgb{0x7c, 0x3a, 0xed});
            profile.lighting = lighting;
            emit documentChanged();
            applyLighting();
            return;
        }
    }
}

void AppController::setApplicationZoneColor(const QString &id, int index, const QString &hex)
{
    const auto color = parseHex(hex);
    if (!color) {
        return;
    }
    for (ApplicationProfile &profile : m_document.applications) {
        if (profile.id == id) {
            Lighting lighting = applicationLightingOrSanitized(profile, m_document.globalLighting);
            if (!applyZoneColor(lighting, index, *color, Rgb{0x7c, 0x3a, 0xed})) {
                return;
            }
            profile.lighting = lighting;
            emit documentChanged();
            applyLighting();
            return;
        }
    }
}

void AppController::applyApplicationGradient(const QString &id, const QString &startHex, const QString &endHex)
{
    const auto start = parseHex(startHex);
    const auto end = parseHex(endHex);
    if (!start || !end) {
        return;
    }
    for (ApplicationProfile &profile : m_document.applications) {
        if (profile.id == id) {
            Lighting lighting = applicationLightingOrSanitized(profile, m_document.globalLighting);
            applyGradient(lighting, *start, *end);
            profile.lighting = lighting;
            emit documentChanged();
            applyLighting();
            return;
        }
    }
}

void AppController::setApplicationSpeed(const QString &id, int percent)
{
    for (ApplicationProfile &profile : m_document.applications) {
        if (profile.id == id) {
            Lighting lighting = applicationLightingOrSanitized(profile, m_document.globalLighting);
            if (!applySpeedPercent(lighting, percent)) {
                return;
            }
            profile.lighting = lighting;
            emit documentChanged();
            applyLighting();
            return;
        }
    }
}

void AppController::setApplicationBreathingColor(const QString &id, const QString &hex)
{
    const auto color = parseHex(hex);
    if (!color) {
        return;
    }
    for (ApplicationProfile &profile : m_document.applications) {
        if (profile.id == id) {
            Lighting lighting = applicationLightingOrSanitized(profile, m_document.globalLighting);
            applyBreathingColor(lighting, *color);
            profile.lighting = lighting;
            emit documentChanged();
            applyLighting();
            return;
        }
    }
}

void AppController::displaysOff()
{
    m_power->displaysOff();
    emit diagnosticsChanged();
}

bool AppController::canSuspend() const
{
    return m_power->suspendAllowed();
}

bool AppController::suspend()
{
    const bool ok = m_power->suspend();
    emit diagnosticsChanged();
    return ok;
}

void AppController::assignEmitShortcut(const QString &controlName, const QString &key, const QStringList &modifiers,
                                       bool applicationLevel, const QString &applicationId)
{
    const auto control = controlFromJsonName(controlName);
    if (!control || controlIsConditional(*control)) {
        return;
    }
    if (!isAllowedChordKey(key) || chordKeyLooksLikeShellOrPath(key)) {
        return;
    }
    Assignment assignment;
    assignment.action = ActionType::EmitShortcut;
    Chord chord;
    chord.key = key;
    for (const QString &name : modifiers) {
        const auto modifier = modifierFromJsonName(name);
        if (!modifier) {
            return;
        }
        chord.modifiers.push_back(*modifier);
    }
    assignment.chord = chord;
    if (applicationLevel) {
        for (ApplicationProfile &profile : m_document.applications) {
            if (profile.id == applicationId) {
                profile.keys.insert(*control, assignment);
                emit documentChanged();
                return;
            }
        }
    } else {
        m_document.globalKeys.insert(*control, assignment);
        emit documentChanged();
    }
}

bool AppController::workspaceApplyAvailable() const
{
    if (m_applyRunning) {
        return false;
    }
    if (!m_document.preferences.workspaceManagementEnabled) {
        return false;
    }
    if (findWorkspaceSession(m_document, activeWorkspaceSession()) == nullptr) {
        return false;
    }
    if (m_workspace == nullptr) {
        return false;
    }
    const WorkspaceState state = currentWorkspaceState();
    if (state.availability != WorkspaceAvailability::Available || state.refreshPending) {
        return false;
    }
    return m_workspace->activeLogicalRequestId() == 0;
}

bool AppController::workspaceCheckpointAvailable() const
{
    return m_mutator.checkpointExists();
}

bool AppController::applyWorkspaceSession(bool switchCurrent, bool removeExtras)
{
    const auto refuse = [this](const QString &status) {
        m_applyStatus = status;
        m_applyResidual.clear();
        emit presentationChanged();
        return false;
    };
    if (m_applyRunning) {
        return refuse(QStringLiteral("in-progress"));
    }
    if (!m_document.preferences.workspaceManagementEnabled) {
        return refuse(QStringLiteral("management-disabled"));
    }
    const QString sessionId = activeWorkspaceSession();
    if (findWorkspaceSession(m_document, sessionId) == nullptr) {
        return refuse(QStringLiteral("no-session"));
    }
    if (m_workspace == nullptr) {
        return refuse(QStringLiteral("observation-unavailable"));
    }
    const WorkspaceState observed = m_workspace->state();
    if (observed.availability != WorkspaceAvailability::Available || observed.refreshPending
        || m_workspace->activeLogicalRequestId() != 0) {
        return refuse(QStringLiteral("observation-stale"));
    }
    if (m_workspace->ownerGeneration() != m_previewOwnerGeneration) {
        return refuse(QStringLiteral("owner-changed"));
    }
    const WorkspacePlan plan = computeWorkspacePlan(m_document, sessionId, observed, openWindowIdentities());
    const QString fingerprint = workspacePreviewFingerprint(plan, observed);
    if (!m_hasLastPlan || m_previewFingerprint.isEmpty() || fingerprint != m_previewFingerprint) {
        return refuse(QStringLiteral("preview-stale"));
    }

    m_applyRunning = true;
    m_applyStatus.clear();
    m_applyResidual.clear();
    m_lastApplyReverted = false;
    m_lastApplyCreated = 0;
    m_lastApplyRemoved = 0;
    emit presentationChanged();

    m_lastPlan = plan;
    m_hasLastPlan = true;
    m_launcher.beginTransaction(QDateTime::currentMSecsSinceEpoch());
    WorkspaceMutationOptions options;
    options.switchCurrent = switchCurrent;
    options.removeExtras = removeExtras;
    const WorkspaceMutationResult result = m_mutator.apply(plan, observed, options);
    if (result.ok && !result.noChanges) {
        launchPlannedProfiles(plan);
    }
    m_lastApplyReverted = result.reverted && !result.revertFailed;
    m_lastApplyCreated = result.createdCount;
    m_lastApplyRemoved = result.removedCount;
    m_applyResidual = result.residualClass;
    if (result.ok) {
        m_applyStatus = result.noChanges ? QStringLiteral("no-changes")
                                         : (result.residualClass.isEmpty() ? QStringLiteral("applied")
                                                                           : QStringLiteral("applied-with-residual"));
    } else {
        m_applyStatus = result.reverted ? (result.revertFailed ? QStringLiteral("failed") : QStringLiteral("failed-reverted"))
                                        : QStringLiteral("failed-no-revert");
    }
    m_launcher.endTransaction();
    m_applyRunning = false;
    (void)workspacePlanMap();
    emit presentationChanged();
    emit diagnosticsChanged();
    return result.ok;
}

bool AppController::revertWorkspaceApply()
{
    if (m_applyRunning) {
        m_applyStatus = QStringLiteral("in-progress");
        emit presentationChanged();
        return false;
    }
    if (!m_mutator.checkpointExists()) {
        m_applyStatus = QStringLiteral("no-checkpoint");
        m_applyResidual.clear();
        emit presentationChanged();
        return false;
    }
    const WorkspaceState observed = m_workspace != nullptr ? m_workspace->state() : WorkspaceState{};
    const WorkspaceMutationResult result = m_mutator.revert(observed);
    m_lastApplyReverted = result.ok;
    m_applyResidual = result.residualClass;
    if (result.ok) {
        m_applyStatus = result.residualClass.isEmpty() ? QStringLiteral("reverted")
                                                       : QStringLiteral("reverted-with-residual");
    } else {
        m_applyStatus = QStringLiteral("revert-failed");
    }
    (void)workspacePlanMap();
    emit presentationChanged();
    emit diagnosticsChanged();
    return result.ok;
}

void AppController::onWorkspaceDesktopCreated(const QString &id, int position)
{
    if (!m_applyRunning) {
        return;
    }
    m_mutator.recordCreatedDesktop(id, position);
}

void AppController::onDesktopCreatedInTransaction(const QString &id, int position)
{
    Q_UNUSED(id);
    if (!m_applyRunning) {
        return;
    }
    launchProfilesForOrdinal(position + 1);
}

void AppController::launchPlannedProfiles(const WorkspacePlan &plan)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const WorkspaceLaunchPlan &entry : plan.launches) {
        if (entry.intent != WorkspaceLaunchIntent::WouldLaunch || m_launcher.hasAttempted(entry.profileId)) {
            continue;
        }
        (void)m_launcher.requestLaunch(entry, now);
    }
}

void AppController::launchProfilesForOrdinal(int ordinal)
{
    if (!m_hasLastPlan) {
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const WorkspaceLaunchPlan &entry : m_lastPlan.launches) {
        if (entry.desktopOrdinal != ordinal || entry.intent != WorkspaceLaunchIntent::WouldLaunch) {
            continue;
        }
        if (!m_launcher.hasAttempted(entry.profileId)) {
            (void)m_launcher.requestLaunch(entry, now);
        } else {
            (void)m_launcher.retryForDesktopCreated(entry, now);
        }
    }
}

QString AppController::workspacePreviewFingerprint(const WorkspacePlan &plan, const WorkspaceState &state) const
{
    QString text = plan.sessionId;
    text += QLatin1Char('|');
    text += plan.sessionFound ? QLatin1Char('1') : QLatin1Char('0');
    text += plan.managementEnabled ? QLatin1Char('1') : QLatin1Char('0');
    text += plan.observationAvailable ? QLatin1Char('1') : QLatin1Char('0');
    text += QLatin1Char('|');
    text += QString::number(plan.desiredDesktopCount);
    text += QLatin1Char(',');
    text += QString::number(plan.observedDesktopCount);
    text += QLatin1Char(',');
    text += plan.extraDesktop ? QLatin1Char('1') : QLatin1Char('0');
    text += plan.rowsChange ? QLatin1Char('1') : QLatin1Char('0');
    text += plan.wrappingChange ? QLatin1Char('1') : QLatin1Char('0');
    text += QLatin1Char('|');
    for (const WorkspaceDesktopPlan &desktop : plan.desktops) {
        text += QString::number(desktop.ordinal);
        text += QLatin1Char(':');
        text += desktop.name;
        text += QLatin1Char(':');
        text += desktop.observedName;
        text += QLatin1Char(':');
        text += desktop.create ? QLatin1Char('1') : QLatin1Char('0');
        text += desktop.rename ? QLatin1Char('1') : QLatin1Char('0');
        text += QLatin1Char(';');
    }
    text += QLatin1Char('|');
    for (const WorkspaceLaunchPlan &launch : plan.launches) {
        text += launch.profileId;
        text += QLatin1Char(':');
        text += QString::number(launch.desktopOrdinal);
        text += QLatin1Char(':');
        text += launch.maximize ? QLatin1Char('1') : QLatin1Char('0');
        text += QLatin1Char(':');
        text += launch.desktopFileId;
        text += QLatin1Char(':');
        text += workspaceLaunchIntentName(launch.intent);
        text += QLatin1Char(';');
    }
    text += QLatin1Char('|');
    text += QString::number(state.currentOrdinal);
    text += QLatin1Char(':');
    text += state.rows.has_value() ? QString::number(*state.rows) : QStringLiteral("-");
    text += QLatin1Char(':');
    text += state.navigationWrappingAround.has_value()
        ? (*state.navigationWrappingAround ? QStringLiteral("1") : QStringLiteral("0"))
        : QStringLiteral("-");
    return text;
}

void AppController::onIdentityChanged()
{
    onContextInputsChanged();
}

void AppController::onContextInputsChanged()
{
    if (m_contextEventQueued) {
        return;
    }
    m_contextEventQueued = true;
    QMetaObject::invokeMethod(
        this,
        [this]() {
            m_contextEventQueued = false;
            ++m_identityUpdates;
            rememberExternalContext();
            const ApplicationIdentity identity = m_context->identity();
            if (m_sessionLighting == SessionLightingMode::TemporaryColor && !isOwnSurface(identity)
                && identity.isIdentified()) {
                m_sessionLighting = SessionLightingMode::Automatic;
                m_document.preferences.automaticEnabled = true;
                emit lightingModeChanged();
                qCInfo(lcUi) << "temporary_color expired on external identity change";
            }
            refreshResolvedProfile();
            recompute();
            emit contextChanged();
            emit diagnosticsChanged();
        },
        Qt::QueuedConnection);
}

void AppController::scheduleRecompute()
{
    if (m_recomputeQueued) {
        return;
    }
    m_recomputeQueued = true;
    QMetaObject::invokeMethod(this, &AppController::recompute, Qt::QueuedConnection);
}

WorkspaceState AppController::currentWorkspaceState() const
{
    if (m_workspace == nullptr) {
        return WorkspaceState{};
    }
    return m_workspace->state();
}

std::optional<Lighting> AppController::sessionOverrideLighting() const
{
    if (m_sessionLighting == SessionLightingMode::LightsOff) {
        Lighting lighting;
        lighting.mode = LightingMode::Off;
        return lighting;
    }
    if (m_sessionLighting == SessionLightingMode::DeviceDefault) {
        return untouchedLighting();
    }
    if (m_sessionLighting == SessionLightingMode::TemporaryColor) {
        Lighting lighting;
        lighting.mode = LightingMode::Direct;
        lighting.baseColor = m_temporaryColor;
        return lighting;
    }
    return std::nullopt;
}

void AppController::recompute()
{
    m_recomputeQueued = false;
    const WorkspaceState workspace = currentWorkspaceState();
    m_resolution = resolveContextLighting(m_document, m_context->identity(), workspace, sessionOverrideLighting());
    const bool deferWorkspace = workspace.refreshPending && !sessionOverrideLighting().has_value() && m_hasLastDesired
        && m_resolution.workspaceLayoutActive && !m_resolution.workspaceUnavailable;
    if (deferWorkspace) {
        emit presentationChanged();
        emit diagnosticsChanged();
        return;
    }
    sendLighting(m_resolution.desired);
    emit presentationChanged();
    emit diagnosticsChanged();
}

Lighting AppController::effectiveLighting() const
{
    return m_resolution.lighting;
}

void AppController::applyLighting()
{
    recompute();
}

void AppController::sendLighting(const DesiredLighting &desired)
{
    if (m_hasLastDesired && m_lastDesired == desired) {
        return;
    }
    m_rgb->setDesiredState(desired);
    m_lastDesired = desired;
    m_hasLastDesired = true;
    ++m_lightingUpdates;
    emit diagnosticsChanged();
}

void AppController::onInventoryChanged()
{
    emit inventoryChanged();
    emit diagnosticsChanged();
}

void AppController::rememberExternalContext()
{
    const ApplicationIdentity identity = m_context->identity();
    if (identity.isIdentified() && !isOwnSurface(identity)) {
        m_lastExternalApplication = friendlyApplicationName(identity);
    }
}

void AppController::refreshResolvedProfile()
{
    const ApplicationIdentity identity = m_context->identity();
    const ApplicationProfile *profile = matchApplication(m_document, identity);
    if (profile == nullptr && m_document.preferences.titleFallbackEnabled) {
        const std::optional<QString> caption = m_context->takeTitleHint();
        if (caption.has_value()) {
            const WorkspaceResolution resolution = resolveWorkspaceAssignment(m_document, identity, *caption);
            if (resolution.matchedByTitleFallback) {
                profile = resolution.profile;
            }
        }
    }
    m_resolvedProfileId = profile != nullptr ? profile->id : QString();
}

PlacementHintDecision AppController::placementHintFor(const ApplicationIdentity &identity,
                                                      const std::optional<QString> &caption) const
{
    const WorkspaceState observed = currentWorkspaceState();
    const PlacementDecision decision =
        resolvePlacementDecision(m_document, identity, observed, activeWorkspaceSession(), caption);
    PlacementHintDecision hint;
    hint.desktopId = decision.desktopId;
    hint.maximize = decision.maximize;
    return hint;
}

bool AppController::isOwnSurface(const ApplicationIdentity &identity) const
{
    const auto containsCi = [](const QString &value, const QString &needle) {
        return value.contains(needle, Qt::CaseInsensitive);
    };
    return containsCi(identity.desktopFileName, QStringLiteral("contextdeck"))
        || containsCi(identity.resourceClass, QStringLiteral("contextdeck"))
        || containsCi(identity.resourceName, QStringLiteral("contextdeck"))
        || containsCi(identity.desktopFileName, QStringLiteral("io.github.cisarik.ContextDeck"));
}

QString AppController::friendlyApplicationName(const ApplicationIdentity &identity) const
{
    QString raw;
    if (!identity.desktopFileName.isEmpty()) {
        raw = identity.desktopFileName;
    } else if (!identity.resourceClass.isEmpty()) {
        raw = identity.resourceClass;
    } else {
        raw = identity.resourceName;
    }
    if (raw.endsWith(QLatin1String(".desktop"))) {
        raw.chop(8);
    }
    const int dot = raw.lastIndexOf(QLatin1Char('.'));
    if (dot >= 0 && dot + 1 < raw.size()) {
        raw = raw.sliced(dot + 1);
    }
    if (!raw.isEmpty()) {
        raw[0] = raw[0].toUpper();
    }
    return raw;
}

QString AppController::openRgbPhrase() const
{
    switch (m_rgb->connectionState()) {
    case LightingConnectionState::Ready:
        return QStringLiteral("OpenRGB pripojený");
    case LightingConnectionState::Connecting:
    case LightingConnectionState::Negotiating:
        return QStringLiteral("OpenRGB sa pripája");
    case LightingConnectionState::Failed:
        return QStringLiteral("OpenRGB nedostupný");
    case LightingConnectionState::Disconnected:
        break;
    }
    return QStringLiteral("OpenRGB odpojený");
}

QString AppController::lightsPhrase() const
{
    if (m_sessionLighting == SessionLightingMode::TemporaryColor) {
        return QStringLiteral("dočasné pretíženie");
    }
    const Lighting lighting = effectiveLighting();
    switch (lighting.mode) {
    case LightingMode::Untouched: {
        QString restore = lightingModeJsonName(m_rgb->recordedRestoreMode());
        if (restore.isEmpty()) {
            restore = QStringLiteral("wave");
        }
        restore[0] = restore[0].toUpper();
        return QStringLiteral("firmware %1").arg(restore);
    }
    case LightingMode::Wave:
        return QStringLiteral("Wave");
    case LightingMode::Cycle:
        return QStringLiteral("Cycle");
    case LightingMode::Breathing:
        return QStringLiteral("Breathing");
    case LightingMode::Off:
        return QStringLiteral("vypnuté");
    case LightingMode::Direct:
        return QStringLiteral("vlastné farby");
    }
    return lightingModeJsonName(lighting.mode);
}

void AppController::speedBounds(LightingMode mode, quint32 &slowest, quint32 &fastest) const
{
    quint32 speedMin = 0;
    quint32 speedMax = 0;
    if (m_rgb->speedRangeFor(mode, speedMin, speedMax)) {
        slowest = speedMin;
        fastest = speedMax;
        return;
    }
    slowest = 0xC8;
    fastest = 0x0A;
}

quint32 AppController::percentToSpeed(int percent, LightingMode mode) const
{
    quint32 slowest = 0;
    quint32 fastest = 0;
    speedBounds(mode, slowest, fastest);
    const int clamped = std::clamp(percent, 0, 100);
    const qint64 span = static_cast<qint64>(fastest) - static_cast<qint64>(slowest);
    return static_cast<quint32>(static_cast<qint64>(slowest) + span * clamped / 100);
}

int AppController::speedToPercent(const Lighting &lighting) const
{
    if (!lighting.speed.has_value()) {
        return 50;
    }
    quint32 slowest = 0;
    quint32 fastest = 0;
    speedBounds(lighting.mode, slowest, fastest);
    if (slowest == fastest) {
        return 50;
    }
    const qint64 span = static_cast<qint64>(fastest) - static_cast<qint64>(slowest);
    const qint64 delta = static_cast<qint64>(*lighting.speed) - static_cast<qint64>(slowest);
    return std::clamp(static_cast<int>((delta * 100 + span / 2) / span), 0, 100);
}

bool AppController::applySpeedPercent(Lighting &lighting, int percent)
{
    if (percent < 0 || percent > 100) {
        return false;
    }
    LightingMode mode = lighting.mode;
    if (mode != LightingMode::Wave && mode != LightingMode::Cycle && mode != LightingMode::Breathing) {
        mode = LightingMode::Wave;
    }
    lighting.speed = percentToSpeed(percent, mode);
    return true;
}

bool AppController::applyBreathingColor(Lighting &lighting, const Rgb &color)
{
    lighting.baseColor = color;
    if (lighting.mode != LightingMode::Breathing) {
        lighting.mode = LightingMode::Breathing;
    }
    return true;
}

std::optional<Rgb> AppController::parseHex(const QString &hex)
{
    QString text = hex.trimmed();
    if (!text.startsWith(QLatin1Char('#'))) {
        text.prepend(QLatin1Char('#'));
    }
    if (text.size() != 7) {
        return std::nullopt;
    }
    bool ok = false;
    const int rgb = text.sliced(1).toInt(&ok, 16);
    if (!ok) {
        return std::nullopt;
    }
    Rgb color;
    color.r = static_cast<quint8>((rgb >> 16) & 0xff);
    color.g = static_cast<quint8>((rgb >> 8) & 0xff);
    color.b = static_cast<quint8>(rgb & 0xff);
    return color;
}

QString AppController::toHex(const Rgb &color)
{
    return QStringLiteral("#%1%2%3")
        .arg(color.r, 2, 16, QLatin1Char('0'))
        .arg(color.g, 2, 16, QLatin1Char('0'))
        .arg(color.b, 2, 16, QLatin1Char('0'));
}

QStringList AppController::previewGradient(const QString &startHex, const QString &endHex) const
{
    const auto start = parseHex(startHex);
    const auto end = parseHex(endHex);
    if (!start || !end) {
        return {};
    }
    QStringList list;
    for (const Rgb &color : gradientColors(*start, *end)) {
        list.push_back(toHex(color));
    }
    return list;
}

bool AppController::isValidHex(const QString &hex) const
{
    return parseHex(hex).has_value();
}

void AppController::armPassThrough()
{
    if (m_brokerIpc != nullptr) {
        m_brokerIpc->arm();
    }
}

void AppController::disarmPassThrough()
{
    if (m_brokerIpc != nullptr) {
        m_brokerIpc->disarm();
    }
}

void AppController::releaseBrokerLease()
{
    if (m_brokerIpc != nullptr) {
        m_brokerIpc->releaseLease();
    }
}

QString AppController::brokerIpcState() const
{
    if (m_brokerIpc == nullptr) {
        return QStringLiteral("disconnected");
    }
    return m_brokerIpc->stateText();
}

} // namespace contextdeck
