#include "app/WorkspaceSessionEditor.h"

#include "context/ContextReceiver.h"

#include <algorithm>

namespace contextdeck {

namespace {

QString sanitizeDisplayText(const QString &text, qsizetype maxBytes, const QString &fallback)
{
    QString cleaned;
    for (const QChar ch : text) {
        if (ch.category() != QChar::Other_Control) {
            cleaned.append(ch);
        }
    }
    cleaned = cleaned.trimmed();
    while (!cleaned.isEmpty() && cleaned.toUtf8().size() > maxBytes) {
        cleaned.chop(1);
    }
    if (cleaned.isEmpty()) {
        return fallback;
    }
    return cleaned;
}

} // namespace

WorkspaceSessionEditor::WorkspaceSessionEditor(ProfileDocument &document, ContextReceiver *context, QObject *parent)
    : QObject(parent)
    , m_document(document)
    , m_context(context)
{
}

bool WorkspaceSessionEditor::workspaceManagementEnabled() const
{
    return m_document.preferences.workspaceManagementEnabled;
}

bool WorkspaceSessionEditor::titleFallbackEnabled() const
{
    return m_document.preferences.titleFallbackEnabled;
}

QString WorkspaceSessionEditor::activeWorkspaceSession() const
{
    return m_document.preferences.activeWorkspaceSessionId.value_or(QString());
}

QVariantList WorkspaceSessionEditor::workspaceSessions() const
{
    QVariantList list;
    for (const WorkspaceSession &session : m_document.workspaceSessions) {
        QVariantMap map;
        map.insert(QStringLiteral("id"), session.id);
        map.insert(QStringLiteral("displayName"), session.displayName);
        map.insert(QStringLiteral("hasRows"), session.rows.has_value());
        map.insert(QStringLiteral("rows"), session.rows.value_or(0));
        map.insert(QStringLiteral("hasWrapping"), session.navigationWrapping.has_value());
        map.insert(QStringLiteral("wrapping"), session.navigationWrapping.value_or(false));
        map.insert(QStringLiteral("desktopCount"), static_cast<int>(session.desktops.size()));
        QVariantList desktops;
        for (const WorkspaceDesktopEntry &desktop : session.desktops) {
            QVariantMap entry;
            entry.insert(QStringLiteral("ordinal"), desktop.ordinal);
            entry.insert(QStringLiteral("name"), desktop.name);
            desktops.push_back(entry);
        }
        map.insert(QStringLiteral("desktops"), desktops);
        map.insert(QStringLiteral("active"), session.id == activeWorkspaceSession());
        list.push_back(map);
    }
    return list;
}

QVariantList WorkspaceSessionEditor::workspaceSessionOptions() const
{
    QVariantList list;
    QVariantMap none;
    none.insert(QStringLiteral("id"), QString());
    none.insert(QStringLiteral("label"), QStringLiteral("(žiadna)"));
    list.push_back(none);
    for (const WorkspaceSession &session : m_document.workspaceSessions) {
        QVariantMap map;
        map.insert(QStringLiteral("id"), session.id);
        map.insert(QStringLiteral("label"), session.displayName);
        list.push_back(map);
    }
    return list;
}

QVariantList WorkspaceSessionEditor::workspaceDesktopEntries() const
{
    QVariantList list;
    for (const WorkspaceSession &session : m_document.workspaceSessions) {
        for (const WorkspaceDesktopEntry &desktop : session.desktops) {
            QVariantMap entry;
            entry.insert(QStringLiteral("sessionId"), session.id);
            entry.insert(QStringLiteral("sessionLabel"), session.displayName);
            entry.insert(QStringLiteral("ordinal"), desktop.ordinal);
            entry.insert(QStringLiteral("name"), desktop.name);
            list.push_back(entry);
        }
    }
    return list;
}

bool WorkspaceSessionEditor::sessionExists(const QString &id) const
{
    if (id.isEmpty()) {
        return false;
    }
    for (const WorkspaceSession &session : m_document.workspaceSessions) {
        if (session.id == id) {
            return true;
        }
    }
    return false;
}

int WorkspaceSessionEditor::sessionDesktopCount(const QString &id) const
{
    for (const WorkspaceSession &session : m_document.workspaceSessions) {
        if (session.id == id) {
            return static_cast<int>(session.desktops.size());
        }
    }
    return 0;
}

WorkspaceAssignment *WorkspaceSessionEditor::applicationWorkspace(const QString &id)
{
    for (ApplicationProfile &profile : m_document.applications) {
        if (profile.id != id) {
            continue;
        }
        if (!profile.workspace) {
            return nullptr;
        }
        return &*profile.workspace;
    }
    return nullptr;
}

void WorkspaceSessionEditor::setWorkspaceManagementEnabled(bool enabled)
{
    if (m_document.preferences.workspaceManagementEnabled == enabled) {
        return;
    }
    m_document.preferences.workspaceManagementEnabled = enabled;
    emit documentChanged();
    emit presentationChanged();
}

void WorkspaceSessionEditor::setTitleFallbackEnabled(bool enabled)
{
    if (m_document.preferences.titleFallbackEnabled == enabled) {
        return;
    }
    m_document.preferences.titleFallbackEnabled = enabled;
    m_context->setTitleFallbackEnabled(enabled);
    if (!enabled) {
        (void)m_context->takeTitleHint();
    }
    emit documentChanged();
    emit presentationChanged();
}

void WorkspaceSessionEditor::setActiveWorkspaceSession(const QString &id)
{
    const QString normalized = sessionExists(id) ? id : QString();
    if (activeWorkspaceSession() == normalized) {
        return;
    }
    if (normalized.isEmpty()) {
        m_document.preferences.activeWorkspaceSessionId.reset();
    } else {
        m_document.preferences.activeWorkspaceSessionId = normalized;
    }
    emit documentChanged();
    emit presentationChanged();
}

void WorkspaceSessionEditor::addWorkspaceSession(const QString &displayName)
{
    const QString name = sanitizeDisplayText(displayName, kMaxDisplayNameBytes, QStringLiteral("Session"));
    int suffix = 1;
    QString id = QStringLiteral("session-%1").arg(suffix);
    while (sessionExists(id)) {
        ++suffix;
        id = QStringLiteral("session-%1").arg(suffix);
    }
    WorkspaceSession session;
    session.id = id;
    session.displayName = name;
    WorkspaceDesktopEntry desktop;
    desktop.ordinal = 1;
    desktop.name = QStringLiteral("Plocha 1");
    session.desktops.push_back(desktop);
    m_document.workspaceSessions.push_back(session);
    if (!m_document.preferences.activeWorkspaceSessionId) {
        m_document.preferences.activeWorkspaceSessionId = id;
    }
    emit documentChanged();
    emit presentationChanged();
}

void WorkspaceSessionEditor::removeWorkspaceSession(const QString &id)
{
    for (int i = 0; i < m_document.workspaceSessions.size(); ++i) {
        if (m_document.workspaceSessions.at(i).id != id) {
            continue;
        }
        m_document.workspaceSessions.removeAt(i);
        if (activeWorkspaceSession() == id) {
            m_document.preferences.activeWorkspaceSessionId.reset();
        }
        for (ApplicationProfile &profile : m_document.applications) {
            if (profile.workspace && profile.workspace->sessionId == id) {
                profile.workspace.reset();
            }
        }
        emit documentChanged();
        emit presentationChanged();
        return;
    }
}

void WorkspaceSessionEditor::renameWorkspaceSession(const QString &id, const QString &displayName)
{
    const QString name = sanitizeDisplayText(displayName, kMaxDisplayNameBytes, QString());
    if (name.isEmpty()) {
        return;
    }
    for (WorkspaceSession &session : m_document.workspaceSessions) {
        if (session.id == id) {
            if (session.displayName == name) {
                return;
            }
            session.displayName = name;
            emit documentChanged();
            emit presentationChanged();
            return;
        }
    }
}

void WorkspaceSessionEditor::setWorkspaceSessionRows(const QString &id, int rows)
{
    for (WorkspaceSession &session : m_document.workspaceSessions) {
        if (session.id != id) {
            continue;
        }
        if (rows <= 0) {
            if (!session.rows.has_value()) {
                return;
            }
            session.rows.reset();
        } else {
            const int clamped = std::clamp(rows, 1, kMaxWorkspaceDesktops);
            if (session.rows.has_value() && *session.rows == clamped) {
                return;
            }
            session.rows = clamped;
        }
        emit documentChanged();
        emit presentationChanged();
        return;
    }
}

void WorkspaceSessionEditor::setWorkspaceSessionWrapping(const QString &id, bool enabled)
{
    for (WorkspaceSession &session : m_document.workspaceSessions) {
        if (session.id != id) {
            continue;
        }
        if (session.navigationWrapping.has_value() && *session.navigationWrapping == enabled) {
            return;
        }
        session.navigationWrapping = enabled;
        emit documentChanged();
        emit presentationChanged();
        return;
    }
}

void WorkspaceSessionEditor::setWorkspaceSessionDesktopCount(const QString &id, int count)
{
    for (WorkspaceSession &session : m_document.workspaceSessions) {
        if (session.id != id) {
            continue;
        }
        const int clamped = std::clamp(count, 1, kMaxWorkspaceDesktops);
        if (session.desktops.size() == clamped) {
            return;
        }
        if (session.desktops.size() < clamped) {
            while (session.desktops.size() < clamped) {
                WorkspaceDesktopEntry desktop;
                desktop.ordinal = static_cast<int>(session.desktops.size()) + 1;
                desktop.name = QStringLiteral("Plocha %1").arg(desktop.ordinal);
                session.desktops.push_back(desktop);
            }
        } else {
            session.desktops.resize(clamped);
        }
        for (ApplicationProfile &profile : m_document.applications) {
            if (profile.workspace && profile.workspace->sessionId == id) {
                profile.workspace->desktopOrdinal = std::min(profile.workspace->desktopOrdinal, clamped);
            }
        }
        emit documentChanged();
        emit presentationChanged();
        return;
    }
}

void WorkspaceSessionEditor::setWorkspaceSessionDesktopName(const QString &id, int ordinal, const QString &name)
{
    const QString sanitized = sanitizeDisplayText(name, kMaxDisplayNameBytes, QString());
    if (sanitized.isEmpty()) {
        return;
    }
    for (WorkspaceSession &session : m_document.workspaceSessions) {
        if (session.id != id) {
            continue;
        }
        for (WorkspaceDesktopEntry &desktop : session.desktops) {
            if (desktop.ordinal != ordinal) {
                continue;
            }
            if (desktop.name == sanitized) {
                return;
            }
            desktop.name = sanitized;
            emit documentChanged();
            emit presentationChanged();
            return;
        }
        return;
    }
}

void WorkspaceSessionEditor::setApplicationWorkspaceSession(const QString &id, const QString &sessionId)
{
    for (ApplicationProfile &profile : m_document.applications) {
        if (profile.id != id) {
            continue;
        }
        if (!sessionExists(sessionId)) {
            if (profile.workspace) {
                profile.workspace.reset();
                emit documentChanged();
                emit presentationChanged();
            }
            return;
        }
        if (!profile.workspace) {
            profile.workspace = WorkspaceAssignment{};
        }
        profile.workspace->sessionId = sessionId;
        const int desktopCount = sessionDesktopCount(sessionId);
        profile.workspace->desktopOrdinal = std::clamp(profile.workspace->desktopOrdinal, 1, desktopCount);
        emit documentChanged();
        emit presentationChanged();
        return;
    }
}

void WorkspaceSessionEditor::setApplicationWorkspaceDesktop(const QString &id, int ordinal)
{
    WorkspaceAssignment *workspace = applicationWorkspace(id);
    if (workspace == nullptr || !sessionExists(workspace->sessionId)) {
        return;
    }
    const int clamped = std::clamp(ordinal, 1, sessionDesktopCount(workspace->sessionId));
    if (workspace->desktopOrdinal == clamped) {
        return;
    }
    workspace->desktopOrdinal = clamped;
    emit documentChanged();
    emit presentationChanged();
}

void WorkspaceSessionEditor::setApplicationWorkspaceLaunch(const QString &id, bool launch)
{
    WorkspaceAssignment *workspace = applicationWorkspace(id);
    if (workspace == nullptr || workspace->launch == launch) {
        return;
    }
    workspace->launch = launch;
    emit documentChanged();
    emit presentationChanged();
}

void WorkspaceSessionEditor::setApplicationWorkspaceMaximize(const QString &id, bool maximize)
{
    WorkspaceAssignment *workspace = applicationWorkspace(id);
    if (workspace == nullptr || workspace->maximize == maximize) {
        return;
    }
    workspace->maximize = maximize;
    emit documentChanged();
    emit presentationChanged();
}

void WorkspaceSessionEditor::setApplicationWorkspaceLaunchFile(const QString &id, const QString &desktopFile)
{
    WorkspaceAssignment *workspace = applicationWorkspace(id);
    if (workspace == nullptr) {
        return;
    }
    const QString trimmed = desktopFile.trimmed();
    if (trimmed.isEmpty()) {
        if (workspace->launchDesktopFile.has_value()) {
            workspace->launchDesktopFile.reset();
            emit documentChanged();
            emit presentationChanged();
        }
        return;
    }
    if (!workspaceDesktopIdLooksValid(trimmed)) {
        return;
    }
    if (workspace->launchDesktopFile.has_value() && *workspace->launchDesktopFile == trimmed) {
        return;
    }
    workspace->launchDesktopFile = trimmed;
    emit documentChanged();
    emit presentationChanged();
}

void WorkspaceSessionEditor::setApplicationTitleFallback(const QString &id, bool enabled, const QString &mode,
                                                         const QString &pattern)
{
    const auto matchMode = titleMatchModeFromJsonName(mode);
    if (!matchMode) {
        return;
    }
    WorkspaceAssignment *workspace = applicationWorkspace(id);
    if (workspace == nullptr) {
        return;
    }
    const QString sanitized = sanitizeDisplayText(pattern, kMaxTitlePatternBytes, QString());
    if (!workspace->titleFallback) {
        workspace->titleFallback = TitleFallback{};
    }
    if (workspace->titleFallback->enabled == enabled && workspace->titleFallback->mode == *matchMode
        && workspace->titleFallback->pattern == sanitized) {
        return;
    }
    workspace->titleFallback->enabled = enabled;
    workspace->titleFallback->mode = *matchMode;
    workspace->titleFallback->pattern = sanitized;
    emit documentChanged();
    emit presentationChanged();
}

} // namespace contextdeck
