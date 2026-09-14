#include "core/persistence/JsonCommon.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QStringList>

namespace contextdeck::persistence {

PersistenceError makeError(const QString &reason, const QString &jsonPath, bool preserved)
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

} // namespace contextdeck::persistence
