#include "context/WorkspaceReceiver.h"

#include <QDBusArgument>
#include <QDBusConnectionInterface>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QLoggingCategory>
#include <QSet>
#include <QTimer>
#include <QVariant>
#include <QVariantMap>

#include <algorithm>
#include <limits>

namespace contextdeck {

Q_LOGGING_CATEGORY(lcWorkspace, "contextdeck.workspace")

QDBusArgument &operator<<(QDBusArgument &argument, const VirtualDesktopDBus &value)
{
    argument.beginStructure();
    argument << value.position << value.id << value.name;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, VirtualDesktopDBus &value)
{
    argument.beginStructure();
    argument >> value.position >> value.id >> value.name;
    argument.endStructure();
    return argument;
}

QDBusArgument &operator<<(QDBusArgument &argument, const VirtualDesktopDBusUnsigned &value)
{
    argument.beginStructure();
    argument << value.position << value.id << value.name;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, VirtualDesktopDBusUnsigned &value)
{
    argument.beginStructure();
    argument >> value.position >> value.id >> value.name;
    argument.endStructure();
    return argument;
}

void registerWorkspaceDesktopDBusTypes()
{
    qDBusRegisterMetaType<VirtualDesktopDBus>();
    qDBusRegisterMetaType<QList<VirtualDesktopDBus>>();
    qDBusRegisterMetaType<VirtualDesktopDBusUnsigned>();
    qDBusRegisterMetaType<QList<VirtualDesktopDBusUnsigned>>();
}

namespace {

constexpr qsizetype kMaxDesktopIdBytes = 128;
constexpr qsizetype kMaxDesktopNameBytes = 256;
constexpr qsizetype kMaxMetadataBytes = 16 * 1024;
constexpr int kMinDesktopCount = 1;
constexpr int kMaxDesktopCount = 32;
constexpr int kMaxPosition = 32;

const QString kKwinService = QStringLiteral("org.kde.KWin");
const QString kManagerPath = QStringLiteral("/VirtualDesktopManager");
const QString kManagerInterface = QStringLiteral("org.kde.KWin.VirtualDesktopManager");
const QString kPropertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");
const QString kNamedConnection = QStringLiteral("contextdeck-workspace");

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

} // namespace

WorkspaceReceiver::WorkspaceReceiver(QObject *parent)
    : WorkspaceReceiver(QDBusConnection::connectToBus(QDBusConnection::SessionBus, kNamedConnection), parent)
{
    m_ownsConnection = true;
}

WorkspaceReceiver::WorkspaceReceiver(const QDBusConnection &connection, QObject *parent)
    : QObject(parent)
    , m_connection(connection)
{
    registerWorkspaceDesktopDBusTypes();
    setupTimers();
}

WorkspaceReceiver::~WorkspaceReceiver()
{
    stop();
    if (m_ownsConnection) {
        QDBusConnection::disconnectFromBus(kNamedConnection);
    }
}

void WorkspaceReceiver::setupTimers()
{
    m_coalesceTimer = new QTimer(this);
    m_coalesceTimer->setSingleShot(true);
    m_coalesceTimer->setInterval(0);
    connect(m_coalesceTimer, &QTimer::timeout, this, &WorkspaceReceiver::onCoalescedInvalidation);

    m_deadlineTimer = new QTimer(this);
    m_deadlineTimer->setSingleShot(true);
    connect(m_deadlineTimer, &QTimer::timeout, this, &WorkspaceReceiver::onDeadline);

    m_recoveryTimer = new QTimer(this);
    m_recoveryTimer->setSingleShot(true);
    connect(m_recoveryTimer, &QTimer::timeout, this, &WorkspaceReceiver::onRecovery);
}

void WorkspaceReceiver::setTimingForTest(int deadlineMs, const QVector<int> &recoveryDelaysMs)
{
    m_deadlineMs = std::max(1, deadlineMs);
    m_recoveryDelaysMs = recoveryDelaysMs;
}

bool WorkspaceReceiver::start()
{
    if (m_paused) {
        return false;
    }
    if (m_started) {
        return m_connection.isConnected();
    }
    if (!m_connection.isConnected()) {
        becomeUnknown(QStringLiteral("bus-unavailable"));
        emit diagnosticsChanged();
        return false;
    }
    m_started = true;
    subscribe();
    resolveOwner();
    if (m_uniqueOwner.isEmpty()) {
        becomeUnknown(QStringLiteral("service-missing"));
        emit diagnosticsChanged();
        return true;
    }
    m_errorClass = QStringLiteral("initial");
    m_state.refreshPending = true;
    emit diagnosticsChanged();
    requestSnapshot();
    return true;
}

void WorkspaceReceiver::stop()
{
    unsubscribe();
    m_started = false;
    abandonCurrentRequest();
    m_pendingRefresh = false;
    m_coalesceQueued = false;
    m_uniqueOwner.clear();
    if (m_coalesceTimer != nullptr) {
        m_coalesceTimer->stop();
    }
    stopDeadline();
    stopRecovery();
}

void WorkspaceReceiver::setPaused(bool paused)
{
    if (m_paused == paused) {
        return;
    }
    m_paused = paused;
    if (paused) {
        stop();
        becomeUnknown(QStringLiteral("observation-paused"));
        emit diagnosticsChanged();
        return;
    }
    start();
    emit diagnosticsChanged();
}

void WorkspaceReceiver::subscribe()
{
    if (m_subscribed || !m_connection.isConnected()) {
        return;
    }
    if (m_watcher == nullptr) {
        m_watcher = new QDBusServiceWatcher(kKwinService, m_connection, QDBusServiceWatcher::WatchForOwnerChange, this);
        connect(m_watcher, &QDBusServiceWatcher::serviceOwnerChanged, this, &WorkspaceReceiver::onServiceOwnerChanged);
    } else {
        m_watcher->setConnection(m_connection);
    }

    const QStringList signalNames{
        QStringLiteral("currentChanged"),
        QStringLiteral("countChanged"),
        QStringLiteral("desktopCreated"),
        QStringLiteral("desktopRemoved"),
        QStringLiteral("desktopDataChanged"),
    };
    for (const QString &name : signalNames) {
        m_connection.connect(kKwinService, kManagerPath, kManagerInterface, name, this,
                             SLOT(onInvalidatingMessage(QDBusMessage)));
    }
    m_subscribed = true;
}

void WorkspaceReceiver::unsubscribe()
{
    if (!m_subscribed) {
        return;
    }
    const QStringList signalNames{
        QStringLiteral("currentChanged"),
        QStringLiteral("countChanged"),
        QStringLiteral("desktopCreated"),
        QStringLiteral("desktopRemoved"),
        QStringLiteral("desktopDataChanged"),
    };
    for (const QString &name : signalNames) {
        m_connection.disconnect(kKwinService, kManagerPath, kManagerInterface, name, this,
                                SLOT(onInvalidatingMessage(QDBusMessage)));
    }
    m_subscribed = false;
}

void WorkspaceReceiver::resolveOwner()
{
    m_uniqueOwner.clear();
    if (!m_connection.isConnected()) {
        return;
    }
    QDBusConnectionInterface *iface = m_connection.interface();
    if (iface == nullptr) {
        return;
    }
    const QDBusReply<QString> owner = iface->serviceOwner(kKwinService);
    if (owner.isValid()) {
        m_uniqueOwner = owner.value();
    }
}

void WorkspaceReceiver::onServiceOwnerChanged(const QString &service, const QString &oldOwner, const QString &newOwner)
{
    Q_UNUSED(oldOwner);
    if (service != kKwinService || !m_started || m_paused) {
        return;
    }
    ++m_ownerGeneration;
    abandonCurrentRequest();
    m_pendingRefresh = false;
    m_coalesceQueued = false;
    if (m_coalesceTimer != nullptr) {
        m_coalesceTimer->stop();
    }
    stopRecovery();
    m_uniqueOwner = newOwner;
    if (newOwner.isEmpty()) {
        stopDeadline();
        becomeUnknown(QStringLiteral("service-lost"));
        emit diagnosticsChanged();
        return;
    }
    becomeUnknown(QStringLiteral("owner-replaced"));
    m_state.refreshPending = true;
    requestSnapshot();
    emit diagnosticsChanged();
}

void WorkspaceReceiver::onInvalidatingMessage(const QDBusMessage &message)
{
    Q_UNUSED(message);
    if (!m_started || m_paused) {
        return;
    }
    invalidate(QStringLiteral("desktop-signal"), false);
}

void WorkspaceReceiver::invalidate(const QString &errorClass, bool immediateUnknown)
{
    ++m_invalidationRevision;
    ++m_invalidationCount;
    m_pendingRefresh = true;
    m_errorClass = errorClass;
    m_state.refreshPending = true;
    startDeadline();
    if (immediateUnknown) {
        becomeUnknown(errorClass);
    }
    if (!m_coalesceQueued) {
        m_coalesceQueued = true;
        m_coalesceTimer->start();
    }
    emit diagnosticsChanged();
}

void WorkspaceReceiver::onCoalescedInvalidation()
{
    m_coalesceQueued = false;
    if (!m_started || m_paused) {
        return;
    }
    requestSnapshot();
}

void WorkspaceReceiver::requestSnapshot()
{
    if (!m_started || m_paused || m_uniqueOwner.isEmpty() || !m_connection.isConnected()) {
        return;
    }
    if (m_activeRequestId != 0) {
        m_pendingRefresh = true;
        return;
    }
    m_pendingRefresh = false;
    ++m_logicalRequestCount;
    m_activeRequestId = m_logicalRequestCount;
    m_requestGeneration = m_ownerGeneration;
    m_requestRevision = m_invalidationRevision;
    stopDeadline();
    startDeadline();

    QDBusMessage call = QDBusMessage::createMethodCall(m_uniqueOwner, kManagerPath, kPropertiesInterface,
                                                       QStringLiteral("GetAll"));
    call << kManagerInterface;
    auto *watcher = new QDBusPendingCallWatcher(m_connection.asyncCall(call), this);
    watcher->setProperty("requestId", QVariant::fromValue(m_activeRequestId));
    watcher->setProperty("generation", QVariant::fromValue(m_requestGeneration));
    watcher->setProperty("revision", QVariant::fromValue(m_requestRevision));
    connect(watcher, &QDBusPendingCallWatcher::finished, this, &WorkspaceReceiver::onSnapshotReply);
}

void WorkspaceReceiver::onSnapshotReply(QDBusPendingCallWatcher *watcher)
{
    watcher->deleteLater();
    const quint64 requestId = watcher->property("requestId").toULongLong();
    const quint64 generation = watcher->property("generation").toULongLong();
    const quint64 revision = watcher->property("revision").toULongLong();
    if (requestId != m_activeRequestId) {
        ++m_rejectedReplyCount;
        emit diagnosticsChanged();
        return;
    }

    m_activeRequestId = 0;
    const bool stale = generation != m_ownerGeneration || revision != m_invalidationRevision || !m_started || m_paused;
    if (stale) {
        ++m_rejectedReplyCount;
        emit diagnosticsChanged();
        if (m_pendingRefresh && m_started && !m_paused) {
            requestSnapshot();
        }
        return;
    }

    const QDBusMessage message = watcher->reply();
    QVariantMap properties;
    if (message.type() != QDBusMessage::ReplyMessage || message.arguments().isEmpty()) {
        ++m_rejectedReplyCount;
        m_pendingRefresh = true;
        m_errorClass = QStringLiteral("snapshot-error");
        m_lastError = message.errorMessage();
        qCWarning(lcWorkspace) << "workspace snapshot failed:" << m_errorClass;
        emit diagnosticsChanged();
        scheduleRecovery();
        return;
    }
    if (!decodeGetAllProperties(message.arguments().constFirst(), properties)) {
        ++m_rejectedReplyCount;
        becomeUnknown(QStringLiteral("snapshot-type"));
        emit diagnosticsChanged();
        scheduleRecovery();
        return;
    }

    QString errorClass;
    std::optional<WorkspaceState> decoded = decodeSnapshot(properties, errorClass);
    if (!decoded) {
        ++m_rejectedReplyCount;
        becomeUnknown(errorClass);
        emit diagnosticsChanged();
        scheduleRecovery();
        return;
    }

    applyValidatedState(std::move(*decoded));
    stopDeadline();
    stopRecovery();
    if (m_pendingRefresh) {
        requestSnapshot();
    }
}

void WorkspaceReceiver::applyValidatedState(WorkspaceState &&next)
{
    ++m_snapshotCount;
    next.refreshPending = false;
    const bool duplicate = m_state.availability == WorkspaceAvailability::Available && m_state.desktops == next.desktops
        && m_state.currentId == next.currentId && m_state.currentOrdinal == next.currentOrdinal;
    m_state = std::move(next);
    m_errorClass.clear();
    m_lastError.clear();
    if (duplicate) {
        emit diagnosticsChanged();
        return;
    }
    ++m_notificationCount;
    emit stateChanged();
    emit diagnosticsChanged();
}

void WorkspaceReceiver::becomeUnknown(const QString &errorClass)
{
    const bool changed = m_state.availability != WorkspaceAvailability::Unknown || !m_state.desktops.isEmpty()
        || m_state.refreshPending;
    m_state = WorkspaceState{};
    m_errorClass = errorClass;
    m_lastError = errorClass;
    qCWarning(lcWorkspace) << "workspace unavailable:" << errorClass;
    if (changed) {
        ++m_notificationCount;
        emit stateChanged();
    }
}

std::optional<WorkspaceState> WorkspaceReceiver::decodeSnapshot(const QVariantMap &properties,
                                                                QString &errorClass) const
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
    return state;
}

void WorkspaceReceiver::startDeadline()
{
    if (!m_deadlineTimer->isActive()) {
        m_deadlineTimer->start(m_deadlineMs);
    }
}

void WorkspaceReceiver::stopDeadline()
{
    m_deadlineTimer->stop();
}

void WorkspaceReceiver::scheduleRecovery()
{
    if (m_recoveryAttempt >= m_recoveryDelaysMs.size()) {
        return;
    }
    if (m_recoveryTimer->isActive()) {
        return;
    }
    m_recoveryTimer->start(m_recoveryDelaysMs.at(m_recoveryAttempt));
    ++m_recoveryAttempt;
}

void WorkspaceReceiver::stopRecovery()
{
    m_recoveryTimer->stop();
    m_recoveryAttempt = 0;
}

void WorkspaceReceiver::abandonCurrentRequest()
{
    m_activeRequestId = 0;
}

void WorkspaceReceiver::onDeadline()
{
    if (!m_started || m_paused) {
        return;
    }
    ++m_invalidationRevision;
    abandonCurrentRequest();
    m_pendingRefresh = false;
    becomeUnknown(QStringLiteral("refresh-deadline"));
    emit diagnosticsChanged();
    scheduleRecovery();
}

void WorkspaceReceiver::onRecovery()
{
    if (!m_started || m_paused) {
        return;
    }
    resolveOwner();
    if (m_uniqueOwner.isEmpty()) {
        return;
    }
    m_pendingRefresh = true;
    requestSnapshot();
}

} // namespace contextdeck
