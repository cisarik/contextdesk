#include "workspace/WorkspacePlan.h"

#include "core/Resolver.h"

namespace contextdeck {

const WorkspaceSession *findWorkspaceSession(const ProfileDocument &document, const QString &sessionId)
{
    if (sessionId.isEmpty()) {
        return nullptr;
    }
    for (const WorkspaceSession &session : document.workspaceSessions) {
        if (session.id == sessionId) {
            return &session;
        }
    }
    return nullptr;
}

WorkspacePlan computeWorkspacePlan(const ProfileDocument &document, const QString &sessionId,
                                   const WorkspaceState &observed,
                                   const QVector<ApplicationIdentity> &openWindows)
{
    WorkspacePlan plan;
    plan.sessionId = sessionId;
    plan.managementEnabled = document.preferences.workspaceManagementEnabled;
    const WorkspaceSession *session = findWorkspaceSession(document, sessionId);
    if (session == nullptr) {
        return plan;
    }
    plan.sessionFound = true;
    plan.desiredDesktopCount = static_cast<int>(session->desktops.size());
    plan.desiredRows = session->rows;
    plan.desiredWrapping = session->navigationWrapping;
    plan.observationAvailable = observed.availability == WorkspaceAvailability::Available;
    if (plan.observationAvailable) {
        plan.observedDesktopCount = static_cast<int>(observed.desktops.size());
        plan.observedRows = observed.rows;
        plan.observedWrapping = observed.navigationWrappingAround;
    }

    for (const WorkspaceDesktopEntry &desired : session->desktops) {
        WorkspaceDesktopPlan entry;
        entry.ordinal = desired.ordinal;
        entry.name = desired.name;
        const WorkspaceDesktop *live = nullptr;
        if (plan.observationAvailable) {
            for (const WorkspaceDesktop &candidate : observed.desktops) {
                if (candidate.ordinal == desired.ordinal) {
                    live = &candidate;
                    break;
                }
            }
        }
        if (live == nullptr) {
            entry.create = true;
        } else {
            entry.observedName = live->displayName;
            entry.rename = live->displayName != desired.name;
        }
        plan.desktops.push_back(entry);
    }

    plan.extraDesktop = plan.observationAvailable
        && plan.observedDesktopCount > plan.desiredDesktopCount;
    if (session->rows && observed.rows) {
        plan.rowsChange = *session->rows != *observed.rows;
    }
    if (session->navigationWrapping && observed.navigationWrappingAround) {
        plan.wrappingChange = *session->navigationWrapping != *observed.navigationWrappingAround;
    }

    for (const ApplicationProfile &profile : document.applications) {
        if (!profile.workspace || profile.workspace->sessionId != sessionId) {
            continue;
        }
        WorkspaceLaunchPlan launch;
        launch.profileId = profile.id;
        launch.displayName = profile.displayName;
        launch.desktopOrdinal = profile.workspace->desktopOrdinal;
        launch.maximize = profile.workspace->maximize;
        launch.intent = WorkspaceLaunchIntent::Disabled;
        if (plan.managementEnabled && profile.workspace->launch) {
            bool running = false;
            for (const ApplicationIdentity &identity : openWindows) {
                if (applicationMatches(profile, identity)) {
                    running = true;
                    break;
                }
            }
            std::optional<QString> desktopFile = profile.workspace->launchDesktopFile;
            if (!desktopFile && profile.match.desktopFileName
                && workspaceDesktopIdLooksValid(*profile.match.desktopFileName)) {
                desktopFile = *profile.match.desktopFileName;
            }
            if (desktopFile.has_value()) {
                launch.desktopFileId = *desktopFile;
            }
            if (running) {
                launch.intent = WorkspaceLaunchIntent::AlreadyRunning;
            } else {
                launch.intent = desktopFile.has_value() ? WorkspaceLaunchIntent::WouldLaunch
                                                        : WorkspaceLaunchIntent::MissingDesktopFile;
            }
        }
        plan.launches.push_back(launch);
    }

    plan.drift = plan.extraDesktop || plan.rowsChange || plan.wrappingChange;
    for (const WorkspaceDesktopPlan &entry : plan.desktops) {
        if (entry.create || entry.rename) {
            plan.drift = true;
            break;
        }
    }
    return plan;
}

bool workspaceEventIsLaunchTrigger(WorkspaceEventKind kind, bool transactionActive)
{
    switch (kind) {
    case WorkspaceEventKind::ApplySession:
        return true;
    case WorkspaceEventKind::DesktopCreated:
        return transactionActive;
    case WorkspaceEventKind::SessionLogin:
    case WorkspaceEventKind::SessionAppStart:
    case WorkspaceEventKind::CurrentDesktopChanged:
    case WorkspaceEventKind::UserDesktopChange:
    case WorkspaceEventKind::BrokerEvent:
        return false;
    }
    return false;
}

QString workspaceLaunchIntentName(WorkspaceLaunchIntent intent)
{
    switch (intent) {
    case WorkspaceLaunchIntent::Disabled:
        return QStringLiteral("disabled");
    case WorkspaceLaunchIntent::WouldLaunch:
        return QStringLiteral("would_launch");
    case WorkspaceLaunchIntent::AlreadyRunning:
        return QStringLiteral("already_running");
    case WorkspaceLaunchIntent::MissingDesktopFile:
        return QStringLiteral("missing_desktop_file");
    }
    return QStringLiteral("disabled");
}

void WorkspaceLaunchDebounce::beginTransaction(qint64 nowMs)
{
    Q_UNUSED(nowMs);
    m_active = true;
    m_lastAttemptMs.clear();
    m_attempts.clear();
}

void WorkspaceLaunchDebounce::endTransaction()
{
    m_active = false;
    m_lastAttemptMs.clear();
    m_attempts.clear();
}

bool WorkspaceLaunchDebounce::tryAttempt(const QString &profileId, qint64 nowMs)
{
    if (!m_active || profileId.isEmpty()) {
        return false;
    }
    const int attempts = m_attempts.value(profileId, 0);
    if (attempts >= kMaximumAttemptsPerProfile) {
        return false;
    }
    const auto last = m_lastAttemptMs.constFind(profileId);
    if (last != m_lastAttemptMs.constEnd() && nowMs - last.value() < kMinimumIntervalMs) {
        return false;
    }
    m_lastAttemptMs.insert(profileId, nowMs);
    m_attempts.insert(profileId, attempts + 1);
    return true;
}

bool WorkspaceLaunchDebounce::tryBoundedRetry(const QString &profileId, qint64 nowMs)
{
    if (!m_active || profileId.isEmpty()) {
        return false;
    }
    const int attempts = m_attempts.value(profileId, 0);
    if (attempts != 1) {
        return false;
    }
    m_lastAttemptMs.insert(profileId, nowMs);
    m_attempts.insert(profileId, attempts + 1);
    return true;
}

bool WorkspaceLaunchDebounce::hasAttempted(const QString &profileId) const
{
    return m_attempts.contains(profileId);
}

int WorkspaceLaunchDebounce::attemptCount(const QString &profileId) const
{
    return m_attempts.value(profileId, 0);
}

} // namespace contextdeck
