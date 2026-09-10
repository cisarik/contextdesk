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

void OpenRgbClient::start()
{
    m_lightingEnabled = true;
    emit lightingEnabledChanged();
    connectToServer();
}

void OpenRgbClient::stop()
{
    m_reconnectTimer->stop();
    m_connectTimer->stop();
    m_requestTimer->stop();
    m_socket->abort();
    m_buffer.clear();
    m_deviceIndex.reset();
    setState(LightingConnectionState::Disconnected);
}

void OpenRgbClient::setDesiredColors(const std::array<Rgb, openrgb::kLedCount> &colors)
{
    m_desired = colors;
    m_pendingSend = true;
    if (!m_coalesceTimer->isActive()) {
        m_coalesceTimer->start(kCoalesceMs);
    }
}

void OpenRgbClient::setDesiredColor(const Rgb &color)
{
    std::array<Rgb, openrgb::kLedCount> colors{};
    colors.fill(color);
    setDesiredColors(colors);
}

void OpenRgbClient::connectToServer()
{
    if (m_state == LightingConnectionState::Connecting || m_state == LightingConnectionState::Negotiating
        || m_state == LightingConnectionState::Ready) {
        return;
    }
    m_buffer.clear();
    m_deviceIndex.reset();
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
        sendBytes(openrgb::encodeControllerDataRequest(0, openrgb::kProtocolVersion));
        m_requestTimer->start(kRequestTimeoutMs);
        break;
    }
    case openrgb::PacketId::RequestControllerData: {
        openrgb::DecodeError error;
        const auto identity = openrgb::parseControllerIdentity(payload, openrgb::kProtocolVersion, &error);
        if (!identity) {
            qCWarning(lcRgb) << "skipped controller: bounded parse failed";
        } else if (openrgb::isLogitechG213(*identity) && !m_deviceIndex.has_value()) {
            m_deviceIndex = header.deviceIndex;
            qCInfo(lcRgb) << "selected G213 controller index (ephemeral)";
        }
        ++m_nextController;
        if (m_nextController < m_controllerCount) {
            sendBytes(openrgb::encodeControllerDataRequest(m_nextController, openrgb::kProtocolVersion));
            m_requestTimer->start(kRequestTimeoutMs);
        } else {
            m_requestTimer->stop();
            if (!m_deviceIndex) {
                disableLighting(QStringLiteral("G213 not present in OpenRGB controller list"));
                return;
            }
            sendBytes(openrgb::encodeSetCustomMode(*m_deviceIndex));
            setState(LightingConnectionState::Ready);
            m_lightingEnabled = true;
            emit lightingEnabledChanged();
            emit deviceSelectionChanged();
            applyPendingColors();
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
    openrgb::PacketHeader header;
    header.packetId = openrgb::PacketId::RequestControllerCount;
    header.payloadSize = 0;
    sendBytes(openrgb::encodeHeader(header));
    m_requestTimer->start(kRequestTimeoutMs);
}

void OpenRgbClient::applyPendingColors()
{
    if (m_state != LightingConnectionState::Ready || !m_deviceIndex || !m_lightingEnabled) {
        return;
    }
    sendBytes(openrgb::encodeUpdateLeds(*m_deviceIndex, m_desired));
    m_pendingSend = false;
}

void OpenRgbClient::onCoalesceTimeout()
{
    if (m_pendingSend) {
        applyPendingColors();
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
