#include "workspace/ApplicationLauncher.h"

#include <KJob>
#include <KService>
#include <KIO/ApplicationLauncherJob>

namespace contextdeck {

QString workspaceLaunchOutcomeName(WorkspaceLaunchOutcome outcome)
{
    switch (outcome) {
    case WorkspaceLaunchOutcome::Launched:
        return QStringLiteral("launched");
    case WorkspaceLaunchOutcome::Failed:
        return QStringLiteral("failed");
    case WorkspaceLaunchOutcome::Disabled:
        return QStringLiteral("disabled");
    case WorkspaceLaunchOutcome::AlreadyRunning:
        return QStringLiteral("already_running");
    case WorkspaceLaunchOutcome::MissingDesktopFile:
        return QStringLiteral("missing_desktop_file");
    case WorkspaceLaunchOutcome::InvalidDesktopFile:
        return QStringLiteral("invalid_desktop_file");
    case WorkspaceLaunchOutcome::Debounced:
        return QStringLiteral("debounced");
    case WorkspaceLaunchOutcome::NotInTransaction:
        return QStringLiteral("not_in_transaction");
    }
    return QStringLiteral("unknown");
}

ApplicationLauncher::ApplicationLauncher(QObject *parent)
    : QObject(parent)
{
}

ApplicationLauncher::~ApplicationLauncher() = default;

void ApplicationLauncher::setInvokerForTest(LaunchInvoker invoker)
{
    m_invoker = std::move(invoker);
}

void ApplicationLauncher::beginTransaction(qint64 nowMs)
{
    m_debounce.beginTransaction(nowMs);
    m_failedBeforeWindow.clear();
    m_launchAttempts = 0;
    m_lastOutcome.clear();
}

void ApplicationLauncher::endTransaction()
{
    m_debounce.endTransaction();
    m_failedBeforeWindow.clear();
}

WorkspaceLaunchOutcome ApplicationLauncher::requestLaunch(const WorkspaceLaunchPlan &entry, qint64 nowMs)
{
    switch (entry.intent) {
    case WorkspaceLaunchIntent::Disabled:
        m_lastOutcome = workspaceLaunchOutcomeName(WorkspaceLaunchOutcome::Disabled);
        return WorkspaceLaunchOutcome::Disabled;
    case WorkspaceLaunchIntent::AlreadyRunning:
        m_lastOutcome = workspaceLaunchOutcomeName(WorkspaceLaunchOutcome::AlreadyRunning);
        return WorkspaceLaunchOutcome::AlreadyRunning;
    case WorkspaceLaunchIntent::MissingDesktopFile:
        m_lastOutcome = workspaceLaunchOutcomeName(WorkspaceLaunchOutcome::MissingDesktopFile);
        return WorkspaceLaunchOutcome::MissingDesktopFile;
    case WorkspaceLaunchIntent::WouldLaunch:
        break;
    }
    if (!m_debounce.isActive()) {
        m_lastOutcome = workspaceLaunchOutcomeName(WorkspaceLaunchOutcome::NotInTransaction);
        return WorkspaceLaunchOutcome::NotInTransaction;
    }
    if (entry.desktopFileId.isEmpty() || !workspaceDesktopIdLooksValid(entry.desktopFileId)) {
        m_lastOutcome = workspaceLaunchOutcomeName(WorkspaceLaunchOutcome::InvalidDesktopFile);
        return WorkspaceLaunchOutcome::InvalidDesktopFile;
    }
    if (!m_debounce.tryAttempt(entry.profileId, nowMs)) {
        m_lastOutcome = workspaceLaunchOutcomeName(WorkspaceLaunchOutcome::Debounced);
        return WorkspaceLaunchOutcome::Debounced;
    }
    return dispatch(entry, false);
}

WorkspaceLaunchOutcome ApplicationLauncher::retryForDesktopCreated(const WorkspaceLaunchPlan &entry, qint64 nowMs)
{
    if (entry.intent != WorkspaceLaunchIntent::WouldLaunch) {
        m_lastOutcome = workspaceLaunchOutcomeName(WorkspaceLaunchOutcome::Disabled);
        return WorkspaceLaunchOutcome::Disabled;
    }
    if (!m_debounce.isActive()) {
        m_lastOutcome = workspaceLaunchOutcomeName(WorkspaceLaunchOutcome::NotInTransaction);
        return WorkspaceLaunchOutcome::NotInTransaction;
    }
    if (entry.desktopFileId.isEmpty() || !workspaceDesktopIdLooksValid(entry.desktopFileId)) {
        m_lastOutcome = workspaceLaunchOutcomeName(WorkspaceLaunchOutcome::InvalidDesktopFile);
        return WorkspaceLaunchOutcome::InvalidDesktopFile;
    }
    if (!m_failedBeforeWindow.contains(entry.profileId) || !m_debounce.tryBoundedRetry(entry.profileId, nowMs)) {
        m_lastOutcome = workspaceLaunchOutcomeName(WorkspaceLaunchOutcome::Debounced);
        return WorkspaceLaunchOutcome::Debounced;
    }
    return dispatch(entry, true);
}

void ApplicationLauncher::noteJobFinished(const QString &profileId, bool failed)
{
    if (profileId.isEmpty()) {
        return;
    }
    if (failed) {
        m_failedBeforeWindow.insert(profileId);
        m_lastOutcome = workspaceLaunchOutcomeName(WorkspaceLaunchOutcome::Failed);
    }
}

bool ApplicationLauncher::hasAttempted(const QString &profileId) const
{
    return m_debounce.hasAttempted(profileId);
}

int ApplicationLauncher::attemptCount(const QString &profileId) const
{
    return m_debounce.attemptCount(profileId);
}

QString ApplicationLauncher::lastOutcomeName() const
{
    return m_lastOutcome;
}

WorkspaceLaunchOutcome ApplicationLauncher::dispatch(const WorkspaceLaunchPlan &entry, bool retry)
{
    Q_UNUSED(retry);
    ++m_launchAttempts;
    if (!startJob(entry.profileId, entry.desktopFileId)) {
        m_failedBeforeWindow.insert(entry.profileId);
        m_lastOutcome = workspaceLaunchOutcomeName(WorkspaceLaunchOutcome::Failed);
        return WorkspaceLaunchOutcome::Failed;
    }
    m_lastOutcome = workspaceLaunchOutcomeName(WorkspaceLaunchOutcome::Launched);
    return WorkspaceLaunchOutcome::Launched;
}

bool ApplicationLauncher::startJob(const QString &profileId, const QString &desktopFileId)
{
    if (m_invoker) {
        return m_invoker(desktopFileId);
    }
    const KService::Ptr service = KService::serviceByStorageId(desktopFileId);
    if (!service) {
        return false;
    }
    auto *job = new KIO::ApplicationLauncherJob(service, this);
    connect(job, &KJob::finished, this, [this, profileId](KJob *finishedJob) {
        noteJobFinished(profileId, finishedJob->error() != 0);
    });
    job->start();
    return true;
}

} // namespace contextdeck
