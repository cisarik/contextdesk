#include "context/WorkspaceStateCodec.h"

#include <QDBusVariant>
#include <QSet>

#include <algorithm>
#include <limits>

namespace contextdeck {

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

bool decodePosition(const QDBusArgument &arg, int &position)
{
    const QString signature = arg.currentSignature();
    if (signature.startsWith(QLatin1Char('i')) || signature.startsWith(QLatin1Char('n'))) {
        qint32 value = 0;
        arg >> value;
        if (value < 0 || value > kMaxPosition) {
            return false;
        }
        position = static_cast<int>(value);
        return true;
    }
    if (signature.startsWith(QLatin1Char('u')) || signature.startsWith(QLatin1Char('q'))) {
        quint32 value = 0;
        arg >> value;
        if (value > static_cast<quint32>(kMaxPosition)) {
            return false;
        }
        position = static_cast<int>(value);
        return true;
    }
    return false;
}

bool decodeDesktopStructure(const QDBusArgument &arg, int &position, QString &id, QString &name)
{
    if (arg.currentType() != QDBusArgument::StructureType) {
        return false;
    }
    arg.beginStructure();
    if (!decodePosition(arg, position)) {
        return false;
    }
    arg >> id >> name;
    arg.endStructure();
    return true;
}

QVariant unwrapDbusVariant(QVariant value)
{
    while (value.canConvert<QDBusVariant>()) {
        const QVariant inner = qvariant_cast<QDBusVariant>(value).variant();
        if (!inner.isValid() || inner == value) {
            break;
        }
        value = inner;
    }
    return value;
}

bool decodeGetAllProperties(const QVariant &first, QVariantMap &properties)
{
    const QVariant unwrapped = unwrapDbusVariant(first);
    if (unwrapped.metaType() == QMetaType::fromType<QDBusArgument>()
        || unwrapped.canConvert<QDBusArgument>()) {
        const QDBusArgument argument = qvariant_cast<QDBusArgument>(unwrapped);
        if (argument.currentType() != QDBusArgument::MapType) {
            argument >> properties;
            return true;
        }
        argument.beginMap();
        while (!argument.atEnd()) {
            argument.beginMapEntry();
            QString key;
            QDBusVariant dbusValue;
            argument >> key >> dbusValue;
            argument.endMapEntry();
            properties.insert(key, dbusValue.variant());
        }
        argument.endMap();
        return true;
    }
    if (unwrapped.metaType() == QMetaType::fromType<QVariantMap>() || unwrapped.canConvert<QVariantMap>()) {
        properties = unwrapped.toMap();
        return true;
    }
    return false;
}

bool decodeCount(const QVariant &value, int &count)
{
    bool ok = false;
    qlonglong number = value.toLongLong(&ok);
    if (!ok) {
        const qulonglong unsignedNumber = value.toULongLong(&ok);
        if (!ok || unsignedNumber > static_cast<qulonglong>(std::numeric_limits<int>::max())) {
            return false;
        }
        number = static_cast<qlonglong>(unsignedNumber);
    }
    if (number < kMinDesktopCount || number > kMaxDesktopCount) {
        return false;
    }
    count = static_cast<int>(number);
    return true;
}

std::optional<WorkspaceState> decodeWorkspaceSnapshot(const QVariantMap &properties, QString &errorClass)
{
    if (!properties.contains(QStringLiteral("count")) || !properties.contains(QStringLiteral("current"))
        || !properties.contains(QStringLiteral("desktops"))) {
        errorClass = QStringLiteral("malformed-snapshot");
        return std::nullopt;
    }

    int count = 0;
    if (!decodeCount(unwrapDbusVariant(properties.value(QStringLiteral("count"))), count)) {
        errorClass = QStringLiteral("count-invalid");
        return std::nullopt;
    }
    const QString current = unwrapDbusVariant(properties.value(QStringLiteral("current"))).toString();
    if (current.isEmpty() || !boundedUtf8(current, kMaxDesktopIdBytes) || hasControlCharacters(current)) {
        errorClass = QStringLiteral("current-invalid");
        return std::nullopt;
    }

    std::optional<int> rows;
    if (properties.contains(QStringLiteral("rows"))) {
        bool ok = false;
        const qlonglong rowCount = unwrapDbusVariant(properties.value(QStringLiteral("rows"))).toLongLong(&ok);
        if (!ok || rowCount < 0 || rowCount > kMaxPosition) {
            errorClass = QStringLiteral("rows-invalid");
            return std::nullopt;
        }
        rows = static_cast<int>(rowCount);
    }

    std::optional<bool> navigationWrappingAround;
    if (properties.contains(QStringLiteral("navigationWrappingAround"))) {
        const QVariant wrapping = unwrapDbusVariant(properties.value(QStringLiteral("navigationWrappingAround")));
        if (wrapping.metaType().id() != QMetaType::Bool) {
            errorClass = QStringLiteral("wrapping-invalid");
            return std::nullopt;
        }
        navigationWrappingAround = wrapping.toBool();
    }

    struct RawDesktop {
        int position = 0;
        QString id;
        QString name;
    };
    QVector<RawDesktop> raw;
    auto appendRaw = [&](int position, const QString &id, const QString &name) -> bool {
        if (position < 0 || position > kMaxPosition) {
            errorClass = QStringLiteral("position-invalid");
            return false;
        }
        if (id.isEmpty() || !boundedUtf8(id, kMaxDesktopIdBytes) || hasControlCharacters(id)) {
            errorClass = QStringLiteral("desktop-id-invalid");
            return false;
        }
        if (!boundedUtf8(name, kMaxDesktopNameBytes) || hasControlCharacters(name)) {
            errorClass = QStringLiteral("desktop-name-invalid");
            return false;
        }
        raw.push_back(RawDesktop{position, id, name});
        return true;
    };

    const QVariant desktopsValue = unwrapDbusVariant(properties.value(QStringLiteral("desktops")));
    if (desktopsValue.metaType() == QMetaType::fromType<QList<VirtualDesktopDBus>>()) {
        const auto rows = desktopsValue.value<QList<VirtualDesktopDBus>>();
        for (const VirtualDesktopDBus &row : rows) {
            if (!appendRaw(row.position, row.id, row.name)) {
                return std::nullopt;
            }
        }
    } else if (desktopsValue.metaType() == QMetaType::fromType<QList<VirtualDesktopDBusUnsigned>>()) {
        const auto rows = desktopsValue.value<QList<VirtualDesktopDBusUnsigned>>();
        for (const VirtualDesktopDBusUnsigned &row : rows) {
            if (row.position > static_cast<quint32>(kMaxPosition)) {
                errorClass = QStringLiteral("position-invalid");
                return std::nullopt;
            }
            if (!appendRaw(static_cast<int>(row.position), row.id, row.name)) {
                return std::nullopt;
            }
        }
    } else if (desktopsValue.metaType() == QMetaType::fromType<QDBusArgument>()
               || desktopsValue.canConvert<QDBusArgument>()) {
        const QDBusArgument arg = qvariant_cast<QDBusArgument>(desktopsValue);
        if (arg.currentType() != QDBusArgument::ArrayType) {
            errorClass = QStringLiteral("desktops-type");
            return std::nullopt;
        }
        arg.beginArray();
        while (!arg.atEnd()) {
            if (arg.currentType() != QDBusArgument::StructureType) {
                errorClass = QStringLiteral("desktop-shape");
                return std::nullopt;
            }
            int position = 0;
            QString id;
            QString name;
            if (!decodeDesktopStructure(arg, position, id, name)) {
                errorClass = QStringLiteral("position-invalid");
                return std::nullopt;
            }
            if (!appendRaw(position, id, name)) {
                return std::nullopt;
            }
        }
        arg.endArray();
    } else if (desktopsValue.canConvert<QList<VirtualDesktopDBus>>()) {
        const auto rows = qvariant_cast<QList<VirtualDesktopDBus>>(desktopsValue);
        for (const VirtualDesktopDBus &row : rows) {
            if (!appendRaw(row.position, row.id, row.name)) {
                return std::nullopt;
            }
        }
    } else if (desktopsValue.canConvert<QList<VirtualDesktopDBusUnsigned>>()) {
        const auto rows = qvariant_cast<QList<VirtualDesktopDBusUnsigned>>(desktopsValue);
        for (const VirtualDesktopDBusUnsigned &row : rows) {
            if (row.position > static_cast<quint32>(kMaxPosition)) {
                errorClass = QStringLiteral("position-invalid");
                return std::nullopt;
            }
            if (!appendRaw(static_cast<int>(row.position), row.id, row.name)) {
                return std::nullopt;
            }
        }
    } else {
        errorClass = QStringLiteral("desktops-type");
        return std::nullopt;
    }

    qsizetype metadataBytes = 0;
    for (const RawDesktop &row : raw) {
        metadataBytes += row.id.toUtf8().size() + row.name.toUtf8().size();
        if (metadataBytes > kMaxMetadataBytes) {
            errorClass = QStringLiteral("metadata-limit");
            return std::nullopt;
        }
    }

    QVector<WorkspaceDesktop> desktops;
    QSet<QString> ids;
    QSet<int> positions;
    desktops.reserve(raw.size());
    for (const RawDesktop &row : raw) {
        if (ids.contains(row.id)) {
            errorClass = QStringLiteral("desktop-id-invalid");
            return std::nullopt;
        }
        if (positions.contains(row.position)) {
            errorClass = QStringLiteral("position-duplicate");
            return std::nullopt;
        }
        ids.insert(row.id);
        positions.insert(row.position);
        WorkspaceDesktop desktop;
        desktop.position = row.position;
        desktop.id = row.id;
        desktop.displayName = row.name;
        desktops.push_back(std::move(desktop));
    }

    if (desktops.size() != count) {
        errorClass = QStringLiteral("count-mismatch");
        return std::nullopt;
    }

    std::sort(desktops.begin(), desktops.end(), [](const WorkspaceDesktop &left, const WorkspaceDesktop &right) {
        return left.position < right.position;
    });

    int currentOrdinal = 0;
    for (int i = 0; i < desktops.size(); ++i) {
        desktops[i].ordinal = i + 1;
        if (desktops[i].displayName.isEmpty()) {
            desktops[i].displayName = QStringLiteral("Plocha %1").arg(desktops[i].ordinal);
        }
        if (desktops[i].id == current) {
            currentOrdinal = desktops[i].ordinal;
        }
    }
    if (currentOrdinal == 0) {
        errorClass = QStringLiteral("current-missing");
        return std::nullopt;
    }

    WorkspaceState state;
    state.availability = WorkspaceAvailability::Available;
    state.desktops = std::move(desktops);
    state.currentId = current;
    state.currentOrdinal = currentOrdinal;
    state.rows = rows;
    state.navigationWrappingAround = navigationWrappingAround;
    return state;
}

} // namespace contextdeck
