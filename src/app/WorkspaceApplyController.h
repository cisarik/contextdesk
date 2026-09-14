#pragma once

#include "core/Types.h"
#include "workspace/ApplicationLauncher.h"
#include "workspace/DesktopMutator.h"
#include "workspace/WorkspacePlan.h"

#include <QObject>
#include <QString>

namespace contextdeck {

class ContextReceiver;
class WorkspaceReceiver;

// Owns the workspace apply/revert transaction, the launch debounce, the
// mutation checkpoint, and the last-preview fingerprint.
class WorkspaceApplyController : public QObject
{
    Q_OBJECT

public:
    WorkspaceApplyController(const ProfileDocument &document, ContextReceiver *context, const QString &configRoot,
                             QObject *parent = nullptr);

    void setWorkspaceReceiver(WorkspaceReceiver *receiver);

    [[nodiscard]] bool workspaceApplyAvailable() const;
    [[nodiscard]] bool workspaceCheckpointAvailable() const;
    [[nodiscard]] bool workspaceApplyRunning() const { return m_applyRunning; }
    [[nodiscard]] QString workspaceApplyStatus() const { return m_applyStatus; }
    [[nodiscard]] QString workspaceLastResidual() const { return m_applyResidual; }
    [[nodiscard]] int workspaceLastMutationCreated() const { return m_lastApplyCreated; }
    [[nodiscard]] int workspaceLastMutationRemoved() const { return m_lastApplyRemoved; }
    [[nodiscard]] bool workspaceLastApplyReverted() const { return m_lastApplyReverted; }
    [[nodiscard]] int launchAttemptsInTransaction() const { return m_launcher.launchAttemptsInTransaction(); }
    [[nodiscard]] ApplicationLauncher &launcher() { return m_launcher; }
    [[nodiscard]] DesktopMutator &mutator() { return m_mutator; }

    [[nodiscard]] WorkspacePlan planForState(const WorkspaceState &observed) const;
    void rememberPreview(const WorkspacePlan &plan, const WorkspaceState &observed) const;
    [[nodiscard]] QVector<ApplicationIdentity> openWindowIdentities() const;

    bool applyWorkspaceSession(bool switchCurrent, bool removeExtras);
    bool revertWorkspaceApply();
    void resetApplyState();
    void refreshPreview();

    [[nodiscard]] static WorkspaceState workspaceStateOf(const WorkspaceReceiver *workspace);

signals:
    void presentationChanged();
    void diagnosticsChanged();

private slots:
    void onWorkspaceDesktopCreated(const QString &id, int position);
    void onDesktopCreatedInTransaction(const QString &id, int position);

private:
    void launchPlannedProfiles(const WorkspacePlan &plan);
    void launchProfilesForOrdinal(int ordinal);
    [[nodiscard]] static QString workspacePreviewFingerprint(const WorkspacePlan &plan, const WorkspaceState &state);

    const ProfileDocument &m_document;
    ContextReceiver *m_context = nullptr;
    WorkspaceReceiver *m_workspace = nullptr;
    DesktopMutator m_mutator;
    ApplicationLauncher m_launcher;
    mutable WorkspacePlan m_lastPlan;
    mutable bool m_hasLastPlan = false;
    mutable QString m_previewFingerprint;
    mutable quint64 m_previewOwnerGeneration = 0;
    bool m_applyRunning = false;
    QString m_applyStatus;
    QString m_applyResidual;
    bool m_lastApplyReverted = false;
    int m_lastApplyCreated = 0;
    int m_lastApplyRemoved = 0;
};

} // namespace contextdeck
