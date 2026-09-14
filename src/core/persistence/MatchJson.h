#pragma once

#include "core/Types.h"

#include <QJsonObject>
#include <optional>

namespace contextdeck::persistence {

std::optional<MatchSpec> parseMatch(const QJsonObject &object, const QString &path, PersistenceError &error);
std::optional<TitleFallback> parseTitleFallback(const QJsonObject &object, const QString &path, PersistenceError &error);
QJsonObject titleFallbackToJson(const TitleFallback &fallback);

} // namespace contextdeck::persistence
