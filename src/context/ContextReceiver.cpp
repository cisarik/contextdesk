#include "context/ContextReceiver.h"

#include "context/DBusNames.h"

#include <cmath>
#include <limits>

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSet>
#include <QTimer>
#include <QVariant>

namespace contextdeck {

Q_LOGGING_CATEGORY(lcContext, "contextdeck.context")

namespace {

QString identityToJson(const ApplicationIdentity &identity)
{
    QJsonObject object;
    object.insert(QStringLiteral("desktop_file_name"), identity.desktopFileName);
    object.insert(QStringLiteral("resource_class"), identity.resourceClass);
    object.insert(QStringLiteral("resource_name"), identity.resourceName);
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

bool boundedString(const QVariant &value, qsizetype maxBytes, QString &out)
{
    if (value.metaType().id() != QMetaType::QString) {
        return false;
    }
    out = value.toString();
    if (out.toUtf8().size() > maxBytes) {
        return false;
    }
    return true;
}

bool asUint32(const QVariant &value, quint32 &out)
{
    switch (value.metaType().id()) {
    case QMetaType::UInt:
        out = value.toUInt();
        return true;
    case QMetaType::Int: {
        const int number = value.toInt();
        if (number < 0) {
            return false;
        }
        out = static_cast<quint32>(number);
        return true;
    }
    case QMetaType::ULongLong: {
        const qulonglong number = value.toULongLong();
        if (number > std::numeric_limits<quint32>::max()) {
            return false;
        }
        out = static_cast<quint32>(number);
        return true;
    }
    case QMetaType::LongLong: {
        const qlonglong number = value.toLongLong();
        if (number < 0 || number > static_cast<qlonglong>(std::numeric_limits<quint32>::max())) {
            return false;
        }
        out = static_cast<quint32>(number);
        return true;
    }
    case QMetaType::Double: {
        const double number = value.toDouble();
        if (!std::isfinite(number) || number < 0 || number > static_cast<double>(std::numeric_limits<quint32>::max())) {
            return false;
        }
        if (number != std::floor(number)) {
            return false;
        }
        out = static_cast<quint32>(number);
        return true;
    }
    case QMetaType::QString: {
        bool ok = false;
        const quint32 number = value.toString().toUInt(&ok, 10);
        if (!ok) {
            return false;
        }
        out = number;
        return true;
    }
    default:
        return false;
    }
}

bool asInt64(const QVariant &value, qint64 &out)
{
    switch (value.metaType().id()) {
    case QMetaType::LongLong:
        out = value.toLongLong();
        return true;
    case QMetaType::Int:
        out = value.toInt();
        return true;
    case QMetaType::UInt:
        out = static_cast<qint64>(value.toUInt());
        return true;
    case QMetaType::ULongLong: {
        const qulonglong number = value.toULongLong();
        if (number > static_cast<qulonglong>(std::numeric_limits<qint64>::max())) {
            return false;
        }
        out = static_cast<qint64>(number);
        return true;
    }
    case QMetaType::Double: {
        const double number = value.toDouble();
        if (!std::isfinite(number) || number < static_cast<double>(std::numeric_limits<qint64>::min())
            || number > static_cast<double>(std::numeric_limits<qint64>::max())) {
            return false;
        }
        if (number != std::floor(number)) {
            return false;
        }
        out = static_cast<qint64>(number);
        return true;
    }
    case QMetaType::QString: {
        bool ok = false;
        const qint64 number = value.toString().toLongLong(&ok, 10);
        if (!ok) {
            return false;
        }
        out = number;
        return true;
    }
    default:
        return false;
    }
}

const QString kIntrospection = QStringLiteral(
    "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\" "
    "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
    "<node>\n"
    "  <interface name=\"org.freedesktop.DBus.Introspectable\">\n"
    "    <method name=\"Introspect\">\n"
    "      <arg direction=\"out\" type=\"s\" name=\"xml\"/>\n"
    "    </method>\n"
    "  </interface>\n"
    "  <interface name=\"org.freedesktop.DBus.Properties\">\n"
    "    <method name=\"Get\">\n"
    "      <arg direction=\"in\" type=\"s\" name=\"interface\"/>\n"
    "      <arg direction=\"in\" type=\"s\" name=\"name\"/>\n"
    "      <arg direction=\"out\" type=\"v\" name=\"value\"/>\n"
    "    </method>\n"
    "    <method name=\"GetAll\">\n"
    "      <arg direction=\"in\" type=\"s\" name=\"interface\"/>\n"
    "      <arg direction=\"out\" type=\"a{sv}\" name=\"properties\"/>\n"
    "    </method>\n"
    "    <method name=\"Set\">\n"
    "      <arg direction=\"in\" type=\"s\" name=\"interface\"/>\n"
    "      <arg direction=\"in\" type=\"s\" name=\"name\"/>\n"
    "      <arg direction=\"in\" type=\"v\" name=\"value\"/>\n"
    "    </method>\n"
    "  </interface>\n"
    "  <interface name=\"io.github.cisarik.ContextDeck.Context1\">\n"
    "    <method name=\"ContextReport\">\n"
    "      <arg direction=\"in\" type=\"s\" name=\"bridge_id\"/>\n"
    "      <arg direction=\"in\" type=\"u\" name=\"sequence\"/>\n"
    "      <arg direction=\"in\" type=\"s\" name=\"desktop_file_name\"/>\n"
    "      <arg direction=\"in\" type=\"s\" name=\"resource_class\"/>\n"
    "      <arg direction=\"in\" type=\"s\" name=\"resource_name\"/>\n"
    "      <arg direction=\"in\" type=\"x\" name=\"parent_window_id\"/>\n"
    "    </method>\n"
    "    <method name=\"InventoryReport\">\n"
    "      <arg direction=\"in\" type=\"s\" name=\"bridge_id\"/>\n"
    "      <arg direction=\"in\" type=\"u\" name=\"sequence\"/>\n"
    "      <arg direction=\"in\" type=\"s\" name=\"payload_json\"/>\n"
    "    </method>\n"
    "    <method name=\"Heartbeat\">\n"
    "      <arg direction=\"in\" type=\"s\" name=\"bridge_id\"/>\n"
    "      <arg direction=\"in\" type=\"u\" name=\"sequence\"/>\n"
    "    </method>\n"
    "    <property name=\"CurrentIdentity\" type=\"s\" access=\"read\"/>\n"
    "    <property name=\"BridgeConnected\" type=\"b\" access=\"read\"/>\n"
    "    <property name=\"PolicyRevision\" type=\"u\" access=\"read\"/>\n"
    "  </interface>\n"
    "</node>\n");

} // namespace

class ContextReceiver::Object : public QDBusVirtualObject
{
public:
    explicit Object(ContextReceiver *receiver)
        : QDBusVirtualObject(receiver)
        , m_receiver(receiver)
    {
    }

    QString introspect(const QString &path) const override
    {
        Q_UNUSED(path);
        return kIntrospection;
    }

    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override
    {
        const QString interface = message.interface();
        const QString member = message.member();

        if (interface == QLatin1String("org.freedesktop.DBus.Introspectable") && member == QLatin1String("Introspect")) {
            return connection.send(message.createReply(QVariantList{introspect(message.path())}));
        }

        if (interface == QLatin1String("org.freedesktop.DBus.Properties")) {
            return handleProperties(message, connection);
        }

        if (interface != QLatin1String(kContextInterface.latin1()) && !interface.isEmpty()) {
            return connection.send(message.createErrorReply(QDBusError::UnknownInterface, interface));
        }

        const QVariantList args = message.arguments();
        if (member == QLatin1String("ContextReport")) {
            if (args.size() != 6) {
                return connection.send(message.createErrorReply(QDBusError::InvalidArgs, QStringLiteral("ContextReport expects 6 arguments")));
            }
            QString bridgeId;
            QString desktop;
            QString resourceClass;
            QString resourceName;
            quint32 sequence = 0;
            qint64 parentWindowId = 0;
            if (!boundedString(args.at(0), kMaxBridgeIdBytes, bridgeId) || !asUint32(args.at(1), sequence)
                || !boundedString(args.at(2), kMaxDbusStringBytes, desktop)
                || !boundedString(args.at(3), kMaxDbusStringBytes, resourceClass)
                || !boundedString(args.at(4), kMaxDbusStringBytes, resourceName) || !asInt64(args.at(5), parentWindowId)) {
                qCWarning(lcContext) << "rejected ContextReport: unbounded or mistyped argument";
                return connection.send(message.createErrorReply(QDBusError::InvalidArgs, QStringLiteral("invalid ContextReport argument")));
            }
            ApplicationIdentity identity;
            identity.desktopFileName = desktop;
            identity.resourceClass = resourceClass;
            identity.resourceName = resourceName;
            m_receiver->onContextReport(bridgeId, sequence, identity, parentWindowId);
            return connection.send(message.createReply());
        }

        if (member == QLatin1String("InventoryReport")) {
            if (args.size() != 3) {
                return connection.send(message.createErrorReply(QDBusError::InvalidArgs, QStringLiteral("InventoryReport expects 3 arguments")));
            }
            QString bridgeId;
            QString payload;
            quint32 sequence = 0;
            if (!boundedString(args.at(0), kMaxBridgeIdBytes, bridgeId) || !asUint32(args.at(1), sequence)) {
                return connection.send(message.createErrorReply(QDBusError::InvalidArgs, QStringLiteral("invalid InventoryReport argument")));
            }
            if (args.at(2).metaType().id() != QMetaType::QString) {
                return connection.send(message.createErrorReply(QDBusError::InvalidArgs, QStringLiteral("payload_json must be a string")));
            }
            payload = args.at(2).toString();
            if (payload.toUtf8().size() > kMaxInventoryBytes) {
                qCWarning(lcContext) << "rejected InventoryReport: payload exceeds 64 KiB";
                return connection.send(message.createErrorReply(QDBusError::InvalidArgs, QStringLiteral("payload exceeds 64 KiB")));
            }
            m_receiver->onInventoryReport(bridgeId, sequence, payload);
            return connection.send(message.createReply());
        }

        if (member == QLatin1String("Heartbeat")) {
            if (args.size() != 2) {
                return connection.send(message.createErrorReply(QDBusError::InvalidArgs, QStringLiteral("Heartbeat expects 2 arguments")));
            }
            QString bridgeId;
            quint32 sequence = 0;
            if (!boundedString(args.at(0), kMaxBridgeIdBytes, bridgeId) || !asUint32(args.at(1), sequence)) {
                return connection.send(message.createErrorReply(QDBusError::InvalidArgs, QStringLiteral("invalid Heartbeat argument")));
            }
            m_receiver->onHeartbeat(bridgeId, sequence);
            return connection.send(message.createReply());
        }

        return connection.send(message.createErrorReply(QDBusError::UnknownMethod, member));
    }

private:
    bool handleProperties(const QDBusMessage &message, const QDBusConnection &connection) const
    {
        const QString member = message.member();
        const QVariantList args = message.arguments();
        if (member == QLatin1String("Get") && args.size() == 2) {
            const QString interface = args.at(0).toString();
            const QString name = args.at(1).toString();
            if (!interface.isEmpty() && interface != QLatin1String(kContextInterface.latin1())) {
                return connection.send(message.createErrorReply(QDBusError::UnknownInterface, interface));
            }
            QVariant value;
            if (!propertyValue(name, value)) {
                return connection.send(message.createErrorReply(QDBusError::UnknownProperty, name));
            }
            return connection.send(message.createReply(QVariantList{QVariant::fromValue(QDBusVariant(value))}));
        }
        if (member == QLatin1String("GetAll") && args.size() == 1) {
            const QString interface = args.at(0).toString();
            if (!interface.isEmpty() && interface != QLatin1String(kContextInterface.latin1())) {
                return connection.send(message.createErrorReply(QDBusError::UnknownInterface, interface));
            }
            QVariantMap map;
            map.insert(QStringLiteral("CurrentIdentity"), m_receiver->currentIdentity());
            map.insert(QStringLiteral("BridgeConnected"), m_receiver->bridgeConnected());
            map.insert(QStringLiteral("PolicyRevision"), m_receiver->policyRevision());
            return connection.send(message.createReply(QVariantList{map}));
        }
        if (member == QLatin1String("Set")) {
            return connection.send(message.createErrorReply(QDBusError::PropertyReadOnly, QStringLiteral("properties are read-only")));
        }
        return connection.send(message.createErrorReply(QDBusError::UnknownMethod, member));
    }

    bool propertyValue(const QString &name, QVariant &value) const
    {
        if (name == QLatin1String("CurrentIdentity")) {
            value = m_receiver->currentIdentity();
            return true;
        }
        if (name == QLatin1String("BridgeConnected")) {
            value = m_receiver->bridgeConnected();
            return true;
        }
        if (name == QLatin1String("PolicyRevision")) {
            value = m_receiver->policyRevision();
            return true;
        }
        return false;
    }

    ContextReceiver *m_receiver = nullptr;
};

ContextReceiver::ContextReceiver(QObject *parent)
    : QObject(parent)
{
    auto *watchdog = new QTimer(this);
    watchdog->setInterval(kHeartbeatIntervalMs);
    connect(watchdog, &QTimer::timeout, this, &ContextReceiver::onWatchdog);
    watchdog->start();
}

ContextReceiver::~ContextReceiver() = default;

bool ContextReceiver::start()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        m_degraded = true;
        m_lastError = QStringLiteral("session bus unavailable");
        qCWarning(lcContext) << "session bus unavailable; running degraded";
        emit degradedChanged();
        return false;
    }

    QDBusConnectionInterface *iface = bus.interface();
    const QDBusReply<QDBusConnectionInterface::RegisterServiceReply> reply = iface->registerService(
        QString(kServiceName),
        QDBusConnectionInterface::DontQueueService,
        QDBusConnectionInterface::DontAllowReplacement);
    if (!reply.isValid() || reply.value() != QDBusConnectionInterface::ServiceRegistered) {
        m_degraded = true;
        m_lastError = QStringLiteral("session bus name already taken");
        qCWarning(lcContext) << "bus name taken; running degraded read-only (not replacing owner)";
        emit degradedChanged();
        return false;
    }

    m_object = new Object(this);
    if (!bus.registerVirtualObject(QString(kContextObjectPath), m_object, QDBusConnection::SingleNode)) {
        m_degraded = true;
        m_lastError = QStringLiteral("failed to register context object");
        qCWarning(lcContext) << "failed to register context object; running degraded";
        emit degradedChanged();
        return false;
    }

    qCInfo(lcContext) << "session bus name registered:" << kServiceName;
    return true;
}

QString ContextReceiver::currentIdentity() const
{
    return identityToJson(m_identity);
}

bool ContextReceiver::acceptSequence(const QString &bridgeId, quint32 sequence)
{
    if (m_degraded) {
        return false;
    }
    if (bridgeId.isEmpty()) {
        return false;
    }
    if (m_bridgeId.isEmpty()) {
        m_bridgeId = bridgeId;
        m_lastSequence = sequence;
        return true;
    }
    if (bridgeId != m_bridgeId) {
        qCWarning(lcContext) << "rejected report from unexpected bridge instance";
        return false;
    }
    if (sequence <= m_lastSequence) {
        qCWarning(lcContext) << "rejected stale or out-of-order sequence";
        return false;
    }
    m_lastSequence = sequence;
    return true;
}

void ContextReceiver::onContextReport(const QString &bridgeId, quint32 sequence, const ApplicationIdentity &identity,
                                      qint64 parentWindowId)
{
    Q_UNUSED(parentWindowId);
    if (!acceptSequence(bridgeId, sequence)) {
        return;
    }
    m_lastHeartbeatMs = QDateTime::currentMSecsSinceEpoch();
    const bool wasConnected = m_bridgeConnected;
    m_bridgeConnected = true;
    m_warnedLoss = false;
    if (!wasConnected) {
        emit bridgeConnectedChanged();
        qCInfo(lcContext) << "bridge connected";
    }

    const bool changed = identity.desktopFileName != m_identity.desktopFileName
        || identity.resourceClass != m_identity.resourceClass
        || identity.resourceName != m_identity.resourceName;
    m_identity = identity;
    if (changed) {
        bumpPolicy();
        emit currentIdentityChanged();
        qCInfo(lcContext) << "context identity updated, policy" << m_policyRevision;
    }
}

void ContextReceiver::onInventoryReport(const QString &bridgeId, quint32 sequence, const QString &payloadJson)
{
    if (!acceptSequence(bridgeId, sequence)) {
        return;
    }
    m_lastHeartbeatMs = QDateTime::currentMSecsSinceEpoch();
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payloadJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        qCWarning(lcContext) << "rejected inventory: invalid JSON";
        return;
    }
    const QJsonObject root = document.object();
    if (!root.contains(QStringLiteral("entries")) || !root.value(QStringLiteral("entries")).isArray()) {
        qCWarning(lcContext) << "rejected inventory: entries array required";
        return;
    }
    for (const QString &key : root.keys()) {
        if (key != QLatin1String("entries")) {
            qCWarning(lcContext) << "rejected inventory: unknown semantic field";
            return;
        }
    }

    QVector<InventoryEntry> entries;
    QSet<QString> seen;
    const QJsonArray array = root.value(QStringLiteral("entries")).toArray();
    if (array.size() > kMaxInventoryEntries) {
        qCWarning(lcContext) << "rejected inventory: more than 200 entries";
        return;
    }
    for (const QJsonValue &value : array) {
        if (!value.isObject()) {
            qCWarning(lcContext) << "rejected inventory: entry must be an object";
            return;
        }
        const QJsonObject object = value.toObject();
        for (const QString &key : object.keys()) {
            if (key != QLatin1String("desktop_file_name") && key != QLatin1String("resource_class")
                && key != QLatin1String("resource_name")) {
                qCWarning(lcContext) << "rejected inventory: unknown identity field";
                return;
            }
        }
        InventoryEntry entry;
        entry.desktopFileName = object.value(QStringLiteral("desktop_file_name")).toString();
        entry.resourceClass = object.value(QStringLiteral("resource_class")).toString();
        entry.resourceName = object.value(QStringLiteral("resource_name")).toString();
        if (entry.desktopFileName.size() > kMaxDbusStringBytes || entry.resourceClass.size() > kMaxDbusStringBytes
            || entry.resourceName.size() > kMaxDbusStringBytes) {
            qCWarning(lcContext) << "rejected inventory: identity field too long";
            return;
        }
        const QString key = entry.identityKey();
        if (seen.contains(key)) {
            continue;
        }
        seen.insert(key);
        entries.push_back(entry);
        if (entries.size() > kMaxInventoryEntries) {
            qCWarning(lcContext) << "rejected inventory: more than 200 unique entries";
            return;
        }
    }

    if (entries != m_inventory) {
        m_inventory = std::move(entries);
        bumpPolicy();
        emit inventoryChanged();
        qCInfo(lcContext) << "inventory updated, entries" << m_inventory.size() << "policy" << m_policyRevision;
    }
}

void ContextReceiver::onHeartbeat(const QString &bridgeId, quint32 sequence)
{
    if (!acceptSequence(bridgeId, sequence)) {
        return;
    }
    m_lastHeartbeatMs = QDateTime::currentMSecsSinceEpoch();
    if (!m_bridgeConnected) {
        m_bridgeConnected = true;
        m_warnedLoss = false;
        emit bridgeConnectedChanged();
        qCInfo(lcContext) << "bridge connected";
    }
}

void ContextReceiver::onWatchdog()
{
    if (!m_bridgeConnected) {
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 limit = static_cast<qint64>(kHeartbeatIntervalMs) * kMissedHeartbeatsForLoss;
    if (m_lastHeartbeatMs == 0 || now - m_lastHeartbeatMs <= limit) {
        return;
    }
    markBridgeLost(QStringLiteral("missed heartbeats"));
}

void ContextReceiver::markBridgeLost(const QString &reason)
{
    if (!m_bridgeConnected && m_identity.desktopFileName.isEmpty() && m_identity.resourceClass.isEmpty()
        && m_identity.resourceName.isEmpty()) {
        return;
    }
    m_bridgeConnected = false;
    m_identity = {};
    if (!m_warnedLoss) {
        qCWarning(lcContext) << "bridge lost:" << reason << "- context unknown, falling back to global profile";
        m_warnedLoss = true;
    }
    bumpPolicy();
    emit bridgeConnectedChanged();
    emit currentIdentityChanged();
    emit bridgeLost();
}

void ContextReceiver::bumpPolicy()
{
    ++m_policyRevision;
    emit policyRevisionChanged();
}

} // namespace contextdeck
