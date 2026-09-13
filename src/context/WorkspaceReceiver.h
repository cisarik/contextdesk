#pragma once

#include "core/Types.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QList>
#include <QObject>
#include <QString>
#include <QVector>

class QDBusPendingCallWatcher;
class QDBusServiceWatcher;
class QTimer;

namespace contextdeck {

struct VirtualDesktopDBus {
    qint32 position = 0;
    QString id;
    QString name;
};

struct VirtualDesktopDBusUnsigned {
    quint32 position = 0;
    QString id;
    QString name;
};

QDBusArgument &operator<<(QDBusArgument &argument, const VirtualDesktopDBus &value);
const QDBusArgument &operator>>(const QDBusArgument &argument, VirtualDesktopDBus &value);
QDBusArgument &operator<<(QDBusArgument &argument, const VirtualDesktopDBusUnsigned &value);
const QDBusArgument &operator>>(const QDBusArgument &argument, VirtualDesktopDBusUnsigned &value);

void registerWorkspaceDesktopDBusTypes();

class WorkspaceReceiver : public QObject
{
    Q_OBJECT

public:
    explicit WorkspaceReceiver(QObject *parent = nullptr);
    explicit WorkspaceReceiver(const QDBusConnection &connection, QObject *parent = nullptr);
    ~WorkspaceReceiver() override;

    bool start();
    void stop();
    void setPaused(bool paused);

    [[nodiscard]] bool isPaused() const { return m_paused; }
    [[nodiscard]] bool isStarted() const { return m_started; }
    [[nodiscard]] WorkspaceState state() const { return m_state; }
    [[nodiscard]] QString lastError() const { return m_lastError; }
    [[nodiscard]] QString errorClass() const { return m_errorClass; }
    [[nodiscard]] quint64 snapshotCount() const { return m_snapshotCount; }
    [[nodiscard]] quint64 rejectedReplyCount() const { return m_rejectedReplyCount; }
    [[nodiscard]] quint64 invalidationCount() const { return m_invalidationCount; }
    [[nodiscard]] quint64 notificationCount() const { return m_notificationCount; }
    [[nodiscard]] quint64 logicalRequestCount() const { return m_logicalRequestCount; }
    [[nodiscard]] quint64 activeLogicalRequestId() const { return m_activeRequestId; }
    [[nodiscard]] int recoveryAttempt() const { return m_recoveryAttempt; }

    void setTimingForTest(int deadlineMs, const QVector<int> &recoveryDelaysMs);

signals:
    void stateChanged();
    void diagnosticsChanged();

private slots:
    void onServiceOwnerChanged(const QString &service, const QString &oldOwner, const QString &newOwner);
    void onInvalidatingMessage(const QDBusMessage &message);
    void onSnapshotReply(QDBusPendingCallWatcher *watcher);
    void onCoalescedInvalidation();
    void onDeadline();
    void onRecovery();

private:
    void setupTimers();
    void subscribe();
    void unsubscribe();
    void resolveOwner();
    void invalidate(const QString &errorClass, bool immediateUnknown);
    void requestSnapshot();
    void becomeUnknown(const QString &errorClass);
    void applyValidatedState(WorkspaceState &&next);
    void abandonCurrentRequest();
    void startDeadline();
    void stopDeadline();
    void scheduleRecovery();
    void stopRecovery();
    [[nodiscard]] std::optional<WorkspaceState> decodeSnapshot(const QVariantMap &properties, QString &errorClass) const;

    QDBusConnection m_connection;
    QDBusServiceWatcher *m_watcher = nullptr;
    QTimer *m_coalesceTimer = nullptr;
    QTimer *m_deadlineTimer = nullptr;
    QTimer *m_recoveryTimer = nullptr;
    bool m_ownsConnection = false;
    bool m_started = false;
    bool m_paused = false;
    bool m_subscribed = false;
    bool m_pendingRefresh = false;
    bool m_coalesceQueued = false;
    QString m_uniqueOwner;
    quint64 m_ownerGeneration = 0;
    quint64 m_invalidationRevision = 0;
    quint64 m_requestGeneration = 0;
    quint64 m_requestRevision = 0;
    quint64 m_activeRequestId = 0;
    quint64 m_logicalRequestCount = 0;
    int m_deadlineMs = 2000;
    QVector<int> m_recoveryDelaysMs{1000, 2000, 4000, 8000, 16000, 30000};
    int m_recoveryAttempt = 0;
    WorkspaceState m_state;
    QString m_lastError;
    QString m_errorClass;
    quint64 m_snapshotCount = 0;
    quint64 m_rejectedReplyCount = 0;
    quint64 m_invalidationCount = 0;
    quint64 m_notificationCount = 0;
};

} // namespace contextdeck

Q_DECLARE_METATYPE(contextdeck::VirtualDesktopDBus)
Q_DECLARE_METATYPE(contextdeck::VirtualDesktopDBusUnsigned)
Q_DECLARE_METATYPE(QList<contextdeck::VirtualDesktopDBus>)
Q_DECLARE_METATYPE(QList<contextdeck::VirtualDesktopDBusUnsigned>)
