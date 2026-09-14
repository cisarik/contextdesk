#include "core/persistence/MatchJson.h"

#include "core/persistence/JsonCommon.h"

#include <QJsonObject>

namespace contextdeck::persistence {

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

QJsonObject titleFallbackToJson(const TitleFallback &fallback)
{
    QJsonObject object;
    object.insert(QStringLiteral("enabled"), fallback.enabled);
    object.insert(QStringLiteral("mode"), titleMatchModeJsonName(fallback.mode));
    object.insert(QStringLiteral("pattern"), fallback.pattern);
    return object;
}

} // namespace contextdeck::persistence
