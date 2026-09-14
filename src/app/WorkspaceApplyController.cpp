#include "app/WorkspaceApplyController.h"

#include "context/ContextReceiver.h"
#include "context/WorkspaceReceiver.h"

#include <QDateTime>

namespace contextdeck {

WorkspaceApplyController::WorkspaceApplyController(const ProfileDocument &document, ContextReceiver *context,
                                                   const QString &configRoot, QObject *parent)
    : QObject(parent)
    , m_document(document)
    , m_context(context)
{
    m_mutator.setCheckpointPath(configRoot + QStringLiteral("/contextdeck/workspace-checkpoint.json"));
    connect(&m_mutator, &DesktopMutator::desktopCreatedInTransaction,
            this, &WorkspaceApplyController::onDesktopCreatedInTransaction);
}

void WorkspaceApplyController::setWorkspaceReceiver(WorkspaceReceiver *receiver)
{
    m_workspace = receiver;
    if (m_workspace == nullptr) {
        return;
    }
    connect(m_workspace, &WorkspaceReceiver::desktopCreatedObserved,
            this, &WorkspaceApplyController::onWorkspaceDesktopCreated);
}

bool WorkspaceApplyController::workspaceApplyAvailable() const
{
    if (m_applyRunning) {
        return false;
    }
    if (!m_document.preferences.workspaceManagementEnabled) {
        return false;
    }
    if (findWorkspaceSession(m_document, m_document.preferences.activeWorkspaceSessionId.value_or(QString()))
        == nullptr) {
        return false;
    }
    if (m_workspace == nullptr) {
        return false;
    }
    const WorkspaceState state = workspaceStateOf(m_workspace);
    if (state.availability != WorkspaceAvailability::Available || state.refreshPending) {
        return false;
    }
    return m_workspace->activeLogicalRequestId() == 0;
}

bool WorkspaceApplyController::workspaceCheckpointAvailable() const
{
    return m_mutator.checkpointExists();
}

WorkspacePlan WorkspaceApplyController::planForState(const WorkspaceState &observed) const
{
    return computeWorkspacePlan(m_document, m_document.preferences.activeWorkspaceSessionId.value_or(QString()),
                                observed, openWindowIdentities());
}

void WorkspaceApplyController::rememberPreview(const WorkspacePlan &plan, const WorkspaceState &observed) const
{
    m_lastPlan = plan;
    m_hasLastPlan = true;
    m_previewFingerprint = workspacePreviewFingerprint(plan, observed);
    m_previewOwnerGeneration = m_workspace != nullptr ? m_workspace->ownerGeneration() : 0;
}

QVector<ApplicationIdentity> WorkspaceApplyController::openWindowIdentities() const
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

bool WorkspaceApplyController::applyWorkspaceSession(bool switchCurrent, bool removeExtras)
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
    const QString sessionId = m_document.preferences.activeWorkspaceSessionId.value_or(QString());
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
    const WorkspacePlan plan = planForState(observed);
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
    refreshPreview();
    emit presentationChanged();
    emit diagnosticsChanged();
    return result.ok;
}

bool WorkspaceApplyController::revertWorkspaceApply()
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
    refreshPreview();
    emit presentationChanged();
    emit diagnosticsChanged();
    return result.ok;
}

void WorkspaceApplyController::resetApplyState()
{
    m_applyStatus.clear();
    m_applyResidual.clear();
    m_lastApplyReverted = false;
    m_lastApplyCreated = 0;
    m_lastApplyRemoved = 0;
}

void WorkspaceApplyController::refreshPreview()
{
    const WorkspaceState observed = workspaceStateOf(m_workspace);
    const WorkspacePlan plan = planForState(observed);
    rememberPreview(plan, observed);
}

void WorkspaceApplyController::onWorkspaceDesktopCreated(const QString &id, int position)
{
    if (!m_applyRunning) {
        return;
    }
    m_mutator.recordCreatedDesktop(id, position);
}

void WorkspaceApplyController::onDesktopCreatedInTransaction(const QString &id, int position)
{
    Q_UNUSED(id);
    if (!m_applyRunning) {
        return;
    }
    launchProfilesForOrdinal(position + 1);
}

void WorkspaceApplyController::launchPlannedProfiles(const WorkspacePlan &plan)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const WorkspaceLaunchPlan &entry : plan.launches) {
        if (entry.intent != WorkspaceLaunchIntent::WouldLaunch || m_launcher.hasAttempted(entry.profileId)) {
            continue;
        }
        (void)m_launcher.requestLaunch(entry, now);
    }
}

void WorkspaceApplyController::launchProfilesForOrdinal(int ordinal)
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

QString WorkspaceApplyController::workspacePreviewFingerprint(const WorkspacePlan &plan, const WorkspaceState &state)
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

WorkspaceState WorkspaceApplyController::workspaceStateOf(const WorkspaceReceiver *workspace)
{
    if (workspace == nullptr) {
        return WorkspaceState{};
    }
    return workspace->state();
}

} // namespace contextdeck
