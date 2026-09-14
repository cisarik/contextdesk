#include "core/Persistence.h"

#include "core/persistence/DocumentCodec.h"
#include "core/persistence/JsonCommon.h"
#include "core/persistence/KeysJson.h"
#include "core/persistence/LightingJson.h"
#include "core/persistence/MatchJson.h"
#include "core/persistence/WorkspaceJson.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>

namespace contextdeck::persistence {

std::optional<Preferences> parsePreferences(const QJsonObject &object, const QString &path, int schemaVersion,
                                            PersistenceError &error)
{
    QStringList allowed{QStringLiteral("automatic_enabled"), QStringLiteral("tray_notifications")};
    if (schemaVersion >= 4) {
        allowed << QStringLiteral("workspace_management_enabled") << QStringLiteral("title_fallback_enabled")
                << QStringLiteral("active_workspace_session_id");
    }
    if (!checkObjectKeys(object, allowed, path, error)) {
        return std::nullopt;
    }
    Preferences preferences;
    auto takeBool = [&](const QString &key, bool &target) -> bool {
        if (!object.contains(key)) {
            return true;
        }
        const QJsonValue value = object.value(key);
        if (!value.isBool()) {
            error = makeError(QStringLiteral("preference must be a boolean"), path + QLatin1Char('.') + key);
            return false;
        }
        target = value.toBool();
        return true;
    };
    if (!takeBool(QStringLiteral("automatic_enabled"), preferences.automaticEnabled)
        || !takeBool(QStringLiteral("tray_notifications"), preferences.trayNotifications)) {
        return std::nullopt;
    }
    if (schemaVersion >= 4) {
        if (!takeBool(QStringLiteral("workspace_management_enabled"), preferences.workspaceManagementEnabled)
            || !takeBool(QStringLiteral("title_fallback_enabled"), preferences.titleFallbackEnabled)) {
            return std::nullopt;
        }
        if (object.contains(QStringLiteral("active_workspace_session_id"))) {
            const QJsonValue activeValue = object.value(QStringLiteral("active_workspace_session_id"));
            if (!activeValue.isString() || activeValue.toString().isEmpty()
                || !boundedUtf8(activeValue.toString(), kMaxIdentifierBytes)
                || hasControlCharacters(activeValue.toString())) {
                error = makeError(QStringLiteral("active_workspace_session_id must be a non-empty bounded identifier"),
                                  path + QStringLiteral(".active_workspace_session_id"));
                return std::nullopt;
            }
            preferences.activeWorkspaceSessionId = activeValue.toString();
        }
    }
    return preferences;
}

} // namespace contextdeck::persistence

namespace contextdeck {
using persistence::makeError;
using persistence::isInteger;
using persistence::checkObjectKeys;
using persistence::hasControlCharacters;
using persistence::boundedUtf8;
using persistence::looksLikeDesktopId;
using persistence::parseKeys;
using persistence::keysToJson;
using persistence::parseLightingV1;
using persistence::parseLightingCommon;
using persistence::isSchema1LightingMigrationFailure;
using persistence::lightingToJson;
using persistence::parseMatch;
using persistence::parsePreferences;
using persistence::parseWorkspaceAssignment;
using persistence::parseWorkspaceSession;
using persistence::workspaceAssignmentToJson;
using persistence::workspaceSessionToJson;

LoadOutcome ProfileStore::parseDocument(const QByteArray &bytes, const QString &sourcePath)
{
    LoadOutcome outcome;
    if (bytes.size() > kMaxDocumentBytes) {
        outcome.error = makeError(QStringLiteral("document exceeds 1 MiB bound"), sourcePath);
        return outcome;
    }

    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject()) {
        outcome.error = makeError(QStringLiteral("document is not valid JSON object"), QStringLiteral("$"));
        return outcome;
    }

    const QJsonObject root = json.object();
    if (!root.contains(QStringLiteral("schema_version"))) {
        outcome.error = makeError(QStringLiteral("schema_version is required"), QStringLiteral("schema_version"));
        return outcome;
    }
    if (!isInteger(root.value(QStringLiteral("schema_version")))) {
        outcome.error = makeError(QStringLiteral("schema_version must be an integer"), QStringLiteral("schema_version"));
        return outcome;
    }
    const int schemaVersion = root.value(QStringLiteral("schema_version")).toInt();
    if (schemaVersion > kSchemaVersion) {
        outcome.error = makeError(QStringLiteral("future schema_version is refused"), QStringLiteral("schema_version"));
        return outcome;
    }
    if (schemaVersion < 1) {
        outcome.error = makeError(QStringLiteral("unsupported schema_version"), QStringLiteral("schema_version"));
        return outcome;
    }

    QStringList allowed{
        QStringLiteral("schema_version"),
        QStringLiteral("device"),
        QStringLiteral("global"),
        QStringLiteral("applications"),
        QStringLiteral("preferences"),
    };
    if (schemaVersion >= 4) {
        allowed << QStringLiteral("workspace_sessions");
    }
    if (!checkObjectKeys(root, allowed, QStringLiteral("$"), outcome.error)) {
        return outcome;
    }

    auto lightingMigrationFallback = [&]() {
        LoadOutcome fallback;
        fallback.ok = true;
        fallback.migrationFallback = true;
        fallback.document.schemaVersion = kSchemaVersion;
        fallback.document.globalLighting = untouchedLighting();
        fallback.error = outcome.error;
        fallback.error.reason =
            QStringLiteral("schema 1 lighting migration failed; using pass-through plus untouched");
        fallback.error.preserved = true;
        return fallback;
    };

    ProfileDocument document;
    document.schemaVersion = kSchemaVersion;

    if (!root.contains(QStringLiteral("device")) || !root.value(QStringLiteral("device")).isObject()) {
        outcome.error = makeError(QStringLiteral("device object is required"), QStringLiteral("device"));
        return outcome;
    }
    const QJsonObject device = root.value(QStringLiteral("device")).toObject();
    static const QStringList deviceAllowed{
        QStringLiteral("vendor_id"),
        QStringLiteral("product_id"),
        QStringLiteral("model"),
    };
    if (!checkObjectKeys(device, deviceAllowed, QStringLiteral("device"), outcome.error)) {
        return outcome;
    }
    const QString vendorId = device.value(QStringLiteral("vendor_id")).toString();
    const QString productId = device.value(QStringLiteral("product_id")).toString();
    const QString model = device.value(QStringLiteral("model")).toString();
    if (vendorId != QLatin1String(kVendorIdText) || productId != QLatin1String(kProductIdText)
        || model != QLatin1String(kDeviceModel)) {
        outcome.error = makeError(QStringLiteral("device scope is not the Logitech G213 Prodigy"), QStringLiteral("device"));
        return outcome;
    }
    document.device.vendorId = vendorId;
    document.device.productId = productId;
    document.device.model = model;

    if (!root.contains(QStringLiteral("global")) || !root.value(QStringLiteral("global")).isObject()) {
        outcome.error = makeError(QStringLiteral("global object is required"), QStringLiteral("global"));
        return outcome;
    }
    const QJsonObject global = root.value(QStringLiteral("global")).toObject();
    static const QStringList globalAllowed{QStringLiteral("keys"), QStringLiteral("lighting")};
    if (!checkObjectKeys(global, globalAllowed, QStringLiteral("global"), outcome.error)) {
        return outcome;
    }
    if (global.contains(QStringLiteral("keys"))
        && !parseKeys(global.value(QStringLiteral("keys")), QStringLiteral("global.keys"), false, document.globalKeys,
                      outcome.error)) {
        return outcome;
    }
    if (!global.contains(QStringLiteral("lighting")) || !global.value(QStringLiteral("lighting")).isObject()) {
        outcome.error = makeError(QStringLiteral("global.lighting is required"), QStringLiteral("global.lighting"));
        if (schemaVersion == 1 && isSchema1LightingMigrationFailure(outcome.error)) {
            return lightingMigrationFallback();
        }
        return outcome;
    }
    const auto lighting = schemaVersion == 1
        ? parseLightingV1(global.value(QStringLiteral("lighting")).toObject(), QStringLiteral("global.lighting"),
                          outcome.error)
        : parseLightingCommon(global.value(QStringLiteral("lighting")).toObject(), QStringLiteral("global.lighting"),
                              schemaVersion, true, outcome.error);
    if (!lighting) {
        if (schemaVersion == 1 && isSchema1LightingMigrationFailure(outcome.error)) {
            return lightingMigrationFallback();
        }
        return outcome;
    }
    document.globalLighting = *lighting;

    if (schemaVersion >= 4 && root.contains(QStringLiteral("workspace_sessions"))) {
        if (!root.value(QStringLiteral("workspace_sessions")).isArray()) {
            outcome.error = makeError(QStringLiteral("workspace_sessions must be an array"), QStringLiteral("workspace_sessions"));
            return outcome;
        }
        const QJsonArray sessions = root.value(QStringLiteral("workspace_sessions")).toArray();
        for (int i = 0; i < sessions.size(); ++i) {
            const QString path = QStringLiteral("workspace_sessions[%1]").arg(i);
            if (!sessions.at(i).isObject()) {
                outcome.error = makeError(QStringLiteral("workspace session must be an object"), path);
                return outcome;
            }
            const auto session = parseWorkspaceSession(sessions.at(i).toObject(), path, outcome.error);
            if (!session) {
                return outcome;
            }
            document.workspaceSessions.push_back(*session);
        }
    }

    if (root.contains(QStringLiteral("applications"))) {
        if (!root.value(QStringLiteral("applications")).isArray()) {
            outcome.error = makeError(QStringLiteral("applications must be an array"), QStringLiteral("applications"));
            return outcome;
        }
        const QJsonArray applications = root.value(QStringLiteral("applications")).toArray();
        QSet<QString> ids;
        for (int i = 0; i < applications.size(); ++i) {
            const QString path = QStringLiteral("applications[%1]").arg(i);
            if (!applications.at(i).isObject()) {
                outcome.error = makeError(QStringLiteral("application profile must be an object"), path);
                return outcome;
            }
            const QJsonObject object = applications.at(i).toObject();
            QStringList appAllowed{
                QStringLiteral("id"),
                QStringLiteral("display_name"),
                QStringLiteral("match"),
                QStringLiteral("keys"),
                QStringLiteral("lighting"),
            };
            if (schemaVersion >= 4) {
                appAllowed << QStringLiteral("workspace");
            }
            if (!checkObjectKeys(object, appAllowed, path, outcome.error)) {
                return outcome;
            }
            ApplicationProfile profile;
            if (!object.value(QStringLiteral("id")).isString() || object.value(QStringLiteral("id")).toString().isEmpty()) {
                outcome.error = makeError(QStringLiteral("application id must be a non-empty string"), path + QStringLiteral(".id"));
                return outcome;
            }
            profile.id = object.value(QStringLiteral("id")).toString();
            if (ids.contains(profile.id)) {
                outcome.error = makeError(QStringLiteral("duplicate application id"), path + QStringLiteral(".id"));
                return outcome;
            }
            ids.insert(profile.id);
            if (!object.value(QStringLiteral("display_name")).isString()
                || object.value(QStringLiteral("display_name")).toString().isEmpty()) {
                outcome.error = makeError(QStringLiteral("display_name must be a non-empty string"),
                                          path + QStringLiteral(".display_name"));
                return outcome;
            }
            profile.displayName = object.value(QStringLiteral("display_name")).toString();
            if (!object.contains(QStringLiteral("match")) || !object.value(QStringLiteral("match")).isObject()) {
                outcome.error = makeError(QStringLiteral("match object is required"), path + QStringLiteral(".match"));
                return outcome;
            }
            const auto match = parseMatch(object.value(QStringLiteral("match")).toObject(), path + QStringLiteral(".match"),
                                          outcome.error);
            if (!match) {
                return outcome;
            }
            profile.match = *match;
            if (object.contains(QStringLiteral("keys"))
                && !parseKeys(object.value(QStringLiteral("keys")), path + QStringLiteral(".keys"), true, profile.keys,
                              outcome.error)) {
                return outcome;
            }
            if (object.contains(QStringLiteral("lighting"))) {
                if (!object.value(QStringLiteral("lighting")).isObject()) {
                    outcome.error = makeError(QStringLiteral("lighting must be an object"), path + QStringLiteral(".lighting"));
                    if (schemaVersion == 1 && isSchema1LightingMigrationFailure(outcome.error)) {
                        return lightingMigrationFallback();
                    }
                    return outcome;
                }
                const auto appLighting = schemaVersion == 1
                    ? parseLightingV1(object.value(QStringLiteral("lighting")).toObject(),
                                      path + QStringLiteral(".lighting"), outcome.error)
                    : parseLightingCommon(object.value(QStringLiteral("lighting")).toObject(),
                                          path + QStringLiteral(".lighting"), schemaVersion, false, outcome.error);
                if (!appLighting) {
                    if (schemaVersion == 1 && isSchema1LightingMigrationFailure(outcome.error)) {
                        return lightingMigrationFallback();
                    }
                    return outcome;
                }
                profile.lighting = *appLighting;
            }
            if (schemaVersion >= 4 && object.contains(QStringLiteral("workspace"))) {
                if (!object.value(QStringLiteral("workspace")).isObject()) {
                    outcome.error = makeError(QStringLiteral("workspace must be an object"), path + QStringLiteral(".workspace"));
                    return outcome;
                }
                const auto workspace = parseWorkspaceAssignment(object.value(QStringLiteral("workspace")).toObject(),
                                                                path + QStringLiteral(".workspace"), outcome.error);
                if (!workspace) {
                    return outcome;
                }
                profile.workspace = *workspace;
            }
            document.applications.push_back(std::move(profile));
        }
    }

    if (root.contains(QStringLiteral("preferences"))) {
        if (!root.value(QStringLiteral("preferences")).isObject()) {
            outcome.error = makeError(QStringLiteral("preferences must be an object"), QStringLiteral("preferences"));
            return outcome;
        }
        const auto preferences = parsePreferences(root.value(QStringLiteral("preferences")).toObject(),
                                                  QStringLiteral("preferences"), schemaVersion, outcome.error);
        if (!preferences) {
            return outcome;
        }
        document.preferences = *preferences;
    }

    const PersistenceError validation = validate(document);
    if (!validation.reason.isEmpty()) {
        outcome.error = validation;
        return outcome;
    }

    outcome.ok = true;
    outcome.document = std::move(document);
    return outcome;
}

PersistenceError ProfileStore::validate(const ProfileDocument &document)
{
    if (document.schemaVersion != kSchemaVersion) {
        return makeError(QStringLiteral("unsupported schema_version"), QStringLiteral("schema_version"));
    }
    if (document.device.vendorId != QLatin1String(kVendorIdText)
        || document.device.productId != QLatin1String(kProductIdText)
        || document.device.model != QLatin1String(kDeviceModel)) {
        return makeError(QStringLiteral("device scope is not the Logitech G213 Prodigy"), QStringLiteral("device"));
    }
    for (auto it = document.globalKeys.begin(); it != document.globalKeys.end(); ++it) {
        if (it.value().action == ActionType::InheritGlobal) {
            return makeError(QStringLiteral("inherit_global is valid only on an application profile"),
                             QStringLiteral("global.keys"));
        }
    }
    for (int i = 0; i < document.applications.size(); ++i) {
        const std::optional<Lighting> &lighting = document.applications.at(i).lighting;
        if (!lighting || !lighting->zones) {
            continue;
        }
        for (int zone = 0; zone < kZoneCount; ++zone) {
            if (zoneRoleIsDynamic((*lighting->zones)[static_cast<size_t>(zone)].role)) {
                return makeError(QStringLiteral("application lighting may use only static or off zone roles"),
                                 QStringLiteral("applications[%1].lighting.zones[%2].role").arg(i).arg(zone));
            }
        }
    }

    QSet<QString> sessionIds;
    QHash<QString, int> sessionDesktopCounts;
    for (int i = 0; i < document.workspaceSessions.size(); ++i) {
        const WorkspaceSession &session = document.workspaceSessions.at(i);
        const QString sessionPath = QStringLiteral("workspace_sessions[%1]").arg(i);
        if (session.id.isEmpty() || !boundedUtf8(session.id, kMaxIdentifierBytes) || hasControlCharacters(session.id)) {
            return makeError(QStringLiteral("workspace session id must be non-empty, bounded, and control-free"),
                             sessionPath + QStringLiteral(".id"));
        }
        if (sessionIds.contains(session.id)) {
            return makeError(QStringLiteral("duplicate workspace session id"), sessionPath + QStringLiteral(".id"));
        }
        sessionIds.insert(session.id);
        if (session.displayName.isEmpty() || !boundedUtf8(session.displayName, kMaxDisplayNameBytes)
            || hasControlCharacters(session.displayName)) {
            return makeError(QStringLiteral("workspace session display_name must be non-empty, bounded, and control-free"),
                             sessionPath + QStringLiteral(".display_name"));
        }
        if (session.rows && (*session.rows < 1 || *session.rows > kMaxWorkspaceDesktops)) {
            return makeError(QStringLiteral("workspace session rows must be an integer in 1..32"),
                             sessionPath + QStringLiteral(".rows"));
        }
        if (session.desktops.isEmpty() || session.desktops.size() > kMaxWorkspaceDesktops) {
            return makeError(QStringLiteral("workspace session desktops must contain 1..32 entries"),
                             sessionPath + QStringLiteral(".desktops"));
        }
        for (int j = 0; j < session.desktops.size(); ++j) {
            const WorkspaceDesktopEntry &desktop = session.desktops.at(j);
            const QString entryPath = sessionPath + QStringLiteral(".desktops[%1]").arg(j);
            if (desktop.ordinal != j + 1) {
                return makeError(QStringLiteral("workspace desktop ordinals must be 1-based and contiguous"),
                                 entryPath + QStringLiteral(".ordinal"));
            }
            if (desktop.name.isEmpty() || !boundedUtf8(desktop.name, kMaxDisplayNameBytes)
                || hasControlCharacters(desktop.name)) {
                return makeError(QStringLiteral("workspace desktop name must be non-empty, bounded, and control-free"),
                                 entryPath + QStringLiteral(".name"));
            }
        }
        sessionDesktopCounts.insert(session.id, session.desktops.size());
    }

    if (document.preferences.activeWorkspaceSessionId
        && !sessionIds.contains(*document.preferences.activeWorkspaceSessionId)) {
        return makeError(QStringLiteral("active workspace session does not exist"),
                         QStringLiteral("preferences.active_workspace_session_id"));
    }

    for (int i = 0; i < document.applications.size(); ++i) {
        const ApplicationProfile &profile = document.applications.at(i);
        if (!profile.workspace) {
            continue;
        }
        const QString workspacePath = QStringLiteral("applications[%1].workspace").arg(i);
        const WorkspaceAssignment &workspace = *profile.workspace;
        if (!sessionDesktopCounts.contains(workspace.sessionId)) {
            return makeError(QStringLiteral("workspace assignment references an unknown session"),
                             workspacePath + QStringLiteral(".session_id"));
        }
        const int desktopCount = sessionDesktopCounts.value(workspace.sessionId);
        if (workspace.desktopOrdinal < 1 || workspace.desktopOrdinal > desktopCount) {
            return makeError(QStringLiteral("workspace.desktop_ordinal must be within the session desktop count"),
                             workspacePath + QStringLiteral(".desktop_ordinal"));
        }
        if (workspace.launchDesktopFile && !looksLikeDesktopId(*workspace.launchDesktopFile)) {
            return makeError(QStringLiteral("workspace.launch_desktop_file must look like a desktop id"),
                             workspacePath + QStringLiteral(".launch_desktop_file"));
        }
        if (workspace.titleFallback) {
            const TitleFallback &fallback = *workspace.titleFallback;
            if (!boundedUtf8(fallback.pattern, kMaxTitlePatternBytes) || hasControlCharacters(fallback.pattern)) {
                return makeError(QStringLiteral("title_fallback.pattern is bounded and must be control-free"),
                                 workspacePath + QStringLiteral(".title_fallback.pattern"));
            }
        }
    }
    return {};
}

QJsonObject ProfileStore::toJson(const ProfileDocument &document)
{
    QJsonObject root;
    root.insert(QStringLiteral("schema_version"), kSchemaVersion);

    QJsonObject device;
    device.insert(QStringLiteral("vendor_id"), document.device.vendorId);
    device.insert(QStringLiteral("product_id"), document.device.productId);
    device.insert(QStringLiteral("model"), document.device.model);
    root.insert(QStringLiteral("device"), device);

    QJsonObject global;
    global.insert(QStringLiteral("keys"), keysToJson(document.globalKeys));
    global.insert(QStringLiteral("lighting"), lightingToJson(document.globalLighting));
    root.insert(QStringLiteral("global"), global);

    QJsonArray applications;
    for (const ApplicationProfile &profile : document.applications) {
        QJsonObject object;
        object.insert(QStringLiteral("id"), profile.id);
        object.insert(QStringLiteral("display_name"), profile.displayName);
        QJsonObject match;
        if (profile.match.desktopFileName) {
            match.insert(QStringLiteral("desktop_file_name"), *profile.match.desktopFileName);
        }
        if (profile.match.resourceClass) {
            match.insert(QStringLiteral("resource_class"), *profile.match.resourceClass);
        }
        if (profile.match.resourceName) {
            match.insert(QStringLiteral("resource_name"), *profile.match.resourceName);
        }
        object.insert(QStringLiteral("match"), match);
        object.insert(QStringLiteral("keys"), keysToJson(profile.keys));
        if (profile.lighting) {
            object.insert(QStringLiteral("lighting"), lightingToJson(*profile.lighting));
        }
        if (profile.workspace) {
            object.insert(QStringLiteral("workspace"), workspaceAssignmentToJson(*profile.workspace));
        }
        applications.append(object);
    }
    root.insert(QStringLiteral("applications"), applications);

    QJsonArray workspaceSessions;
    for (const WorkspaceSession &session : document.workspaceSessions) {
        workspaceSessions.append(workspaceSessionToJson(session));
    }
    root.insert(QStringLiteral("workspace_sessions"), workspaceSessions);

    QJsonObject preferences;
    preferences.insert(QStringLiteral("automatic_enabled"), document.preferences.automaticEnabled);
    preferences.insert(QStringLiteral("tray_notifications"), document.preferences.trayNotifications);
    preferences.insert(QStringLiteral("workspace_management_enabled"), document.preferences.workspaceManagementEnabled);
    preferences.insert(QStringLiteral("title_fallback_enabled"), document.preferences.titleFallbackEnabled);
    if (document.preferences.activeWorkspaceSessionId) {
        preferences.insert(QStringLiteral("active_workspace_session_id"), *document.preferences.activeWorkspaceSessionId);
    }
    root.insert(QStringLiteral("preferences"), preferences);
    return root;
}

} // namespace contextdeck
