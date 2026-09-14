#include "core/Persistence.h"

#include "core/ControlCatalog.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

namespace contextdeck {
namespace {

PersistenceError makeError(const QString &reason, const QString &jsonPath, bool preserved = true)
{
    PersistenceError error;
    error.reason = reason;
    error.jsonPath = jsonPath;
    error.preserved = preserved;
    return error;
}

bool isInteger(const QJsonValue &value)
{
    if (!value.isDouble()) {
        return false;
    }
    const double number = value.toDouble();
    return number == static_cast<double>(static_cast<int>(number));
}

bool checkObjectKeys(const QJsonObject &object, const QStringList &allowed, const QString &path, PersistenceError &error)
{
    const QSet<QString> allow(allowed.begin(), allowed.end());
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (!allow.contains(it.key())) {
            error = makeError(QStringLiteral("unknown semantic field"), path + QLatin1Char('.') + it.key());
            return false;
        }
    }
    return true;
}

bool hasControlCharacters(const QString &text)
{
    for (const QChar ch : text) {
        if (ch.category() == QChar::Other_Control) {
            return true;
        }
    }
    return false;
}

bool boundedUtf8(const QString &text, qsizetype maxBytes)
{
    return text.toUtf8().size() <= maxBytes;
}

bool looksLikeDesktopId(const QString &value)
{
    return workspaceDesktopIdLooksValid(value);
}

std::optional<Rgb> parseColor(const QJsonValue &value, const QString &path, PersistenceError &error)
{
    if (!value.isString()) {
        error = makeError(QStringLiteral("color must be a #rrggbb string"), path);
        return std::nullopt;
    }
    const QString text = value.toString();
    if (text.size() != 7 || text[0] != QLatin1Char('#')) {
        error = makeError(QStringLiteral("color must be a #rrggbb string"), path);
        return std::nullopt;
    }
    bool ok = false;
    const int rgb = text.sliced(1).toInt(&ok, 16);
    if (!ok) {
        error = makeError(QStringLiteral("color must be a #rrggbb string"), path);
        return std::nullopt;
    }
    Rgb color;
    color.r = static_cast<quint8>((rgb >> 16) & 0xff);
    color.g = static_cast<quint8>((rgb >> 8) & 0xff);
    color.b = static_cast<quint8>(rgb & 0xff);
    return color;
}

QString colorToJson(const Rgb &color)
{
    return QStringLiteral("#%1%2%3")
        .arg(color.r, 2, 16, QLatin1Char('0'))
        .arg(color.g, 2, 16, QLatin1Char('0'))
        .arg(color.b, 2, 16, QLatin1Char('0'));
}

std::optional<Chord> parseChord(const QJsonObject &object, const QString &path, PersistenceError &error)
{
    static const QStringList allowed{QStringLiteral("key"), QStringLiteral("modifiers")};
    if (!checkObjectKeys(object, allowed, path, error)) {
        return std::nullopt;
    }
    if (!object.contains(QStringLiteral("key")) || !object.value(QStringLiteral("key")).isString()) {
        error = makeError(QStringLiteral("chord.key must be a string"), path + QStringLiteral(".key"));
        return std::nullopt;
    }
    const QString key = object.value(QStringLiteral("key")).toString();
    if (chordKeyLooksLikeShellOrPath(key)) {
        error = makeError(QStringLiteral("chord key looks like a shell command or path"), path + QStringLiteral(".key"));
        return std::nullopt;
    }
    if (!isAllowedChordKey(key)) {
        error = makeError(QStringLiteral("chord key is not in the allowed table"), path + QStringLiteral(".key"));
        return std::nullopt;
    }

    Chord chord;
    chord.key = key;
    if (object.contains(QStringLiteral("modifiers"))) {
        const QJsonValue modifiersValue = object.value(QStringLiteral("modifiers"));
        if (!modifiersValue.isArray()) {
            error = makeError(QStringLiteral("chord.modifiers must be an array"), path + QStringLiteral(".modifiers"));
            return std::nullopt;
        }
        const QJsonArray modifiers = modifiersValue.toArray();
        QSet<Modifier> seen;
        for (int i = 0; i < modifiers.size(); ++i) {
            const QJsonValue item = modifiers.at(i);
            if (!item.isString()) {
                error = makeError(QStringLiteral("modifier must be a string"),
                                  path + QStringLiteral(".modifiers[%1]").arg(i));
                return std::nullopt;
            }
            const auto modifier = modifierFromJsonName(item.toString());
            if (!modifier) {
                error = makeError(QStringLiteral("unknown modifier"), path + QStringLiteral(".modifiers[%1]").arg(i));
                return std::nullopt;
            }
            if (seen.contains(*modifier)) {
                error = makeError(QStringLiteral("duplicate modifier"), path + QStringLiteral(".modifiers[%1]").arg(i));
                return std::nullopt;
            }
            seen.insert(*modifier);
            chord.modifiers.push_back(*modifier);
        }
    }
    return chord;
}

std::optional<Assignment> parseAssignment(const QJsonObject &object, const QString &path, bool allowInherit,
                                          PersistenceError &error)
{
    if (!object.contains(QStringLiteral("action")) || !object.value(QStringLiteral("action")).isString()) {
        error = makeError(QStringLiteral("assignment.action must be a string"), path + QStringLiteral(".action"));
        return std::nullopt;
    }
    const QString actionName = object.value(QStringLiteral("action")).toString();
    const auto action = actionFromJsonName(actionName);
    if (!action) {
        error = makeError(QStringLiteral("unknown action type"), path + QStringLiteral(".action"));
        return std::nullopt;
    }

    QStringList allowed{QStringLiteral("action")};
    switch (*action) {
    case ActionType::EmitShortcut:
        allowed << QStringLiteral("chord");
        break;
    case ActionType::ApprovedSystemAction:
        allowed << QStringLiteral("action_id");
        break;
    default:
        break;
    }
    if (!checkObjectKeys(object, allowed, path, error)) {
        return std::nullopt;
    }

    if (*action == ActionType::InheritGlobal && !allowInherit) {
        error = makeError(QStringLiteral("inherit_global is valid only on an application profile"), path + QStringLiteral(".action"));
        return std::nullopt;
    }

    Assignment assignment;
    assignment.action = *action;

    if (*action == ActionType::EmitShortcut) {
        if (!object.contains(QStringLiteral("chord")) || !object.value(QStringLiteral("chord")).isObject()) {
            error = makeError(QStringLiteral("emit_shortcut requires a chord object"), path + QStringLiteral(".chord"));
            return std::nullopt;
        }
        const auto chord = parseChord(object.value(QStringLiteral("chord")).toObject(), path + QStringLiteral(".chord"), error);
        if (!chord) {
            return std::nullopt;
        }
        assignment.chord = *chord;
    } else if (*action == ActionType::ApprovedSystemAction) {
        if (!object.contains(QStringLiteral("action_id")) || !object.value(QStringLiteral("action_id")).isString()) {
            error = makeError(QStringLiteral("approved_system_action requires action_id"), path + QStringLiteral(".action_id"));
            return std::nullopt;
        }
        const auto systemAction = systemActionFromJsonName(object.value(QStringLiteral("action_id")).toString());
        if (!systemAction) {
            error = makeError(QStringLiteral("unknown approved system action"), path + QStringLiteral(".action_id"));
            return std::nullopt;
        }
        assignment.systemAction = *systemAction;
    }

    return assignment;
}

bool parseKeys(const QJsonValue &value, const QString &path, bool allowInherit, QHash<ControlId, Assignment> &out,
               PersistenceError &error)
{
    if (!value.isObject()) {
        error = makeError(QStringLiteral("keys must be an object"), path);
        return false;
    }
    const QJsonObject object = value.toObject();
    for (auto it = object.begin(); it != object.end(); ++it) {
        const auto control = controlFromJsonName(it.key());
        if (!control) {
            error = makeError(QStringLiteral("unknown control"), path + QLatin1Char('.') + it.key());
            return false;
        }
        if (!it.value().isObject()) {
            error = makeError(QStringLiteral("assignment must be an object"), path + QLatin1Char('.') + it.key());
            return false;
        }
        const auto assignment = parseAssignment(it.value().toObject(), path + QLatin1Char('.') + it.key(), allowInherit, error);
        if (!assignment) {
            return false;
        }
        if (controlIsConditional(*control)
            && (assignment->action == ActionType::EmitShortcut
                || assignment->action == ActionType::ApprovedSystemAction)) {
            error = makeError(QStringLiteral("GameMode and Backlight are conditional and cannot be bound in M1"),
                              path + QLatin1Char('.') + it.key());
            return false;
        }
        out.insert(*control, *assignment);
    }
    return true;
}

std::optional<ZoneRole> zoneRoleFromJsonName(QStringView name)
{
    if (name == QLatin1String("static")) {
        return ZoneRole::Static;
    }
    if (name == QLatin1String("desktop_indicator")) {
        return ZoneRole::DesktopIndicator;
    }
    if (name == QLatin1String("app_color")) {
        return ZoneRole::AppColor;
    }
    if (name == QLatin1String("off")) {
        return ZoneRole::Off;
    }
    return std::nullopt;
}

QString zoneRoleJsonName(ZoneRole role)
{
    switch (role) {
    case ZoneRole::Static:
        return QStringLiteral("static");
    case ZoneRole::DesktopIndicator:
        return QStringLiteral("desktop_indicator");
    case ZoneRole::AppColor:
        return QStringLiteral("app_color");
    case ZoneRole::Off:
        return QStringLiteral("off");
    }
    return QStringLiteral("static");
}

bool parseZoneArrayV2(const QJsonValue &zonesValue, const QString &path,
                      std::optional<std::array<ZoneValue, kZoneCount>> &out, PersistenceError &error)
{
    if (zonesValue.isNull()) {
        out.reset();
        return true;
    }
    if (!zonesValue.isArray()) {
        error = makeError(QStringLiteral("lighting.zones must be null or an array of five colors"), path);
        return false;
    }
    const QJsonArray zones = zonesValue.toArray();
    if (zones.size() != kZoneCount) {
        error = makeError(QStringLiteral("lighting.zones must contain exactly five #rrggbb entries"), path);
        return false;
    }
    std::array<ZoneValue, kZoneCount> parsed{};
    for (int i = 0; i < kZoneCount; ++i) {
        const auto color = parseColor(zones.at(i), path + QStringLiteral("[%1]").arg(i), error);
        if (!color) {
            return false;
        }
        parsed[static_cast<size_t>(i)].role = ZoneRole::Static;
        parsed[static_cast<size_t>(i)].color = *color;
    }
    out = parsed;
    return true;
}

bool parseZoneObjectV3(const QJsonValue &value, const QString &path, bool allowDynamicRoles, ZoneValue &out,
                       PersistenceError &error)
{
    if (!value.isObject()) {
        error = makeError(QStringLiteral("lighting.zones entries must be objects"), path);
        return false;
    }
    const QJsonObject object = value.toObject();
    if (!object.contains(QStringLiteral("role")) || !object.value(QStringLiteral("role")).isString()) {
        error = makeError(QStringLiteral("zone.role must be a string"), path + QStringLiteral(".role"));
        return false;
    }
    const auto role = zoneRoleFromJsonName(object.value(QStringLiteral("role")).toString());
    if (!role) {
        error = makeError(QStringLiteral("unknown zone role"), path + QStringLiteral(".role"));
        return false;
    }
    if (!allowDynamicRoles && zoneRoleIsDynamic(*role)) {
        error = makeError(QStringLiteral("application lighting may use only static or off zone roles"),
                          path + QStringLiteral(".role"));
        return false;
    }

    QStringList allowed{QStringLiteral("role")};
    if (*role != ZoneRole::Off) {
        allowed << QStringLiteral("color");
    }
    if (!checkObjectKeys(object, allowed, path, error)) {
        return false;
    }

    ZoneValue parsed;
    parsed.role = *role;
    if (*role == ZoneRole::Off) {
        if (object.contains(QStringLiteral("color"))) {
            error = makeError(QStringLiteral("off zones must not include color"), path + QStringLiteral(".color"));
            return false;
        }
        out = parsed;
        return true;
    }
    if (!object.contains(QStringLiteral("color"))) {
        error = makeError(QStringLiteral("zone.color is required"), path + QStringLiteral(".color"));
        return false;
    }
    const auto color = parseColor(object.value(QStringLiteral("color")), path + QStringLiteral(".color"), error);
    if (!color) {
        return false;
    }
    parsed.color = *color;
    out = parsed;
    return true;
}

bool parseZoneArrayV3(const QJsonValue &zonesValue, const QString &path, bool allowDynamicRoles,
                      std::optional<std::array<ZoneValue, kZoneCount>> &out, PersistenceError &error)
{
    if (zonesValue.isNull()) {
        out.reset();
        return true;
    }
    if (!zonesValue.isArray()) {
        error = makeError(QStringLiteral("lighting.zones must be null or an array of five objects"), path);
        return false;
    }
    const QJsonArray zones = zonesValue.toArray();
    if (zones.size() != kZoneCount) {
        error = makeError(QStringLiteral("lighting.zones must contain exactly five objects"), path);
        return false;
    }
    std::array<ZoneValue, kZoneCount> parsed{};
    for (int i = 0; i < kZoneCount; ++i) {
        if (!parseZoneObjectV3(zones.at(i), path + QStringLiteral("[%1]").arg(i), allowDynamicRoles,
                               parsed[static_cast<size_t>(i)], error)) {
            return false;
        }
    }
    out = parsed;
    return true;
}

std::optional<Lighting> parseLightingV1(const QJsonObject &object, const QString &path, PersistenceError &error)
{
    static const QStringList allowed{QStringLiteral("mode"), QStringLiteral("base_color"), QStringLiteral("zones")};
    if (!checkObjectKeys(object, allowed, path, error)) {
        return std::nullopt;
    }
    if (!object.contains(QStringLiteral("mode")) || !object.value(QStringLiteral("mode")).isString()) {
        error = makeError(QStringLiteral("lighting.mode must be a string"), path + QStringLiteral(".mode"));
        return std::nullopt;
    }
    const QString modeName = object.value(QStringLiteral("mode")).toString();
    Lighting lighting;
    lighting.restoreMode = LightingMode::Wave;
    if (modeName == QLatin1String("automatic")) {
        lighting.mode = LightingMode::Untouched;
    } else if (modeName == QLatin1String("temporary_color")) {
        lighting.mode = LightingMode::Direct;
    } else if (modeName == QLatin1String("lights_off")) {
        lighting.mode = LightingMode::Off;
    } else {
        error = makeError(QStringLiteral("unknown lighting mode"), path + QStringLiteral(".mode"));
        return std::nullopt;
    }
    if (!object.contains(QStringLiteral("base_color"))) {
        error = makeError(QStringLiteral("lighting.base_color is required"), path + QStringLiteral(".base_color"));
        return std::nullopt;
    }
    const auto baseColor = parseColor(object.value(QStringLiteral("base_color")), path + QStringLiteral(".base_color"), error);
    if (!baseColor) {
        return std::nullopt;
    }
    lighting.baseColor = *baseColor;
    if (object.contains(QStringLiteral("zones"))) {
        if (!parseZoneArrayV2(object.value(QStringLiteral("zones")), path + QStringLiteral(".zones"), lighting.zones,
                              error)) {
            return std::nullopt;
        }
    }
    return lighting;
}

std::optional<Lighting> parseLightingCommon(const QJsonObject &object, const QString &path, int schemaVersion,
                                           bool allowDynamicRoles, PersistenceError &error)
{
    static const QStringList allowed{
        QStringLiteral("mode"),
        QStringLiteral("base_color"),
        QStringLiteral("zones"),
        QStringLiteral("restore_mode"),
        QStringLiteral("speed"),
    };
    if (!checkObjectKeys(object, allowed, path, error)) {
        return std::nullopt;
    }
    if (!object.contains(QStringLiteral("mode")) || !object.value(QStringLiteral("mode")).isString()) {
        error = makeError(QStringLiteral("lighting.mode must be a string"), path + QStringLiteral(".mode"));
        return std::nullopt;
    }
    const auto mode = lightingModeFromJsonName(object.value(QStringLiteral("mode")).toString());
    if (!mode) {
        error = makeError(QStringLiteral("unknown lighting mode"), path + QStringLiteral(".mode"));
        return std::nullopt;
    }

    Lighting lighting;
    lighting.mode = *mode;
    lighting.restoreMode = LightingMode::Wave;

    if (object.contains(QStringLiteral("restore_mode"))) {
        if (!object.value(QStringLiteral("restore_mode")).isString()) {
            error = makeError(QStringLiteral("lighting.restore_mode must be a string"), path + QStringLiteral(".restore_mode"));
            return std::nullopt;
        }
        const auto restore = restoreLightingModeFromJsonName(object.value(QStringLiteral("restore_mode")).toString());
        if (!restore) {
            error = makeError(QStringLiteral("unknown lighting mode"), path + QStringLiteral(".restore_mode"));
            return std::nullopt;
        }
        lighting.restoreMode = *restore;
    }

    if (object.contains(QStringLiteral("base_color")) && !object.value(QStringLiteral("base_color")).isNull()) {
        const auto baseColor = parseColor(object.value(QStringLiteral("base_color")), path + QStringLiteral(".base_color"), error);
        if (!baseColor) {
            return std::nullopt;
        }
        lighting.baseColor = *baseColor;
    }

    if (object.contains(QStringLiteral("zones"))) {
        const bool ok = schemaVersion >= 3
            ? parseZoneArrayV3(object.value(QStringLiteral("zones")), path + QStringLiteral(".zones"), allowDynamicRoles,
                               lighting.zones, error)
            : parseZoneArrayV2(object.value(QStringLiteral("zones")), path + QStringLiteral(".zones"), lighting.zones,
                               error);
        if (!ok) {
            return std::nullopt;
        }
    }

    if (object.contains(QStringLiteral("speed"))) {
        const QJsonValue speedValue = object.value(QStringLiteral("speed"));
        if (!isInteger(speedValue) || speedValue.toDouble() < 0 || speedValue.toDouble() > 2147483647.0) {
            error = makeError(QStringLiteral("lighting.speed must be a non-negative integer"),
                              path + QStringLiteral(".speed"));
            return std::nullopt;
        }
        lighting.speed = static_cast<quint32>(speedValue.toInt());
    }

    if (lighting.mode == LightingMode::Direct && !lighting.baseColor && !lighting.zones) {
        error = makeError(QStringLiteral("direct lighting requires base_color or exactly five zones"), path);
        return std::nullopt;
    }
    return lighting;
}

bool isSchema1LightingMigrationFailure(const PersistenceError &error)
{
    if (error.reason.contains(QStringLiteral("unknown semantic field"))) {
        return false;
    }
    return error.jsonPath.contains(QStringLiteral(".lighting")) || error.jsonPath == QLatin1String("global.lighting");
}

std::optional<MatchSpec> parseMatch(const QJsonObject &object, const QString &path, PersistenceError &error)
{
    static const QStringList allowed{
        QStringLiteral("desktop_file_name"),
        QStringLiteral("resource_class"),
        QStringLiteral("resource_name"),
    };
    if (!checkObjectKeys(object, allowed, path, error)) {
        return std::nullopt;
    }
    MatchSpec match;
    auto takeOptionalString = [&](const QString &key, std::optional<QString> &target) -> bool {
        if (!object.contains(key)) {
            return true;
        }
        const QJsonValue value = object.value(key);
        if (!value.isString() || value.toString().isEmpty()) {
            error = makeError(QStringLiteral("match field must be a non-empty string"), path + QLatin1Char('.') + key);
            return false;
        }
        target = value.toString();
        return true;
    };
    if (!takeOptionalString(QStringLiteral("desktop_file_name"), match.desktopFileName)
        || !takeOptionalString(QStringLiteral("resource_class"), match.resourceClass)
        || !takeOptionalString(QStringLiteral("resource_name"), match.resourceName)) {
        return std::nullopt;
    }
    if (match.isEmpty()) {
        error = makeError(QStringLiteral("match requires at least one of desktop_file_name, resource_class, resource_name"),
                          path);
        return std::nullopt;
    }
    return match;
}

std::optional<TitleFallback> parseTitleFallback(const QJsonObject &object, const QString &path, PersistenceError &error)
{
    static const QStringList allowed{
        QStringLiteral("enabled"),
        QStringLiteral("mode"),
        QStringLiteral("pattern"),
    };
    if (!checkObjectKeys(object, allowed, path, error)) {
        return std::nullopt;
    }
    if (!object.contains(QStringLiteral("enabled")) || !object.value(QStringLiteral("enabled")).isBool()) {
        error = makeError(QStringLiteral("title_fallback.enabled must be a boolean"), path + QStringLiteral(".enabled"));
        return std::nullopt;
    }
    TitleFallback fallback;
    fallback.enabled = object.value(QStringLiteral("enabled")).toBool();
    if (object.contains(QStringLiteral("mode"))) {
        if (!object.value(QStringLiteral("mode")).isString()) {
            error = makeError(QStringLiteral("title_fallback.mode must be a string"), path + QStringLiteral(".mode"));
            return std::nullopt;
        }
        const auto mode = titleMatchModeFromJsonName(object.value(QStringLiteral("mode")).toString());
        if (!mode) {
            error = makeError(QStringLiteral("unknown title match mode"), path + QStringLiteral(".mode"));
            return std::nullopt;
        }
        fallback.mode = *mode;
    }
    if (object.contains(QStringLiteral("pattern"))) {
        const QJsonValue patternValue = object.value(QStringLiteral("pattern"));
        if (!patternValue.isString()) {
            error = makeError(QStringLiteral("title_fallback.pattern must be a string"), path + QStringLiteral(".pattern"));
            return std::nullopt;
        }
        const QString pattern = patternValue.toString();
        if (!boundedUtf8(pattern, kMaxTitlePatternBytes) || hasControlCharacters(pattern)) {
            error = makeError(QStringLiteral("title_fallback.pattern is bounded and must be control-free"),
                              path + QStringLiteral(".pattern"));
            return std::nullopt;
        }
        fallback.pattern = pattern;
    }
    return fallback;
}

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

QJsonObject assignmentToJson(const Assignment &assignment)
{
    QJsonObject object;
    object.insert(QStringLiteral("action"), actionJsonName(assignment.action));
    if (assignment.action == ActionType::EmitShortcut && assignment.chord) {
        QJsonObject chord;
        chord.insert(QStringLiteral("key"), assignment.chord->key);
        QJsonArray modifiers;
        for (Modifier modifier : assignment.chord->modifiers) {
            modifiers.append(modifierJsonName(modifier));
        }
        chord.insert(QStringLiteral("modifiers"), modifiers);
        object.insert(QStringLiteral("chord"), chord);
    }
    if (assignment.action == ActionType::ApprovedSystemAction && assignment.systemAction) {
        object.insert(QStringLiteral("action_id"), systemActionJsonName(*assignment.systemAction));
    }
    return object;
}

QJsonObject keysToJson(const QHash<ControlId, Assignment> &keys)
{
    QJsonObject object;
    for (const ControlInfo &info : controlCatalog()) {
        if (keys.contains(info.id)) {
            object.insert(QString::fromLatin1(info.jsonName), assignmentToJson(keys.value(info.id)));
        }
    }
    return object;
}

QJsonObject lightingToJson(const Lighting &lighting)
{
    QJsonObject object;
    object.insert(QStringLiteral("mode"), lightingModeJsonName(lighting.mode));
    object.insert(QStringLiteral("restore_mode"), lightingModeJsonName(lighting.restoreMode));
    if (lighting.baseColor) {
        object.insert(QStringLiteral("base_color"), colorToJson(*lighting.baseColor));
    }
    if (lighting.zones) {
        QJsonArray zones;
        for (const ZoneValue &zone : *lighting.zones) {
            QJsonObject entry;
            entry.insert(QStringLiteral("role"), zoneRoleJsonName(zone.role));
            if (zone.role != ZoneRole::Off) {
                entry.insert(QStringLiteral("color"), colorToJson(zone.color));
            }
            zones.append(entry);
        }
        object.insert(QStringLiteral("zones"), zones);
    } else {
        object.insert(QStringLiteral("zones"), QJsonValue::Null);
    }
    if (lighting.speed) {
        object.insert(QStringLiteral("speed"), static_cast<qint64>(*lighting.speed));
    }
    return object;
}

QJsonObject titleFallbackToJson(const TitleFallback &fallback)
{
    QJsonObject object;
    object.insert(QStringLiteral("enabled"), fallback.enabled);
    object.insert(QStringLiteral("mode"), titleMatchModeJsonName(fallback.mode));
    object.insert(QStringLiteral("pattern"), fallback.pattern);
    return object;
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

} // namespace

ProfileStore::ProfileStore(QString configRoot)
    : m_configRoot(std::move(configRoot))
{
}

QString ProfileStore::configRoot() const
{
    if (!m_configRoot.isEmpty()) {
        return m_configRoot;
    }
    const QString xdg = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (!xdg.isEmpty()) {
        return xdg;
    }
    return QDir::homePath() + QStringLiteral("/.config");
}

QString ProfileStore::documentPath() const
{
    return configRoot() + QStringLiteral("/contextdeck/profiles.json");
}

QString ProfileStore::backupPath() const
{
    return documentPath() + QStringLiteral(".bak");
}

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

QByteArray ProfileStore::toJsonBytes(const ProfileDocument &document)
{
    return QJsonDocument(toJson(document)).toJson(QJsonDocument::Indented);
}

LoadOutcome ProfileStore::load() const
{
    LoadOutcome outcome;
    const QString path = documentPath();
    QFile file(path);
    if (!file.exists()) {
        outcome.ok = true;
        outcome.missing = true;
        return outcome;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        outcome.error = makeError(QStringLiteral("unable to read configuration"), path);
        return outcome;
    }
    const QByteArray bytes = file.readAll();
    return parseDocument(bytes, path);
}

SaveOutcome ProfileStore::save(const ProfileDocument &document) const
{
    SaveOutcome outcome;
    const PersistenceError validation = validate(document);
    if (!validation.reason.isEmpty()) {
        outcome.error = validation;
        return outcome;
    }

    const LoadOutcome parsed = parseDocument(toJsonBytes(document), QStringLiteral("$"));
    if (!parsed.ok) {
        outcome.error = parsed.error;
        return outcome;
    }

    const QString path = documentPath();
    QDir dir = QFileInfo(path).dir();
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        outcome.error = makeError(QStringLiteral("unable to create configuration directory"), path);
        return outcome;
    }

    const QByteArray bytes = toJsonBytes(document);
    if (bytes.size() > kMaxDocumentBytes) {
        outcome.error = makeError(QStringLiteral("document exceeds 1 MiB bound"), path);
        return outcome;
    }

    auto writeAtomically = [](const QString &target, const QByteArray &payload) -> bool {
        QSaveFile writer(target);
        writer.setDirectWriteFallback(false);
        if (!writer.open(QIODevice::WriteOnly)) {
            return false;
        }
        if (writer.write(payload) != payload.size()) {
            writer.cancelWriting();
            return false;
        }
        return writer.commit();
    };

    QFile existing(path);
    const bool hadExisting = existing.exists();
    if (hadExisting) {
        if (!existing.open(QIODevice::ReadOnly)) {
            outcome.error = makeError(QStringLiteral("unable to read existing configuration; original file preserved"),
                                      path);
            return outcome;
        }
        const QByteArray original = existing.readAll();
        existing.close();
        const LoadOutcome existingParsed = parseDocument(original, path);
        if (!existingParsed.ok || existingParsed.migrationFallback) {
            outcome.error = makeError(QStringLiteral("refusing to overwrite unsupported or invalid existing document"),
                                      path);
            outcome.error.preserved = true;
            return outcome;
        }
        if (!writeAtomically(backupPath(), original)) {
            outcome.error = makeError(QStringLiteral("unable to write backup; original file preserved"), path);
            return outcome;
        }
    }

    if (!writeAtomically(path, bytes)) {
        outcome.error = makeError(QStringLiteral("write failed; original file preserved"), path);
        return outcome;
    }

    outcome.ok = true;
    return outcome;
}

} // namespace contextdeck
