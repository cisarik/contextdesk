#include "core/persistence/WorkspaceJson.h"

#include "core/persistence/JsonCommon.h"
#include "core/persistence/MatchJson.h"

#include <QJsonArray>
#include <QJsonObject>

namespace contextdeck::persistence {

std::optional<WorkspaceAssignment> parseWorkspaceAssignment(const QJsonObject &object, const QString &path,
                                                            PersistenceError &error)
{
    static const QStringList allowed{
        QStringLiteral("session_id"),
        QStringLiteral("desktop_ordinal"),
        QStringLiteral("launch"),
        QStringLiteral("maximize"),
        QStringLiteral("launch_desktop_file"),
        QStringLiteral("title_fallback"),
    };
    if (!checkObjectKeys(object, allowed, path, error)) {
        return std::nullopt;
    }
    WorkspaceAssignment workspace;
    if (!object.contains(QStringLiteral("session_id")) || !object.value(QStringLiteral("session_id")).isString()) {
        error = makeError(QStringLiteral("workspace.session_id must be a string"), path + QStringLiteral(".session_id"));
        return std::nullopt;
    }
    workspace.sessionId = object.value(QStringLiteral("session_id")).toString();
    if (workspace.sessionId.isEmpty() || !boundedUtf8(workspace.sessionId, kMaxIdentifierBytes)
        || hasControlCharacters(workspace.sessionId)) {
        error = makeError(QStringLiteral("workspace.session_id must be non-empty, bounded, and control-free"),
                          path + QStringLiteral(".session_id"));
        return std::nullopt;
    }
    const QJsonValue ordinalValue = object.value(QStringLiteral("desktop_ordinal"));
    if (!isInteger(ordinalValue) || ordinalValue.toInt() < 1 || ordinalValue.toInt() > kMaxWorkspaceDesktops) {
        error = makeError(QStringLiteral("workspace.desktop_ordinal must be an integer in 1..32"),
                          path + QStringLiteral(".desktop_ordinal"));
        return std::nullopt;
    }
    workspace.desktopOrdinal = ordinalValue.toInt();

    auto takeBool = [&](const QString &key, bool &target) -> bool {
        if (!object.contains(key)) {
            return true;
        }
        if (!object.value(key).isBool()) {
            error = makeError(QStringLiteral("workspace field must be a boolean"), path + QLatin1Char('.') + key);
            return false;
        }
        target = object.value(key).toBool();
        return true;
    };
    if (!takeBool(QStringLiteral("launch"), workspace.launch)
        || !takeBool(QStringLiteral("maximize"), workspace.maximize)) {
        return std::nullopt;
    }

    if (object.contains(QStringLiteral("launch_desktop_file"))) {
        const QJsonValue fileValue = object.value(QStringLiteral("launch_desktop_file"));
        if (!fileValue.isString() || !looksLikeDesktopId(fileValue.toString())) {
            error = makeError(QStringLiteral("workspace.launch_desktop_file must look like a desktop id"),
                              path + QStringLiteral(".launch_desktop_file"));
            return std::nullopt;
        }
        workspace.launchDesktopFile = fileValue.toString();
    }
    if (object.contains(QStringLiteral("title_fallback"))) {
        if (!object.value(QStringLiteral("title_fallback")).isObject()) {
            error = makeError(QStringLiteral("workspace.title_fallback must be an object"),
                              path + QStringLiteral(".title_fallback"));
            return std::nullopt;
        }
        const auto fallback = parseTitleFallback(object.value(QStringLiteral("title_fallback")).toObject(),
                                                 path + QStringLiteral(".title_fallback"), error);
        if (!fallback) {
            return std::nullopt;
        }
        workspace.titleFallback = *fallback;
    }
    return workspace;
}

std::optional<WorkspaceSession> parseWorkspaceSession(const QJsonObject &object, const QString &path,
                                                      PersistenceError &error)
{
    static const QStringList allowed{
        QStringLiteral("id"),
        QStringLiteral("display_name"),
        QStringLiteral("rows"),
        QStringLiteral("navigation_wrapping"),
        QStringLiteral("desktops"),
    };
    if (!checkObjectKeys(object, allowed, path, error)) {
        return std::nullopt;
    }
    WorkspaceSession session;
    if (!object.contains(QStringLiteral("id")) || !object.value(QStringLiteral("id")).isString()) {
        error = makeError(QStringLiteral("workspace session id must be a string"), path + QStringLiteral(".id"));
        return std::nullopt;
    }
    session.id = object.value(QStringLiteral("id")).toString();
    if (session.id.isEmpty() || !boundedUtf8(session.id, kMaxIdentifierBytes) || hasControlCharacters(session.id)) {
        error = makeError(QStringLiteral("workspace session id must be non-empty, bounded, and control-free"),
                          path + QStringLiteral(".id"));
        return std::nullopt;
    }
    if (!object.contains(QStringLiteral("display_name")) || !object.value(QStringLiteral("display_name")).isString()) {
        error = makeError(QStringLiteral("workspace session display_name must be a string"),
                          path + QStringLiteral(".display_name"));
        return std::nullopt;
    }
    session.displayName = object.value(QStringLiteral("display_name")).toString();
    if (session.displayName.isEmpty() || !boundedUtf8(session.displayName, kMaxDisplayNameBytes)
        || hasControlCharacters(session.displayName)) {
        error = makeError(QStringLiteral("workspace session display_name must be non-empty, bounded, and control-free"),
                          path + QStringLiteral(".display_name"));
        return std::nullopt;
    }
    if (object.contains(QStringLiteral("rows"))) {
        const QJsonValue rowsValue = object.value(QStringLiteral("rows"));
        if (!isInteger(rowsValue) || rowsValue.toInt() < 1 || rowsValue.toInt() > kMaxWorkspaceDesktops) {
            error = makeError(QStringLiteral("workspace session rows must be an integer in 1..32"),
                              path + QStringLiteral(".rows"));
            return std::nullopt;
        }
        session.rows = rowsValue.toInt();
    }
    if (object.contains(QStringLiteral("navigation_wrapping"))) {
        if (!object.value(QStringLiteral("navigation_wrapping")).isBool()) {
            error = makeError(QStringLiteral("workspace session navigation_wrapping must be a boolean"),
                              path + QStringLiteral(".navigation_wrapping"));
            return std::nullopt;
        }
        session.navigationWrapping = object.value(QStringLiteral("navigation_wrapping")).toBool();
    }
    if (!object.contains(QStringLiteral("desktops")) || !object.value(QStringLiteral("desktops")).isArray()) {
        error = makeError(QStringLiteral("workspace session desktops must be an array"), path + QStringLiteral(".desktops"));
        return std::nullopt;
    }
    const QJsonArray desktops = object.value(QStringLiteral("desktops")).toArray();
    if (desktops.isEmpty() || desktops.size() > kMaxWorkspaceDesktops) {
        error = makeError(QStringLiteral("workspace session desktops must contain 1..32 entries"),
                          path + QStringLiteral(".desktops"));
        return std::nullopt;
    }
    for (int i = 0; i < desktops.size(); ++i) {
        const QString entryPath = path + QStringLiteral(".desktops[%1]").arg(i);
        if (!desktops.at(i).isObject()) {
            error = makeError(QStringLiteral("workspace desktop entry must be an object"), entryPath);
            return std::nullopt;
        }
        const QJsonObject entry = desktops.at(i).toObject();
        static const QStringList desktopAllowed{QStringLiteral("ordinal"), QStringLiteral("name")};
        if (!checkObjectKeys(entry, desktopAllowed, entryPath, error)) {
            return std::nullopt;
        }
        const QJsonValue ordinalValue = entry.value(QStringLiteral("ordinal"));
        if (!isInteger(ordinalValue) || ordinalValue.toInt() != i + 1) {
            error = makeError(QStringLiteral("workspace desktop ordinals must be 1-based and contiguous"),
                              entryPath + QStringLiteral(".ordinal"));
            return std::nullopt;
        }
        if (!entry.contains(QStringLiteral("name")) || !entry.value(QStringLiteral("name")).isString()) {
            error = makeError(QStringLiteral("workspace desktop name must be a string"), entryPath + QStringLiteral(".name"));
            return std::nullopt;
        }
        WorkspaceDesktopEntry desktop;
        desktop.ordinal = ordinalValue.toInt();
        desktop.name = entry.value(QStringLiteral("name")).toString();
        if (desktop.name.isEmpty() || !boundedUtf8(desktop.name, kMaxDisplayNameBytes) || hasControlCharacters(desktop.name)) {
            error = makeError(QStringLiteral("workspace desktop name must be non-empty, bounded, and control-free"),
                              entryPath + QStringLiteral(".name"));
            return std::nullopt;
        }
        session.desktops.push_back(std::move(desktop));
    }
    return session;
}

QJsonObject workspaceAssignmentToJson(const WorkspaceAssignment &workspace)
{
    QJsonObject object;
    object.insert(QStringLiteral("session_id"), workspace.sessionId);
    object.insert(QStringLiteral("desktop_ordinal"), workspace.desktopOrdinal);
    object.insert(QStringLiteral("launch"), workspace.launch);
    object.insert(QStringLiteral("maximize"), workspace.maximize);
    if (workspace.launchDesktopFile) {
        object.insert(QStringLiteral("launch_desktop_file"), *workspace.launchDesktopFile);
    }
    if (workspace.titleFallback) {
        object.insert(QStringLiteral("title_fallback"), titleFallbackToJson(*workspace.titleFallback));
    }
    return object;
}

QJsonObject workspaceSessionToJson(const WorkspaceSession &session)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), session.id);
    object.insert(QStringLiteral("display_name"), session.displayName);
    if (session.rows) {
        object.insert(QStringLiteral("rows"), *session.rows);
    }
    if (session.navigationWrapping) {
        object.insert(QStringLiteral("navigation_wrapping"), *session.navigationWrapping);
    }
    QJsonArray desktops;
    for (const WorkspaceDesktopEntry &desktop : session.desktops) {
        QJsonObject entry;
        entry.insert(QStringLiteral("ordinal"), desktop.ordinal);
        entry.insert(QStringLiteral("name"), desktop.name);
        desktops.append(entry);
    }
    object.insert(QStringLiteral("desktops"), desktops);
    return object;
}

} // namespace contextdeck::persistence
