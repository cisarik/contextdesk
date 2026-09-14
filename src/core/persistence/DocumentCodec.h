#pragma once

#include "core/Types.h"

#include <QJsonObject>
#include <optional>

namespace contextdeck::persistence {

std::optional<Preferences> parsePreferences(const QJsonObject &object, const QString &path, int schemaVersion,
                                            PersistenceError &error);

} // namespace contextdeck::persistence
