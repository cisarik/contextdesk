#include "core/persistence/LightingJson.h"

#include "core/ControlCatalog.h"
#include "core/persistence/JsonCommon.h"

#include <QJsonArray>
#include <QJsonObject>

namespace contextdeck::persistence {
namespace {

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

} // namespace

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

} // namespace contextdeck::persistence
