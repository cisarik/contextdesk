#include "app/PresentationModel.h"

#include "app/BrokerIpcClient.h"
#include "app/LightingEdit.h"
#include "app/ProfileDocumentEditor.h"
#include "app/WorkspaceApplyController.h"
#include "actions/PowerActions.h"
#include "context/ContextReceiver.h"
#include "context/DBusNames.h"
#include "context/WorkspaceReceiver.h"
#include "core/ControlCatalog.h"
#include "core/Resolver.h"
#include "core/ZoneMap.h"
#include "rgb/OpenRgbClient.h"

namespace contextdeck {

namespace {

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

} // namespace

PresentationModel::PresentationModel(State state)
    : m_state(state)
{
}

Lighting PresentationModel::effectiveLighting() const
{
    return m_state.resolution.lighting;
}

QString PresentationModel::lightingMode() const
{
    if (m_state.sessionLighting == SessionLightingMode::TemporaryColor) {
        return QStringLiteral("temporary:") + lightingModeJsonName(effectiveLighting().mode);
    }
    return lightingModeJsonName(effectiveLighting().mode);
}

QString PresentationModel::lightingLabel() const
{
    if (m_state.sessionLighting == SessionLightingMode::TemporaryColor) {
        return QStringLiteral("temporary override — %1").arg(lightingModeJsonName(effectiveLighting().mode));
    }
    if (m_state.sessionLighting == SessionLightingMode::LightsOff) {
        return QStringLiteral("off");
    }
    if (m_state.sessionLighting == SessionLightingMode::DeviceDefault
        || effectiveLighting().mode == LightingMode::Untouched) {
        return QStringLiteral("untouched — device default");
    }
    return lightingModeJsonName(effectiveLighting().mode);
}

QString PresentationModel::sessionLighting() const
{
    switch (m_state.sessionLighting) {
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

bool PresentationModel::temporaryOverrideActive() const
{
    return m_state.sessionLighting == SessionLightingMode::TemporaryColor;
}

QString PresentationModel::lightingConnection() const
{
    switch (m_state.rgb->connectionState()) {
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

QString PresentationModel::lastError() const
{
    if (!m_state.rgb->lastError().isEmpty()) {
        return m_state.rgb->lastError();
    }
    if (!m_state.context->lastError().isEmpty()) {
        return m_state.context->lastError();
    }
    return m_state.power->lastError();
}

bool PresentationModel::bridgeConnected() const
{
    return m_state.context->bridgeConnected();
}

bool PresentationModel::degraded() const
{
    return m_state.context->isDegraded();
}

QString PresentationModel::brokerIpcState() const
{
    if (m_state.brokerIpc == nullptr) {
        return QStringLiteral("disconnected");
    }
    return m_state.brokerIpc->stateText();
}

QString PresentationModel::globalColor() const
{
    return LightingEdit::toHex(LightingEdit::displayColor(m_state.document.globalLighting));
}

QString PresentationModel::globalMode() const
{
    return lightingModeJsonName(m_state.document.globalLighting.mode);
}

QStringList PresentationModel::globalZones() const
{
    return LightingEdit::zoneHexList(m_state.document.globalLighting);
}

QStringList PresentationModel::zoneNames() const
{
    QStringList names;
    for (const char *name : kZoneNames) {
        names.push_back(QString::fromUtf8(name));
    }
    return names;
}

QStringList PresentationModel::lightingPresets() const
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

QStringList PresentationModel::lightingPresetLabels() const
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

int PresentationModel::globalSpeedPercent() const
{
    return LightingEdit::speedToPercent(m_state.rgb, m_state.document.globalLighting);
}

QString PresentationModel::globalBreathingColor() const
{
    return LightingEdit::toHex(m_state.document.globalLighting.baseColor.value_or(kDefaultEffectColor));
}

QVariantList PresentationModel::globalZoneSlots() const
{
    QVariantList list;
    const Lighting &lighting = m_state.document.globalLighting;
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
        map.insert(QStringLiteral("color"), LightingEdit::toHex(color));
        map.insert(QStringLiteral("ordinal"), m_state.resolution.representedOrdinals[static_cast<size_t>(i)]);
        map.insert(QStringLiteral("contribution"),
                   slotContributionName(m_state.resolution.slotContributions[static_cast<size_t>(i)]));
        list.push_back(map);
    }
    return list;
}

QVariantList PresentationModel::inventory() const
{
    QVariantList list;
    for (const InventoryEntry &entry : m_state.context->inventory()) {
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

QVariantList PresentationModel::profiles() const
{
    QVariantList list;
    for (const ApplicationProfile &profile : m_state.document.applications) {
        QVariantMap map;
        map.insert(QStringLiteral("id"), profile.id);
        map.insert(QStringLiteral("displayName"), profile.displayName);
        map.insert(QStringLiteral("color"),
                   LightingEdit::toHex(LightingEdit::displayColor(
                       profile.lighting.value_or(m_state.document.globalLighting))));
        const Lighting lighting = profile.lighting.value_or(m_state.document.globalLighting);
        map.insert(QStringLiteral("mode"), lightingModeJsonName(lighting.mode));
        map.insert(QStringLiteral("zones"), LightingEdit::zoneHexList(lighting));
        map.insert(QStringLiteral("speedPercent"), LightingEdit::speedToPercent(m_state.rgb, lighting));
        map.insert(QStringLiteral("breathingColor"),
                   LightingEdit::toHex(lighting.baseColor.value_or(kDefaultEffectColor)));
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

QVariantList PresentationModel::controls() const
{
    QVariantList list;
    const ApplicationIdentity identity = m_state.context->identity();
    for (const ControlInfo &info : controlCatalog()) {
        const Assignment assignment = resolveAssignment(m_state.document, identity, info.id);
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

QVariantMap PresentationModel::diagnostics() const
{
    QVariantMap map;
    map.insert(QStringLiteral("bridgeConnected"), m_state.context->bridgeConnected());
    map.insert(QStringLiteral("degraded"), m_state.context->isDegraded());
    map.insert(QStringLiteral("policyRevision"), m_state.context->policyRevision());
    map.insert(QStringLiteral("lightingConnection"), lightingConnection());
    map.insert(QStringLiteral("lightingLabel"), lightingLabel());
    map.insert(QStringLiteral("sessionLighting"), sessionLighting());
    map.insert(QStringLiteral("lightingEnabled"), m_state.rgb->lightingEnabled());
    map.insert(QStringLiteral("hasG213"), m_state.rgb->hasG213());
    map.insert(QStringLiteral("identityUpdates"), QVariant::fromValue(m_state.identityUpdates));
    map.insert(QStringLiteral("lightingUpdates"), QVariant::fromValue(m_state.lightingUpdates));
    map.insert(QStringLiteral("lastError"), lastError());
    map.insert(QStringLiteral("inventoryCount"), m_state.context->inventory().size());
    map.insert(QStringLiteral("dbusService"), QString(kServiceName));
    map.insert(QStringLiteral("dbusObjectPath"), QString(kContextObjectPath));
    map.insert(QStringLiteral("dbusInterface"), QString(kContextInterface));
    map.insert(QStringLiteral("bridgeId"), m_state.context->bridgeId());
    map.insert(QStringLiteral("currentIdentity"), m_state.context->currentIdentity());
    map.insert(QStringLiteral("socketState"), m_state.rgb->socketStateText());
    map.insert(QStringLiteral("sdkEndpoint"), m_state.rgb->sdkEndpoint());
    map.insert(QStringLiteral("brokerIpcState"), brokerIpcState());
    map.insert(QStringLiteral("isSelfWindow"), isSelfWindow());
    map.insert(QStringLiteral("lastExternalApplication"), m_state.lastExternalApplication);
    map.insert(QStringLiteral("workspaceLayoutActive"), workspaceLayoutActive());
    map.insert(QStringLiteral("workspaceUnavailable"), m_state.resolution.workspaceUnavailable);
    map.insert(QStringLiteral("workspaceRefreshPending"),
               WorkspaceApplyController::workspaceStateOf(m_state.workspace).refreshPending);
    map.insert(QStringLiteral("workspaceDesktopCount"), m_state.resolution.desktopCount);
    map.insert(QStringLiteral("workspaceCurrentOrdinal"),
               WorkspaceApplyController::workspaceStateOf(m_state.workspace).currentOrdinal);
    map.insert(QStringLiteral("workspaceIndicatorCapacity"), m_state.resolution.indicatorCapacity);
    map.insert(QStringLiteral("workspaceOverflow"), m_state.resolution.overflowCount);
    map.insert(QStringLiteral("workspaceErrorClass"),
               m_state.workspace != nullptr ? m_state.workspace->errorClass() : QString());
    map.insert(QStringLiteral("workspacePaused"), workspaceObservationPaused());
    map.insert(QStringLiteral("workspaceSnapshots"),
               m_state.workspace != nullptr ? QVariant::fromValue(m_state.workspace->snapshotCount()) : 0);
    const QVariantMap workspacePlan = workspacePlanMap();
    map.insert(QStringLiteral("workspaceManagementEnabled"), m_state.document.preferences.workspaceManagementEnabled);
    map.insert(QStringLiteral("titleFallbackEnabled"), m_state.document.preferences.titleFallbackEnabled);
    map.insert(QStringLiteral("workspaceSessionCount"),
               static_cast<int>(m_state.document.workspaceSessions.size()));
    map.insert(QStringLiteral("workspacePlanSessionFound"), workspacePlan.value(QStringLiteral("sessionFound")));
    map.insert(QStringLiteral("workspacePlanDrift"), workspacePlan.value(QStringLiteral("drift")));
    map.insert(QStringLiteral("workspaceObservedRows"), workspaceObserved().value(QStringLiteral("rows")));
    map.insert(QStringLiteral("workspaceObservedWrapping"), workspaceObserved().value(QStringLiteral("wrapping")));
    map.insert(QStringLiteral("workspaceApplyRunning"), m_state.apply.workspaceApplyRunning());
    map.insert(QStringLiteral("workspaceApplyStatus"), m_state.apply.workspaceApplyStatus());
    map.insert(QStringLiteral("workspaceCheckpointAvailable"), m_state.apply.workspaceCheckpointAvailable());
    map.insert(QStringLiteral("workspaceLastResidual"), m_state.apply.workspaceLastResidual());
    map.insert(QStringLiteral("workspaceLastMutationCreated"), m_state.apply.workspaceLastMutationCreated());
    map.insert(QStringLiteral("workspaceLastMutationRemoved"), m_state.apply.workspaceLastMutationRemoved());
    map.insert(QStringLiteral("workspaceLastApplyReverted"), m_state.apply.workspaceLastApplyReverted());
    map.insert(QStringLiteral("workspaceLaunchAttempts"), m_state.apply.launchAttemptsInTransaction());
    return map;
}

QString PresentationModel::statusSummary() const
{
    return QStringLiteral("%1 · kontext: %2 · svetlá: %3")
        .arg(openRgbPhrase(), contextDisplayName(), lightsPhrase());
}

bool PresentationModel::isSelfWindow() const
{
    return isOwnSurface(m_state.context->identity());
}

QString PresentationModel::lastExternalApplication() const
{
    return m_state.lastExternalApplication;
}

QString PresentationModel::contextDisplayName() const
{
    if (isSelfWindow()) {
        if (!m_state.lastExternalApplication.isEmpty()) {
            return QStringLiteral("Posledná aplikácia: %1").arg(m_state.lastExternalApplication);
        }
        return QStringLiteral("ContextDeck (toto okno)");
    }
    const ApplicationIdentity identity = m_state.context->identity();
    if (!identity.isIdentified()) {
        return QStringLiteral("neidentifikovaný");
    }
    return friendlyApplicationName(identity);
}

bool PresentationModel::hasSavedProfiles() const
{
    if (!m_state.document.applications.isEmpty()) {
        return true;
    }
    return m_state.document.globalLighting.mode != LightingMode::Untouched;
}

QString PresentationModel::heroKind() const
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

QString PresentationModel::heroBadge() const
{
    const Lighting lighting = effectiveLighting();
    if (m_state.sessionLighting == SessionLightingMode::TemporaryColor) {
        return QStringLiteral("Temporary override");
    }
    if (lighting.mode == LightingMode::Untouched) {
        QString restore = lightingModeJsonName(m_state.rgb->recordedRestoreMode());
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

QStringList PresentationModel::heroZones() const
{
    QStringList list;
    for (const Rgb &color : m_state.resolution.previewColors) {
        list.push_back(LightingEdit::toHex(color));
    }
    if (list.size() != kZoneCount) {
        return LightingEdit::zoneHexList(effectiveLighting());
    }
    return list;
}

bool PresentationModel::workspaceLayoutActive() const
{
    return m_state.resolution.workspaceLayoutActive;
}

QString PresentationModel::workspaceSummary() const
{
    if (!m_state.resolution.workspaceLayoutActive) {
        return QStringLiteral("Workspace layout is inactive.");
    }
    if (m_state.resolution.workspaceUnavailable) {
        return QStringLiteral("Workspace observation unavailable — desired fallback is the recorded device default. This is not physical readback.");
    }
    if (WorkspaceApplyController::workspaceStateOf(m_state.workspace).refreshPending) {
        return QStringLiteral("Refreshing desktop snapshot; last desired preview is retained.");
    }
    QString text = QStringLiteral("Desired preview: desktop %1 of %2, indicator capacity %3")
                       .arg(WorkspaceApplyController::workspaceStateOf(m_state.workspace).currentOrdinal)
                       .arg(m_state.resolution.desktopCount)
                       .arg(m_state.resolution.indicatorCapacity);
    if (m_state.resolution.overflowCount > 0) {
        text += QStringLiteral(". Overflow: %1 desktops are not represented.").arg(m_state.resolution.overflowCount);
    }
    if (m_state.resolution.currentDesktopUnrepresented) {
        text += QStringLiteral(" Current desktop is not represented.");
    }
    return text;
}

bool PresentationModel::workspaceObservationPaused() const
{
    return m_state.workspace != nullptr && m_state.workspace->isPaused();
}

QVariantMap PresentationModel::workspaceObserved() const
{
    const WorkspaceState state = WorkspaceApplyController::workspaceStateOf(m_state.workspace);
    QVariantMap map;
    map.insert(QStringLiteral("available"), state.availability == WorkspaceAvailability::Available);
    map.insert(QStringLiteral("refreshPending"), state.refreshPending);
    map.insert(QStringLiteral("count"), static_cast<int>(state.desktops.size()));
    map.insert(QStringLiteral("currentOrdinal"), state.currentOrdinal);
    map.insert(QStringLiteral("rows"), state.rows.has_value() ? QVariant(*state.rows) : QVariant());
    map.insert(QStringLiteral("wrapping"),
               state.navigationWrappingAround.has_value() ? QVariant(*state.navigationWrappingAround) : QVariant());
    map.insert(QStringLiteral("paused"), workspaceObservationPaused());
    map.insert(QStringLiteral("errorClass"),
               m_state.workspace != nullptr ? m_state.workspace->errorClass() : QString());
    return map;
}

QVariantMap PresentationModel::workspacePlanPreview() const
{
    return workspacePlanMap();
}

QVariantMap PresentationModel::workspacePlanMap() const
{
    const WorkspaceState observed = WorkspaceApplyController::workspaceStateOf(m_state.workspace);
    const WorkspacePlan plan = m_state.apply.planForState(observed);
    m_state.apply.rememberPreview(plan, observed);
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

QString PresentationModel::openRgbPhrase() const
{
    switch (m_state.rgb->connectionState()) {
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

QString PresentationModel::lightsPhrase() const
{
    if (m_state.sessionLighting == SessionLightingMode::TemporaryColor) {
        return QStringLiteral("dočasné pretíženie");
    }
    const Lighting lighting = effectiveLighting();
    switch (lighting.mode) {
    case LightingMode::Untouched: {
        QString restore = lightingModeJsonName(m_state.rgb->recordedRestoreMode());
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

bool PresentationModel::isOwnSurface(const ApplicationIdentity &identity)
{
    const auto containsCi = [](const QString &value, const QString &needle) {
        return value.contains(needle, Qt::CaseInsensitive);
    };
    return containsCi(identity.desktopFileName, QStringLiteral("contextdeck"))
        || containsCi(identity.resourceClass, QStringLiteral("contextdeck"))
        || containsCi(identity.resourceName, QStringLiteral("contextdeck"))
        || containsCi(identity.desktopFileName, QStringLiteral("io.github.cisarik.ContextDeck"));
}

QString PresentationModel::friendlyApplicationName(const ApplicationIdentity &identity)
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

} // namespace contextdeck
