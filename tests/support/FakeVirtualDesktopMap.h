#pragma once

#include "context/WorkspaceReceiver.h"

#include <QDBusArgument>
#include <QDBusVariant>
#include <QString>
#include <QVariantMap>
#include <QVector>

#include <optional>

struct DesktopTuple {
    qint32 position = 0;
    QString id;
    QString name;
};

Q_DECLARE_METATYPE(DesktopTuple)

inline QDBusArgument &operator<<(QDBusArgument &argument, const DesktopTuple &tuple)
{
    argument.beginStructure();
    argument << tuple.position << tuple.id << tuple.name;
    argument.endStructure();
    return argument;
}

inline const QDBusArgument &operator>>(const QDBusArgument &argument, DesktopTuple &tuple)
{
    argument.beginStructure();
    argument >> tuple.position >> tuple.id >> tuple.name;
    argument.endStructure();
    return argument;
}

struct FakeVirtualDesktopGetAll {
    QVector<DesktopTuple> desktops;
    QString current;
    uint rows = 1;
    bool wrapping = false;
    bool malformed = false;
    bool missingCurrent = false;
    bool emptyCurrent = false;
    bool unsignedPositions = false;
    bool emptyName = false;
    bool malformedWrapping = false;
    bool extraKey = false;
    std::optional<int> countOverride;
};

inline QVariantMap makeVirtualDesktopGetAllMap(const QVector<DesktopTuple> &desktops, const QString &current,
                                               uint rows = 1, bool wrapping = false)
{
    QVariantMap map;
    map.insert(QStringLiteral("count"), QVariant::fromValue(static_cast<uint>(desktops.size())));
    map.insert(QStringLiteral("current"), current);
    map.insert(QStringLiteral("rows"), QVariant::fromValue(rows));
    map.insert(QStringLiteral("navigationWrappingAround"), QVariant(wrapping));
    QList<contextdeck::VirtualDesktopDBus> dbusRows;
    for (const DesktopTuple &tuple : desktops) {
        contextdeck::VirtualDesktopDBus row;
        row.position = tuple.position;
        row.id = tuple.id;
        row.name = tuple.name;
        dbusRows.push_back(row);
    }
    map.insert(QStringLiteral("desktops"), QVariant::fromValue(dbusRows));
    return map;
}

inline void writeVirtualDesktopGetAllArg(QDBusArgument &mapArg, const FakeVirtualDesktopGetAll &spec)
{
    mapArg.beginMap(QMetaType::fromType<QString>(), QMetaType::fromType<QDBusVariant>());
    auto put = [&](const QString &key, const QVariant &value) {
        mapArg.beginMapEntry();
        mapArg << key << QDBusVariant(value);
        mapArg.endMapEntry();
    };

    if (spec.malformed) {
        put(QStringLiteral("count"), QVariant::fromValue(uint(2)));
        put(QStringLiteral("current"), QVariant(spec.current));
        put(QStringLiteral("desktops"), QVariant(QStringLiteral("nope")));
    } else {
        const int count = spec.countOverride.value_or(static_cast<int>(spec.desktops.size()));
        put(QStringLiteral("count"), QVariant::fromValue(static_cast<uint>(count)));
        QString current = spec.current;
        if (spec.emptyCurrent) {
            current.clear();
        } else if (spec.missingCurrent) {
            current = QStringLiteral("missing");
        }
        put(QStringLiteral("current"), QVariant(current));
        put(QStringLiteral("rows"), QVariant::fromValue(spec.rows));
        if (spec.malformedWrapping) {
            put(QStringLiteral("navigationWrappingAround"), QVariant(QStringLiteral("yes")));
        } else {
            put(QStringLiteral("navigationWrappingAround"), QVariant(spec.wrapping));
        }
        if (spec.extraKey) {
            put(QStringLiteral("futureProperty"), QVariant(1));
        }

        QDBusArgument desktopsArg;
        if (spec.unsignedPositions) {
            desktopsArg.beginArray(qMetaTypeId<contextdeck::VirtualDesktopDBusUnsigned>());
            for (const DesktopTuple &tuple : spec.desktops) {
                contextdeck::VirtualDesktopDBusUnsigned row;
                row.position = static_cast<quint32>(tuple.position);
                row.id = tuple.id;
                row.name = spec.emptyName ? QString() : tuple.name;
                desktopsArg << row;
            }
            desktopsArg.endArray();
        } else {
            desktopsArg.beginArray(qMetaTypeId<contextdeck::VirtualDesktopDBus>());
            for (const DesktopTuple &tuple : spec.desktops) {
                contextdeck::VirtualDesktopDBus row;
                row.position = tuple.position;
                row.id = tuple.id;
                row.name = spec.emptyName ? QString() : tuple.name;
                desktopsArg << row;
            }
            desktopsArg.endArray();
        }
        put(QStringLiteral("desktops"), QVariant::fromValue(desktopsArg));
    }
    mapArg.endMap();
}
