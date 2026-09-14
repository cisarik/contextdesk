#pragma once

#include "core/Types.h"

#include <QObject>
#include <QString>
#include <QVariantList>

namespace contextdeck {

class ContextReceiver;

// Owns workspace session CRUD, the workspace-related document preferences, and
// the per-application workspace and title-fallback setters.
class WorkspaceSessionEditor : public QObject
{
    Q_OBJECT

public:
    WorkspaceSessionEditor(ProfileDocument &document, ContextReceiver *context, QObject *parent = nullptr);

    [[nodiscard]] bool workspaceManagementEnabled() const;
    [[nodiscard]] bool titleFallbackEnabled() const;
    [[nodiscard]] QString activeWorkspaceSession() const;
    [[nodiscard]] QVariantList workspaceSessions() const;
    [[nodiscard]] QVariantList workspaceSessionOptions() const;
    [[nodiscard]] QVariantList workspaceDesktopEntries() const;

    void setWorkspaceManagementEnabled(bool enabled);
    void setTitleFallbackEnabled(bool enabled);
    void setActiveWorkspaceSession(const QString &id);
    void addWorkspaceSession(const QString &displayName);
    void removeWorkspaceSession(const QString &id);
    void renameWorkspaceSession(const QString &id, const QString &displayName);
    void setWorkspaceSessionRows(const QString &id, int rows);
    void setWorkspaceSessionWrapping(const QString &id, bool enabled);
    void setWorkspaceSessionDesktopCount(const QString &id, int count);
    void setWorkspaceSessionDesktopName(const QString &id, int ordinal, const QString &name);
    void setApplicationWorkspaceSession(const QString &id, const QString &sessionId);
    void setApplicationWorkspaceDesktop(const QString &id, int ordinal);
    void setApplicationWorkspaceLaunch(const QString &id, bool launch);
    void setApplicationWorkspaceMaximize(const QString &id, bool maximize);
    void setApplicationWorkspaceLaunchFile(const QString &id, const QString &desktopFile);
    void setApplicationTitleFallback(const QString &id, bool enabled, const QString &mode, const QString &pattern);

signals:
    void documentChanged();
    void presentationChanged();

private:
    [[nodiscard]] bool sessionExists(const QString &id) const;
    [[nodiscard]] int sessionDesktopCount(const QString &id) const;
    [[nodiscard]] WorkspaceAssignment *applicationWorkspace(const QString &id);

    ProfileDocument &m_document;
    ContextReceiver *m_context = nullptr;
};

} // namespace contextdeck
