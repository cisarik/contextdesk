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

bool parseZoneArray(const QJsonValue &zonesValue, const QString &path, std::optional<std::array<ZoneValue, kZoneCount>> &out,
                    PersistenceError &error)
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
        parsed[static_cast<size_t>(i)].color = *color;
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
        if (!parseZoneArray(object.value(QStringLiteral("zones")), path + QStringLiteral(".zones"), lighting.zones, error)) {
            return std::nullopt;
        }
    }
    return lighting;
}

std::optional<Lighting> parseLighting(const QJsonObject &object, const QString &path, PersistenceError &error)
{
    static const QStringList allowed{
        QStringLiteral("mode"),
        QStringLiteral("base_color"),
        QStringLiteral("zones"),
        QStringLiteral("restore_mode"),
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
        if (!parseZoneArray(object.value(QStringLiteral("zones")), path + QStringLiteral(".zones"), lighting.zones, error)) {
            return std::nullopt;
        }
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

std::optional<Preferences> parsePreferences(const QJsonObject &object, const QString &path, PersistenceError &error)
{
    static const QStringList allowed{QStringLiteral("automatic_enabled"), QStringLiteral("tray_notifications")};
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
            zones.append(colorToJson(zone.color));
        }
        object.insert(QStringLiteral("zones"), zones);
    } else {
        object.insert(QStringLiteral("zones"), QJsonValue::Null);
    }
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
    if (schemaVersion != 1 && schemaVersion != kSchemaVersion) {
        outcome.error = makeError(QStringLiteral("unsupported schema_version"), QStringLiteral("schema_version"));
        return outcome;
    }

    static const QStringList allowed{
        QStringLiteral("schema_version"),
        QStringLiteral("device"),
        QStringLiteral("global"),
        QStringLiteral("applications"),
        QStringLiteral("preferences"),
    };
    if (!checkObjectKeys(root, allowed, QStringLiteral("$"), outcome.error)) {
        return outcome;
    }

    auto lightingMigrationFallback = [&]() {
        LoadOutcome fallback;
        fallback.ok = true;
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
        : parseLighting(global.value(QStringLiteral("lighting")).toObject(), QStringLiteral("global.lighting"),
                        outcome.error);
    if (!lighting) {
        if (schemaVersion == 1 && isSchema1LightingMigrationFailure(outcome.error)) {
            return lightingMigrationFallback();
        }
        return outcome;
    }
    document.globalLighting = *lighting;

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
            static const QStringList appAllowed{
                QStringLiteral("id"),
                QStringLiteral("display_name"),
                QStringLiteral("match"),
                QStringLiteral("keys"),
                QStringLiteral("lighting"),
            };
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
                    : parseLighting(object.value(QStringLiteral("lighting")).toObject(),
                                    path + QStringLiteral(".lighting"), outcome.error);
                if (!appLighting) {
                    if (schemaVersion == 1 && isSchema1LightingMigrationFailure(outcome.error)) {
                        return lightingMigrationFallback();
                    }
                    return outcome;
                }
                profile.lighting = *appLighting;
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
                                                  QStringLiteral("preferences"), outcome.error);
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
        applications.append(object);
    }
    root.insert(QStringLiteral("applications"), applications);

    QJsonObject preferences;
    preferences.insert(QStringLiteral("automatic_enabled"), document.preferences.automaticEnabled);
    preferences.insert(QStringLiteral("tray_notifications"), document.preferences.trayNotifications);
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

    QFile existing(path);
    const bool hadExisting = existing.exists();
    if (hadExisting) {
        const QString backup = backupPath();
        QFile::remove(backup);
        if (!existing.copy(backup)) {
            outcome.error = makeError(QStringLiteral("unable to write backup; original file preserved"), path);
            return outcome;
        }
    }

    QSaveFile writer(path);
    writer.setDirectWriteFallback(false);
    if (!writer.open(QIODevice::WriteOnly)) {
        outcome.error = makeError(QStringLiteral("unable to open atomic writer; original file preserved"), path);
        return outcome;
    }
    if (writer.write(bytes) != bytes.size()) {
        writer.cancelWriting();
        outcome.error = makeError(QStringLiteral("write failed; original file preserved"), path);
        return outcome;
    }
    if (!writer.commit()) {
        outcome.error = makeError(QStringLiteral("commit failed; original file preserved"), path);
        return outcome;
    }

    outcome.ok = true;
    return outcome;
}

} // namespace contextdeck
