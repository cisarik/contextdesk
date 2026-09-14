#pragma once

#include "core/Types.h"

#include <QHash>
#include <QString>
#include <QVector>

#include <optional>

namespace contextdeck {

enum class WorkspaceLaunchIntent {
    Disabled,
    WouldLaunch,
    AlreadyRunning,
    MissingDesktopFile,
};

enum class WorkspaceEventKind {
    ApplySession,
    DesktopCreated,
    SessionLogin,
    SessionAppStart,
    CurrentDesktopChanged,
    UserDesktopChange,
    BrokerEvent,
};

struct WorkspaceDesktopPlan {
    int ordinal = 0;
    QString name;
    QString observedName;
    bool create = false;
    bool rename = false;
};

struct WorkspaceLaunchPlan {
    QString profileId;
    QString displayName;
    int desktopOrdinal = 0;
    bool maximize = false;
    QString desktopFileId;
    WorkspaceLaunchIntent intent = WorkspaceLaunchIntent::Disabled;
};

struct WorkspacePlan {
    bool sessionFound = false;
    QString sessionId;
    bool managementEnabled = false;
    bool observationAvailable = false;
    int desiredDesktopCount = 0;
    int observedDesktopCount = 0;
    bool drift = false;
    bool extraDesktop = false;
    bool rowsChange = false;
    bool wrappingChange = false;
    std::optional<int> desiredRows;
    std::optional<int> observedRows;
    std::optional<bool> desiredWrapping;
    std::optional<bool> observedWrapping;
    QVector<WorkspaceDesktopPlan> desktops;
    QVector<WorkspaceLaunchPlan> launches;
};

[[nodiscard]] const WorkspaceSession *findWorkspaceSession(const ProfileDocument &document, const QString &sessionId);

[[nodiscard]] WorkspacePlan computeWorkspacePlan(const ProfileDocument &document, const QString &sessionId,
                                                 const WorkspaceState &observed,
                                                 const QVector<ApplicationIdentity> &openWindows);

[[nodiscard]] bool workspaceEventIsLaunchTrigger(WorkspaceEventKind kind, bool transactionActive);

[[nodiscard]] QString workspaceLaunchIntentName(WorkspaceLaunchIntent intent);

class WorkspaceLaunchDebounce
{
public:
    static constexpr qint64 kMinimumIntervalMs = 2000;
    static constexpr int kMaximumAttemptsPerProfile = 2;

    void beginTransaction(qint64 nowMs);
    void endTransaction();
    [[nodiscard]] bool isActive() const { return m_active; }
    [[nodiscard]] bool tryAttempt(const QString &profileId, qint64 nowMs);
    [[nodiscard]] bool tryBoundedRetry(const QString &profileId, qint64 nowMs);
    [[nodiscard]] bool hasAttempted(const QString &profileId) const;
    [[nodiscard]] int attemptCount(const QString &profileId) const;

private:
    bool m_active = false;
    QHash<QString, qint64> m_lastAttemptMs;
    QHash<QString, int> m_attempts;
};

} // namespace contextdeck
