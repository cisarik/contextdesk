#include "app/AppController.h"

#include "app/BrokerIpcClient.h"
#include "app/LightingEdit.h"
#include "context/DBusNames.h"
#include "context/WorkspaceReceiver.h"
#include "core/ControlCatalog.h"
#include "core/Resolver.h"
#include "core/ZoneMap.h"
#include "workspace/PlacementResolver.h"
#include "workspace/WorkspacePlan.h"

#include <QDateTime>
#include <QMetaObject>
#include <QSet>
#include <QStringList>

#include <algorithm>

namespace contextdeck {

Q_LOGGING_CATEGORY(lcUi, "contextdeck.ui")

AppController::AppController(ContextReceiver *context, OpenRgbClient *rgb, PowerActions *power, QObject *parent,
                             QString configRoot)
    : QObject(parent)
    , m_context(context)
    , m_rgb(rgb)
    , m_power(power)
    , m_store(std::move(configRoot))
    , m_editor(m_store, m_document, m_context, m_rgb, m_sessionLighting, m_temporaryColor)
    , m_sessions(m_document, m_context)
    , m_apply(m_document, m_context, m_store.configRoot())
    , m_presentation({m_context, m_workspace, m_rgb, m_power, m_brokerIpc, m_document, m_resolution,
                      m_sessionLighting, m_lastExternalApplication, m_identityUpdates, m_lightingUpdates, m_apply})
{
    m_document.globalLighting = LightingEdit::defaultLighting();
    connect(m_context, &ContextReceiver::currentIdentityChanged, this, &AppController::onContextInputsChanged);
    connect(m_context, &ContextReceiver::bridgeLost, this, &AppController::onContextInputsChanged);
    connect(m_context, &ContextReceiver::bridgeConnectedChanged, this, &AppController::contextChanged);
    connect(m_context, &ContextReceiver::inventoryChanged, this, &AppController::onInventoryChanged);
    connect(m_context, &ContextReceiver::degradedChanged, this, &AppController::contextChanged);
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
    connect(&m_editor, &ProfileDocumentEditor::documentChanged, this, &AppController::documentChanged);
    connect(&m_editor, &ProfileDocumentEditor::lightingModeChanged, this, &AppController::lightingModeChanged);
    connect(&m_editor, &ProfileDocumentEditor::applyLightingRequested, this, &AppController::applyLighting);
    connect(&m_editor, &ProfileDocumentEditor::resolvedProfileInvalidated, this, &AppController::refreshResolvedProfile);
    connect(&m_sessions, &WorkspaceSessionEditor::documentChanged, this, &AppController::documentChanged);
    connect(&m_sessions, &WorkspaceSessionEditor::presentationChanged, this, &AppController::presentationChanged);
    connect(&m_apply, &WorkspaceApplyController::presentationChanged, this, &AppController::presentationChanged);
    connect(&m_apply, &WorkspaceApplyController::diagnosticsChanged, this, &AppController::diagnosticsChanged);
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
    m_apply.setWorkspaceReceiver(m_workspace);
}

void AppController::load()
{
    const LoadOutcome loaded = m_store.load();
    if (loaded.ok && !loaded.missing) {
        m_document = loaded.document;
        qCInfo(lcUi) << "loaded profile document";
    } else if (loaded.missing) {
        m_document = ProfileDocument{};
        m_document.globalLighting = LightingEdit::defaultLighting();
        qCInfo(lcUi) << "cold start: no profile document, pass-through, writing nothing";
    } else {
        qCWarning(lcUi) << "profile load refused:" << loaded.error.reason << loaded.error.jsonPath
                        << "preserved=" << loaded.error.preserved;
        m_document = ProfileDocument{};
        m_document.globalLighting = LightingEdit::defaultLighting();
    }
    if (m_document.preferences.automaticEnabled) {
        m_sessionLighting = SessionLightingMode::Automatic;
    }
    m_context->setTitleFallbackEnabled(m_document.preferences.titleFallbackEnabled);
    m_apply.resetApplyState();
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
    return m_presentation.lightingMode();
}

QString AppController::lightingLabel() const
{
    return m_presentation.lightingLabel();
}

QString AppController::sessionLighting() const
{
    return m_presentation.sessionLighting();
}

bool AppController::temporaryOverrideActive() const
{
    return m_presentation.temporaryOverrideActive();
}

QString AppController::lightingConnection() const
{
    return m_presentation.lightingConnection();
}

QString AppController::lastError() const
{
    return m_presentation.lastError();
}

bool AppController::bridgeConnected() const
{
    return m_presentation.bridgeConnected();
}

bool AppController::degraded() const
{
    return m_presentation.degraded();
}

QString AppController::globalColor() const
{
    return m_presentation.globalColor();
}

QString AppController::globalMode() const
{
    return m_presentation.globalMode();
}

QStringList AppController::globalZones() const
{
    return m_presentation.globalZones();
}

QStringList AppController::zoneNames() const
{
    return m_presentation.zoneNames();
}

QStringList AppController::lightingPresets() const
{
    return m_presentation.lightingPresets();
}

QStringList AppController::lightingPresetLabels() const
{
    return m_presentation.lightingPresetLabels();
}

int AppController::globalSpeedPercent() const
{
    return m_presentation.globalSpeedPercent();
}

QString AppController::globalBreathingColor() const
{
    return m_presentation.globalBreathingColor();
}

QString AppController::statusSummary() const
{
    return m_presentation.statusSummary();
}

bool AppController::isSelfWindow() const
{
    return m_presentation.isSelfWindow();
}

QString AppController::lastExternalApplication() const
{
    return m_presentation.lastExternalApplication();
}

QString AppController::contextDisplayName() const
{
    return m_presentation.contextDisplayName();
}

bool AppController::hasSavedProfiles() const
{
    return m_presentation.hasSavedProfiles();
}

QString AppController::heroKind() const
{
    return m_presentation.heroKind();
}

QString AppController::heroBadge() const
{
    return m_presentation.heroBadge();
}

QStringList AppController::heroZones() const
{
    return m_presentation.heroZones();
}

bool AppController::workspaceLayoutActive() const
{
    return m_presentation.workspaceLayoutActive();
}

QString AppController::workspaceSummary() const
{
    return m_presentation.workspaceSummary();
}

bool AppController::workspaceObservationPaused() const
{
    return m_presentation.workspaceObservationPaused();
}

QVariantList AppController::globalZoneSlots() const
{
    return m_presentation.globalZoneSlots();
}

bool AppController::workspaceManagementEnabled() const
{
    return m_sessions.workspaceManagementEnabled();
}

bool AppController::titleFallbackEnabled() const
{
    return m_sessions.titleFallbackEnabled();
}

QString AppController::activeWorkspaceSession() const
{
    return m_sessions.activeWorkspaceSession();
}

QVariantList AppController::workspaceSessions() const
{
    return m_sessions.workspaceSessions();
}

QVariantList AppController::workspaceSessionOptions() const
{
    return m_sessions.workspaceSessionOptions();
}

QVariantList AppController::workspaceDesktopEntries() const
{
    return m_sessions.workspaceDesktopEntries();
}

QVariantMap AppController::workspaceObserved() const
{
    return m_presentation.workspaceObserved();
}

QVariantMap AppController::workspacePlanPreview() const
{
    return m_presentation.workspacePlanPreview();
}

bool AppController::workspaceApplyAvailable() const
{
    return m_apply.workspaceApplyAvailable();
}

bool AppController::workspaceCheckpointAvailable() const
{
    return m_apply.workspaceCheckpointAvailable();
}

QVariantList AppController::inventory() const
{
    return m_presentation.inventory();
}

QVariantList AppController::profiles() const
{
    return m_presentation.profiles();
}

QVariantList AppController::controls() const
{
    return m_presentation.controls();
}

QVariantMap AppController::diagnostics() const
{
    return m_presentation.diagnostics();
}

bool AppController::save()
{
    return m_editor.save();
}

void AppController::setGlobalColor(const QString &hex)
{
    m_editor.setGlobalColor(hex);
}

void AppController::setApplicationColor(const QString &id, const QString &hex)
{
    m_editor.setApplicationColor(id, hex);
}

void AppController::addProfileFromInventory(int index)
{
    m_editor.addProfileFromInventory(index);
}

void AppController::removeProfile(const QString &id)
{
    m_editor.removeProfile(id);
}

void AppController::setAutomatic(bool enabled)
{
    m_editor.setAutomatic(enabled);
}

void AppController::lightsOff()
{
    m_editor.lightsOff();
}

void AppController::restoreAutomatic()
{
    m_editor.restoreAutomatic();
}

void AppController::restoreDeviceDefault()
{
    m_editor.restoreDeviceDefault();
}

void AppController::setTemporaryColor(const QString &hex)
{
    m_editor.setTemporaryColor(hex);
}

void AppController::setGlobalLightingMode(const QString &modeName)
{
    m_editor.setGlobalLightingMode(modeName);
}

void AppController::setGlobalZoneColor(int index, const QString &hex)
{
    m_editor.setGlobalZoneColor(index, hex);
}

void AppController::applyGlobalGradient(const QString &startHex, const QString &endHex)
{
    m_editor.applyGlobalGradient(startHex, endHex);
}

void AppController::setGlobalZoneRole(int index, const QString &roleName)
{
    m_editor.setGlobalZoneRole(index, roleName);
}

void AppController::useDefaultWorkspaceLayout()
{
    m_editor.useDefaultWorkspaceLayout();
}

void AppController::useStaticZoneLayout()
{
    m_editor.useStaticZoneLayout();
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

void AppController::setWorkspaceManagementEnabled(bool enabled)
{
    m_sessions.setWorkspaceManagementEnabled(enabled);
}

void AppController::setTitleFallbackEnabled(bool enabled)
{
    m_sessions.setTitleFallbackEnabled(enabled);
}

void AppController::setActiveWorkspaceSession(const QString &id)
{
    m_sessions.setActiveWorkspaceSession(id);
}

void AppController::addWorkspaceSession(const QString &displayName)
{
    m_sessions.addWorkspaceSession(displayName);
}

void AppController::removeWorkspaceSession(const QString &id)
{
    m_sessions.removeWorkspaceSession(id);
}

void AppController::renameWorkspaceSession(const QString &id, const QString &displayName)
{
    m_sessions.renameWorkspaceSession(id, displayName);
}

void AppController::setWorkspaceSessionRows(const QString &id, int rows)
{
    m_sessions.setWorkspaceSessionRows(id, rows);
}

void AppController::setWorkspaceSessionWrapping(const QString &id, bool enabled)
{
    m_sessions.setWorkspaceSessionWrapping(id, enabled);
}

void AppController::setWorkspaceSessionDesktopCount(const QString &id, int count)
{
    m_sessions.setWorkspaceSessionDesktopCount(id, count);
}

void AppController::setWorkspaceSessionDesktopName(const QString &id, int ordinal, const QString &name)
{
    m_sessions.setWorkspaceSessionDesktopName(id, ordinal, name);
}

bool AppController::applyWorkspaceSession(bool switchCurrent, bool removeExtras)
{
    return m_apply.applyWorkspaceSession(switchCurrent, removeExtras);
}

bool AppController::revertWorkspaceApply()
{
    return m_apply.revertWorkspaceApply();
}

void AppController::setApplicationWorkspaceSession(const QString &id, const QString &sessionId)
{
    m_sessions.setApplicationWorkspaceSession(id, sessionId);
}

void AppController::setApplicationWorkspaceDesktop(const QString &id, int ordinal)
{
    m_sessions.setApplicationWorkspaceDesktop(id, ordinal);
}

void AppController::setApplicationWorkspaceLaunch(const QString &id, bool launch)
{
    m_sessions.setApplicationWorkspaceLaunch(id, launch);
}

void AppController::setApplicationWorkspaceMaximize(const QString &id, bool maximize)
{
    m_sessions.setApplicationWorkspaceMaximize(id, maximize);
}

void AppController::setApplicationWorkspaceLaunchFile(const QString &id, const QString &desktopFile)
{
    m_sessions.setApplicationWorkspaceLaunchFile(id, desktopFile);
}

void AppController::setApplicationTitleFallback(const QString &id, bool enabled, const QString &mode,
                                                const QString &pattern)
{
    m_sessions.setApplicationTitleFallback(id, enabled, mode, pattern);
}

void AppController::setGlobalSpeed(int percent)
{
    m_editor.setGlobalSpeed(percent);
}

void AppController::setGlobalBreathingColor(const QString &hex)
{
    m_editor.setGlobalBreathingColor(hex);
}

void AppController::setApplicationLightingMode(const QString &id, const QString &modeName)
{
    m_editor.setApplicationLightingMode(id, modeName);
}

void AppController::setApplicationZoneColor(const QString &id, int index, const QString &hex)
{
    m_editor.setApplicationZoneColor(id, index, hex);
}

void AppController::applyApplicationGradient(const QString &id, const QString &startHex, const QString &endHex)
{
    m_editor.applyApplicationGradient(id, startHex, endHex);
}

void AppController::setApplicationSpeed(const QString &id, int percent)
{
    m_editor.setApplicationSpeed(id, percent);
}

void AppController::setApplicationBreathingColor(const QString &id, const QString &hex)
{
    m_editor.setApplicationBreathingColor(id, hex);
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
    m_editor.assignEmitShortcut(controlName, key, modifiers, applicationLevel, applicationId);
}

QStringList AppController::previewGradient(const QString &startHex, const QString &endHex) const
{
    const auto start = LightingEdit::parseHex(startHex);
    const auto end = LightingEdit::parseHex(endHex);
    if (!start || !end) {
        return {};
    }
    QStringList list;
    for (const Rgb &color : gradientColors(*start, *end)) {
        list.push_back(LightingEdit::toHex(color));
    }
    return list;
}

bool AppController::isValidHex(const QString &hex) const
{
    return LightingEdit::parseHex(hex).has_value();
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
    return m_presentation.brokerIpcState();
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
            if (m_sessionLighting == SessionLightingMode::TemporaryColor
                && !PresentationModel::isOwnSurface(identity) && identity.isIdentified()) {
                m_editor.expireTemporaryColor();
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
    return WorkspaceApplyController::workspaceStateOf(m_workspace);
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
    if (identity.isIdentified() && !PresentationModel::isOwnSurface(identity)) {
        m_lastExternalApplication = PresentationModel::friendlyApplicationName(identity);
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
        resolvePlacementDecision(m_document, identity, observed, m_sessions.activeWorkspaceSession(), caption);
    PlacementHintDecision hint;
    hint.desktopId = decision.desktopId;
    hint.maximize = decision.maximize;
    return hint;
}

} // namespace contextdeck
