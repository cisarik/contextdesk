#pragma once

#include "core/Types.h"

#include <QJsonObject>
#include <optional>

namespace contextdeck::persistence {

std::optional<Lighting> parseLightingV1(const QJsonObject &object, const QString &path, PersistenceError &error);
std::optional<Lighting> parseLightingCommon(const QJsonObject &object, const QString &path, int schemaVersion,
                                            bool allowDynamicRoles, PersistenceError &error);
bool isSchema1LightingMigrationFailure(const PersistenceError &error);
QJsonObject lightingToJson(const Lighting &lighting);

} // namespace contextdeck::persistence
