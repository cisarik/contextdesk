#pragma once

#include "core/Types.h"
#include "workspace/WorkspaceCheckpoint.h"
#include "workspace/WorkspacePlan.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QObject>
#include <QString>
#include <QVector>

namespace contextdeck {

struct WorkspaceMutationOptions {
    bool switchCurrent = false;
    bool removeExtras = false;
};

struct WorkspaceMutationResult {
    bool ok = false;
    bool noChanges = false;
    QString failureClass;
    bool reverted = false;
    bool revertFailed = false;
    QString residualClass;
    QVector<QString> createdIds;
    int createdCount = 0;
    int renamedCount = 0;
    bool rowsChanged = false;
    bool wrappingChanged = false;
    bool currentSwitched = false;
    int removedCount = 0;
};

class DesktopMutator : public QObject
{
    Q_OBJECT

public:
    explicit DesktopMutator(QObject *parent = nullptr);
    explicit DesktopMutator(const QDBusConnection &connection, QObject *parent = nullptr);

    void setCheckpointPath(const QString &path);
    void setCallTimeoutForTest(int timeoutMs);

    [[nodiscard]] bool isApplying() const { return m_applying; }
    [[nodiscard]] bool checkpointExists() const { return m_checkpoint.exists(); }
    void recordCreatedDesktop(const QString &id, int position);

    [[nodiscard]] WorkspaceMutationResult apply(const WorkspacePlan &plan, const WorkspaceState &observed,
                                                const WorkspaceMutationOptions &options);
    [[nodiscard]] WorkspaceMutationResult revert(const WorkspaceState &observed);

signals:
    void desktopCreatedInTransaction(const QString &id, int position);

private:
    struct CallOutcome {
        bool ok = false;
        QDBusMessage reply;
        QString errorClass;
    };

    [[nodiscard]] CallOutcome call(const QDBusMessage &message);
    [[nodiscard]] bool createDesktop(int position, const QString &name, QString &errorClass);
    [[nodiscard]] bool setDesktopName(const QString &id, const QString &name, QString &errorClass);
    [[nodiscard]] bool removeDesktop(const QString &id, QString &errorClass);
    [[nodiscard]] bool setUintProperty(const QString &name, quint32 value, QString &errorClass);
    [[nodiscard]] bool setBoolProperty(const QString &name, bool value, QString &errorClass);
    [[nodiscard]] bool setStringProperty(const QString &name, const QString &value, QString &errorClass);
    [[nodiscard]] bool persistCheckpoint(QString &errorClass);
    [[nodiscard]] WorkspaceMutationResult runRevert(const WorkspaceState &observed, const QString &failureClass);

    QDBusConnection m_connection;
    WorkspaceCheckpoint m_checkpoint;
    int m_callTimeoutMs = 3000;
    bool m_applying = false;
    bool m_checkpointUpdateFailed = false;
    QVector<int> m_expectedCreatePositions;
    WorkspaceCheckpointData m_checkpointData;
};

} // namespace contextdeck
