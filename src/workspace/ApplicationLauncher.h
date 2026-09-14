#pragma once

#include "core/Types.h"
#include "workspace/WorkspacePlan.h"

#include <QObject>
#include <QSet>
#include <QString>

#include <functional>

namespace contextdeck {

enum class WorkspaceLaunchOutcome {
    Launched,
    Failed,
    Disabled,
    AlreadyRunning,
    MissingDesktopFile,
    InvalidDesktopFile,
    Debounced,
    NotInTransaction,
};

[[nodiscard]] QString workspaceLaunchOutcomeName(WorkspaceLaunchOutcome outcome);

class ApplicationLauncher : public QObject
{
    Q_OBJECT

public:
    using LaunchInvoker = std::function<bool(const QString &desktopFileId)>;

    explicit ApplicationLauncher(QObject *parent = nullptr);
    ~ApplicationLauncher() override;

    void setInvokerForTest(LaunchInvoker invoker);

    void beginTransaction(qint64 nowMs);
    void endTransaction();
    [[nodiscard]] bool transactionActive() const { return m_debounce.isActive(); }

    [[nodiscard]] WorkspaceLaunchOutcome requestLaunch(const WorkspaceLaunchPlan &entry, qint64 nowMs);
    [[nodiscard]] WorkspaceLaunchOutcome retryForDesktopCreated(const WorkspaceLaunchPlan &entry, qint64 nowMs);
    void noteJobFinished(const QString &profileId, bool failed);

    [[nodiscard]] bool hasAttempted(const QString &profileId) const;
    [[nodiscard]] int attemptCount(const QString &profileId) const;
    [[nodiscard]] int launchAttemptsInTransaction() const { return m_launchAttempts; }
    [[nodiscard]] QString lastOutcomeName() const;

private:
    [[nodiscard]] WorkspaceLaunchOutcome dispatch(const WorkspaceLaunchPlan &entry, bool retry);
    [[nodiscard]] bool startJob(const QString &profileId, const QString &desktopFileId);

    LaunchInvoker m_invoker;
    WorkspaceLaunchDebounce m_debounce;
    QSet<QString> m_failedBeforeWindow;
    int m_launchAttempts = 0;
    QString m_lastOutcome;
};

} // namespace contextdeck
