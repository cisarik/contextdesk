#include "context/InventoryPayload.h"

#include "context/DBusNames.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSet>

namespace contextdeck {

Q_LOGGING_CATEGORY(lcContext, "contextdeck.context")

std::optional<QVector<InventoryEntry>> parseInventoryPayload(const QString &payloadJson)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payloadJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        qCWarning(lcContext) << "rejected inventory: invalid JSON";
        return std::nullopt;
    }
    const QJsonObject root = document.object();
    if (!root.contains(QStringLiteral("entries")) || !root.value(QStringLiteral("entries")).isArray()) {
        qCWarning(lcContext) << "rejected inventory: entries array required";
        return std::nullopt;
    }
    for (const QString &key : root.keys()) {
        if (key != QLatin1String("entries")) {
            qCWarning(lcContext) << "rejected inventory: unknown semantic field";
            return std::nullopt;
        }
    }

    QVector<InventoryEntry> entries;
    QSet<QString> seen;
    const QJsonArray array = root.value(QStringLiteral("entries")).toArray();
    if (array.size() > kMaxInventoryEntries) {
        qCWarning(lcContext) << "rejected inventory: more than 200 entries";
        return std::nullopt;
    }
    for (const QJsonValue &value : array) {
        if (!value.isObject()) {
            qCWarning(lcContext) << "rejected inventory: entry must be an object";
            return std::nullopt;
        }
        const QJsonObject object = value.toObject();
        for (const QString &key : object.keys()) {
            if (key != QLatin1String("desktop_file_name") && key != QLatin1String("resource_class")
                && key != QLatin1String("resource_name")) {
                qCWarning(lcContext) << "rejected inventory: unknown identity field";
                return std::nullopt;
            }
        }
        InventoryEntry entry;
        entry.desktopFileName = object.value(QStringLiteral("desktop_file_name")).toString();
        entry.resourceClass = object.value(QStringLiteral("resource_class")).toString();
        entry.resourceName = object.value(QStringLiteral("resource_name")).toString();
        if (entry.desktopFileName.size() > kMaxDbusStringBytes || entry.resourceClass.size() > kMaxDbusStringBytes
            || entry.resourceName.size() > kMaxDbusStringBytes) {
            qCWarning(lcContext) << "rejected inventory: identity field too long";
            return std::nullopt;
        }
        const QString key = entry.identityKey();
        if (seen.contains(key)) {
            continue;
        }
        seen.insert(key);
        entries.push_back(entry);
        if (entries.size() > kMaxInventoryEntries) {
            qCWarning(lcContext) << "rejected inventory: more than 200 unique entries";
            return std::nullopt;
        }
    }

    return entries;
}

} // namespace contextdeck
