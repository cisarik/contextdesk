#pragma once

#include "core/Types.h"

#include <QJsonObject>
#include <QJsonValue>

namespace contextdeck::persistence {

PersistenceError makeError(const QString &reason, const QString &jsonPath, bool preserved = true);
bool isInteger(const QJsonValue &value);
bool checkObjectKeys(const QJsonObject &object, const QStringList &allowed, const QString &path, PersistenceError &error);
bool hasControlCharacters(const QString &text);
bool boundedUtf8(const QString &text, qsizetype maxBytes);
bool looksLikeDesktopId(const QString &value);

} // namespace contextdeck::persistence
