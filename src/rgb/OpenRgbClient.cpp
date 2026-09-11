#include "rgb/OpenRgbClient.h"

#include <QAbstractSocket>
#include <QHostAddress>
#include <QLoggingCategory>
#include <QTcpSocket>
#include <QTimer>

namespace contextdeck {

Q_LOGGING_CATEGORY(lcRgb, "contextdeck.rgb")

namespace {
constexpr int kConnectTimeoutMs = 2000;
constexpr int kRequestTimeoutMs = 5000;
constexpr int kCoalesceMs = 50;
constexpr int kMaxBackoffMs = 30000;
const QHostAddress kLoopback{QHostAddress::LocalHost};
} // namespace

OpenRgbClient::OpenRgbClient(QObject *parent)
    : QObject(parent)
    , m_socket(new QTcpSocket(this))
    , m_connectTimer(new QTimer(this))
    , m_requestTimer(new QTimer(this))
    , m_coalesceTimer(new QTimer(this))
    , m_reconnectTimer(new QTimer(this))
{
    m_connectTimer->setSingleShot(true);
    m_requestTimer->setSingleShot(true);
    m_coalesceTimer->setSingleShot(true);
    m_reconnectTimer->setSingleShot(true);

    connect(m_socket, &QTcpSocket::connected, this, &OpenRgbClient::onConnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &OpenRgbClient::onReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &OpenRgbClient::onSocketError);
    connect(m_socket, &QTcpSocket::disconnected, this, &OpenRgbClient::onDisconnected);
    connect(m_connectTimer, &QTimer::timeout, this, [this]() {
        if (m_state == LightingConnectionState::Connecting) {
            m_socket->abort();
            disableLighting(QStringLiteral("connect timeout"));
            scheduleReconnect();
        }
    });
    connect(m_requestTimer, &QTimer::timeout, this, &OpenRgbClient::onRequestTimeout);
    connect(m_coalesceTimer, &QTimer::timeout, this, &OpenRgbClient::onCoalesceTimeout);
    connect(m_reconnectTimer, &QTimer::timeout, this, &OpenRgbClient::connectToServer);
}

QString OpenRgbClient::socketStateText() const
{
    switch (m_socket->state()) {
    case QAbstractSocket::UnconnectedState:
        return QStringLiteral("unconnected");
    case QAbstractSocket::HostLookupState:
        return QStringLiteral("host-lookup");
    case QAbstractSocket::ConnectingState:
        return QStringLiteral("connecting");
    case QAbstractSocket::ConnectedState:
        return QStringLiteral("connected");
    case QAbstractSocket::BoundState:
        return QStringLiteral("bound");
    case QAbstractSocket::ClosingState:
        return QStringLiteral("closing");
    case QAbstractSocket::ListeningState:
        return QStringLiteral("listening");
    }
    return QStringLiteral("unknown");
}

QString OpenRgbClient::sdkEndpoint() const
{
    return QStringLiteral("127.0.0.1:%1").arg(openrgb::kDefaultPort);
}

bool OpenRgbClient::speedRangeFor(LightingMode mode, quint32 &speedMin, quint32 &speedMax) const
{
    const auto index = openrgb::findModeIndex(m_modes, mode);
    if (!index) {
        return false;
    }
    speedMin = m_modes.at(*index).speedMin;
    speedMax = m_modes.at(*index).speedMax;
    return true;
}

void OpenRgbClient::start()
{
    m_lightingEnabled = true;
    emit lightingEnabledChanged();
    connectToServer();
}

void OpenRgbClient::stop()
{
    if (m_tookOver && m_state == LightingConnectionState::Ready && m_deviceIndex) {
        sendRestoreThenRelease();
    }
    m_reconnectTimer->stop();
    m_connectTimer->stop();
    m_requestTimer->stop();
    m_socket->abort();
    m_buffer.clear();
    m_deviceIndex.reset();
    m_modes.clear();
    m_lastSent.reset();
    m_tookOver = false;
    setState(LightingConnectionState::Disconnected);
}

void OpenRgbClient::setDesiredState(const DesiredLighting &state)
{
    if (state.mode == LightingMode::Untouched && m_tookOver) {
        m_restoreThenUntouched = true;
        DesiredLighting restore;
        restore.mode = m_recordedRestoreMode;
        m_desired = restore;
    } else {
        m_restoreThenUntouched = false;
        m_desired = state;
    }
    m_pendingSend = true;
    if (!m_coalesceTimer->isActive()) {
        m_coalesceTimer->start(kCoalesceMs);
    }
}

void OpenRgbClient::connectToServer()
{
    if (m_state == LightingConnectionState::Connecting || m_state == LightingConnectionState::Negotiating
        || m_state == LightingConnectionState::Ready) {
        return;
    }
    m_buffer.clear();
    m_deviceIndex.reset();
    m_modes.clear();
    m_lastSent.reset();
    m_controllerCount = 0;
    m_nextController = 0;
    m_serverProtocol = 0;
    setState(LightingConnectionState::Connecting);
    m_connectTimer->start(kConnectTimeoutMs);
    m_socket->connectToHost(kLoopback, openrgb::kDefaultPort);
}

void OpenRgbClient::onConnected()
{
    m_connectTimer->stop();
    m_backoffMs = 1000;
    setState(LightingConnectionState::Negotiating);
    sendBytes(openrgb::encodeProtocolVersionRequest());
    m_requestTimer->start(kRequestTimeoutMs);
}

void OpenRgbClient::sendBytes(const QByteArray &bytes)
{
    if (m_socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }
    m_socket->write(bytes);
}

void OpenRgbClient::onReadyRead()
{
    m_buffer.append(m_socket->readAll());
    if (m_buffer.size() > static_cast<qsizetype>(openrgb::kHeaderSize + openrgb::kMaxPayloadBytes)) {
        disableLighting(QStringLiteral("receive buffer exceeded cap"));
        m_socket->abort();
        scheduleReconnect();
        return;
    }

    while (m_buffer.size() >= openrgb::kHeaderSize) {
        openrgb::DecodeError error;
        const auto header = openrgb::decodeHeader(QByteArrayView(m_buffer.constData(), openrgb::kHeaderSize), &error);
        if (!header) {
            disableLighting(error.reason);
            m_socket->abort();
            scheduleReconnect();
            return;
        }
        const int frameSize = openrgb::kHeaderSize + static_cast<int>(header->payloadSize);
        if (m_buffer.size() < frameSize) {
            return;
        }
        const QByteArray payload = m_buffer.mid(openrgb::kHeaderSize, static_cast<int>(header->payloadSize));
        m_buffer.remove(0, frameSize);
        handlePacket(*header, payload);
    }
}

void OpenRgbClient::handlePacket(const openrgb::PacketHeader &header, const QByteArray &payload)
{
    switch (header.packetId) {
    case openrgb::PacketId::RequestProtocolVersion: {
        openrgb::DecodeError error;
        const auto version = openrgb::decodeProtocolVersionPayload(payload, &error);
        if (!version) {
            disableLighting(error.reason);
            m_socket->abort();
            return;
        }
        if (!openrgb::protocolVersionAcceptable(*version)) {
            disableLighting(QStringLiteral("rejected OpenRGB protocol newer than 5"));
            m_socket->abort();
            return;
        }
        m_serverProtocol = *version;
        sendBytes(openrgb::encodeClientName(QStringLiteral("ContextDeck")));
        beginEnumeration();
        break;
    }
    case openrgb::PacketId::RequestControllerCount: {
        openrgb::DecodeError error;
        const auto count = openrgb::decodeUint32Payload(payload, &error);
        if (!count) {
            disableLighting(error.reason);
            return;
        }
        m_controllerCount = *count;
        m_nextController = 0;
        m_deviceIndex.reset();
        if (m_controllerCount == 0) {
            disableLighting(QStringLiteral("no OpenRGB controllers"));
            return;
        }
        sendBytes(openrgb::encodeControllerDataRequest(0, m_serverProtocol != 0 ? m_serverProtocol
                                                                                : openrgb::kProtocolVersion));
        m_requestTimer->start(kRequestTimeoutMs);
        break;
    }
    case openrgb::PacketId::RequestControllerData: {
        openrgb::DecodeError error;
        const auto snapshot = openrgb::parseControllerSnapshot(payload, m_serverProtocol != 0 ? m_serverProtocol
                                                                                              : openrgb::kProtocolVersion,
                                                               &error);
        if (!snapshot) {
            qCWarning(lcRgb) << "skipped controller: bounded parse failed";
        } else if (openrgb::isLogitechG213(snapshot->identity) && !m_deviceIndex.has_value()) {
            m_deviceIndex = header.deviceIndex;
            m_modes = snapshot->modes;
            recordRestoreMode(*snapshot);
            qCInfo(lcRgb) << "selected G213 controller index (ephemeral)";
        }
        ++m_nextController;
        if (m_nextController < m_controllerCount) {
            sendBytes(openrgb::encodeControllerDataRequest(m_nextController,
                                                           m_serverProtocol != 0 ? m_serverProtocol
                                                                                : openrgb::kProtocolVersion));
            m_requestTimer->start(kRequestTimeoutMs);
        } else {
            m_requestTimer->stop();
            if (!m_deviceIndex) {
                disableLighting(QStringLiteral("G213 not present in OpenRGB controller list"));
                return;
            }
            setState(LightingConnectionState::Ready);
            m_lightingEnabled = true;
            emit lightingEnabledChanged();
            emit deviceSelectionChanged();
            qCInfo(lcRgb) << "G213 enumerated; no mode selected without lighting intent";
            applyDesiredState();
        }
        break;
    }
    case openrgb::PacketId::DeviceListUpdated:
        qCInfo(lcRgb) << "device list updated; re-enumerating";
        m_deviceIndex.reset();
        beginEnumeration();
        break;
    default:
        break;
    }
}

void OpenRgbClient::beginEnumeration()
{
    m_controllerCount = 0;
    m_nextController = 0;
    m_deviceIndex.reset();
    m_modes.clear();
    m_lastSent.reset();
    openrgb::PacketHeader header;
    header.packetId = openrgb::PacketId::RequestControllerCount;
    header.payloadSize = 0;
    sendBytes(openrgb::encodeHeader(header));
    m_requestTimer->start(kRequestTimeoutMs);
}

void OpenRgbClient::recordRestoreMode(const openrgb::ControllerSnapshot &snapshot)
{
    LightingMode recorded = LightingMode::Wave;
    if (snapshot.activeMode >= 0 && snapshot.activeMode < snapshot.modes.size()) {
        const auto named = openrgb::lightingModeFromOpenRgbName(snapshot.modes.at(snapshot.activeMode).name);
        if (named && isDeviceLightingMode(*named) && *named != LightingMode::Direct) {
            recorded = *named;
        }
    }
    if (m_recordedRestoreMode != recorded) {
        m_recordedRestoreMode = recorded;
        emit restoreModeChanged();
    }
}

void OpenRgbClient::sendRestoreThenRelease()
{
    DesiredLighting restore;
    restore.mode = m_recordedRestoreMode;
    openrgb::DecodeError error;
    const auto frames = openrgb::encodeDesiredStateFrames(*m_deviceIndex, restore, m_modes,
                                                          m_serverProtocol != 0 ? m_serverProtocol
                                                                                : openrgb::kProtocolVersion,
                                                          &error);
    if (frames) {
        for (const QByteArray &frame : *frames) {
            sendBytes(frame);
        }
    }
    m_tookOver = false;
    m_lastSent.reset();
}

void OpenRgbClient::applyDesiredState()
{
    if (m_state != LightingConnectionState::Ready || !m_deviceIndex || !m_lightingEnabled) {
        return;
    }
    if (m_desired.mode == LightingMode::Untouched) {
        m_pendingSend = false;
        return;
    }
    if (m_lastSent && *m_lastSent == m_desired && !m_restoreThenUntouched) {
        m_pendingSend = false;
        return;
    }
    openrgb::DecodeError error;
    const auto frames = openrgb::encodeDesiredStateFrames(*m_deviceIndex, m_desired, m_modes,
                                                          m_serverProtocol != 0 ? m_serverProtocol
                                                                                : openrgb::kProtocolVersion,
                                                          &error);
    if (!frames) {
        qCWarning(lcRgb) << "lighting encode refused:" << error.reason;
        m_pendingSend = false;
        return;
    }
    if (frames->isEmpty()) {
        m_pendingSend = false;
        return;
    }
    const bool allZeroDirect = m_desired.mode == LightingMode::Direct
        && m_desired.colors[0] == Rgb{} && m_desired.colors[1] == Rgb{} && m_desired.colors[2] == Rgb{}
        && m_desired.colors[3] == Rgb{} && m_desired.colors[4] == Rgb{};
    if (allZeroDirect) {
        qCWarning(lcRgb) << "refusing Direct with all-zero colors";
        m_pendingSend = false;
        return;
    }
    for (const QByteArray &frame : *frames) {
        sendBytes(frame);
    }
    m_lastSent = m_desired;
    m_tookOver = true;
    m_pendingSend = false;
    if (m_restoreThenUntouched) {
        m_desired = DesiredLighting{};
        m_tookOver = false;
        m_restoreThenUntouched = false;
        m_lastSent.reset();
    }
}

void OpenRgbClient::onCoalesceTimeout()
{
    if (m_pendingSend) {
        applyDesiredState();
    }
}

void OpenRgbClient::onRequestTimeout()
{
    disableLighting(QStringLiteral("OpenRGB request timeout"));
    m_socket->abort();
    scheduleReconnect();
}

void OpenRgbClient::onSocketError()
{
    disableLighting(m_socket->errorString());
    scheduleReconnect();
}

void OpenRgbClient::onDisconnected()
{
    if (m_state != LightingConnectionState::Disconnected) {
        disableLighting(QStringLiteral("OpenRGB disconnected"));
        scheduleReconnect();
    }
}

void OpenRgbClient::scheduleReconnect()
{
    if (m_reconnectTimer->isActive()) {
        return;
    }
    m_reconnectTimer->start(m_backoffMs);
    m_backoffMs = qMin(m_backoffMs * 2, kMaxBackoffMs);
}

void OpenRgbClient::disableLighting(const QString &reason)
{
    setError(reason);
    if (m_lightingEnabled) {
        m_lightingEnabled = false;
        emit lightingEnabledChanged();
    }
    if (m_state != LightingConnectionState::Failed && m_state != LightingConnectionState::Disconnected) {
        setState(LightingConnectionState::Failed);
    }
    qCWarning(lcRgb) << "lighting disabled:" << reason;
}

void OpenRgbClient::setState(LightingConnectionState state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    emit connectionStateChanged();
}

void OpenRgbClient::setError(const QString &reason)
{
    if (m_lastError == reason) {
        return;
    }
    m_lastError = reason;
    emit lastErrorChanged();
}

} // namespace contextdeck
