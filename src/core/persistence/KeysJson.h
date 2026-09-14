#pragma once

#include "core/Types.h"

#include <QJsonObject>
#include <QJsonValue>

namespace contextdeck::persistence {

bool parseKeys(const QJsonValue &value, const QString &path, bool allowInherit, QHash<ControlId, Assignment> &out,
               PersistenceError &error);
QJsonObject keysToJson(const QHash<ControlId, Assignment> &keys);

} // namespace contextdeck::persistence
