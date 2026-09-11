#include "app/BrokerIpcClient.h"

#include "broker/IpcProtocol.h"

#include <QLoggingCategory>

using contextdeck::broker::decodeIpcFrame;
using contextdeck::broker::encodeIpcFrame;
using contextdeck::broker::FrameStatus;
using contextdeck::broker::kDefaultBrokerSocket;

namespace contextdeck {

Q_LOGGING_CATEGORY(lcBrokerIpc, "contextdeck.brokeripc")

BrokerIpcClient::BrokerIpcClient(QObject *parent)
    : QObject(parent)
{
    const QByteArray fromEnv = qgetenv("CONTEXTDECK_BROKER_SOCKET");
    m_path = fromEnv.isEmpty() ? QString::fromUtf8(kDefaultBrokerSocket) : QString::fromUtf8(fromEnv);

    m_retry.setInterval(5000);
    m_retry.setSingleShot(true);
    m_heartbeat.setInterval(2000);

    connect(&m_socket, &QLocalSocket::connected, this, &BrokerIpcClient::onConnected);
    connect(&m_socket, &QLocalSocket::readyRead, this, &BrokerIpcClient::onReadyRead);
    connect(&m_socket, &QLocalSocket::errorOccurred, this, &BrokerIpcClient::onError);
    connect(&m_retry, &QTimer::timeout, this, &BrokerIpcClient::start);
    connect(&m_heartbeat, &QTimer::timeout, this, [this]() { sendPayload(QByteArrayLiteral("HEARTBEAT")); });
}

void BrokerIpcClient::start()
{
    if (m_socket.state() != QLocalSocket::UnconnectedState) {
        return;
    }
    setState(QStringLiteral("connecting"));
    m_socket.connectToServer(m_path);
}

void BrokerIpcClient::requestStatus()
{
    sendPayload(QByteArrayLiteral("STATUS"));
}

void BrokerIpcClient::acquireLease()
{
    m_wantLease = true;
    sendPayload(QByteArrayLiteral("LEASE"));
}

void BrokerIpcClient::arm()
{
    m_wantArm = true;
    if (!m_wantLease) {
        acquireLease();
    }
    sendPayload(QByteArrayLiteral("ARM"));
    startHeartbeat();
}

void BrokerIpcClient::disarm()
{
    m_wantArm = false;
    sendPayload(QByteArrayLiteral("DISARM"));
}

void BrokerIpcClient::releaseLease()
{
    m_wantArm = false;
    m_wantLease = false;
    stopHeartbeat();
    sendPayload(QByteArrayLiteral("RELEASE"));
}

void BrokerIpcClient::sendPayload(const QByteArray &payload)
{
    if (m_socket.state() != QLocalSocket::ConnectedState) {
        return;
    }
    std::vector<uint8_t> frame;
    if (!encodeIpcFrame(std::string_view(payload.constData(), static_cast<std::size_t>(payload.size())), frame)) {
        return;
    }
    m_socket.write(reinterpret_cast<const char *>(frame.data()), static_cast<qint64>(frame.size()));
}

void BrokerIpcClient::onConnected()
{
    m_buffer.clear();
    setState(QStringLiteral("connected"));
    qCInfo(lcBrokerIpc) << "connected; probing STATUS (no auto-arm)";
    requestStatus();
}

void BrokerIpcClient::onReadyRead()
{
    const QByteArray chunk = m_socket.readAll();
    m_buffer.insert(m_buffer.end(), chunk.cbegin(), chunk.cend());
    for (;;) {
        std::string payload;
        const FrameStatus status = decodeIpcFrame(m_buffer, payload);
        if (status == FrameStatus::NeedMore) {
            return;
        }
        if (status == FrameStatus::Malformed) {
            qCWarning(lcBrokerIpc) << "malformed broker reply";
            m_socket.disconnectFromServer();
            return;
        }
        handleReply(QByteArray::fromStdString(payload));
    }
}

void BrokerIpcClient::onError(QLocalSocket::LocalSocketError error)
{
    Q_UNUSED(error);
    m_wantArm = false;
    m_wantLease = false;
    stopHeartbeat();
    setState(QStringLiteral("disconnected"));
    if (!m_retry.isActive()) {
        m_retry.start();
    }
}

void BrokerIpcClient::handleReply(const QByteArray &payload)
{
    qCInfo(lcBrokerIpc) << "reply" << payload;
    if (payload.startsWith("OK ARMED")) {
        setState(QStringLiteral("armed"));
        startHeartbeat();
        return;
    }
    if (payload.startsWith("OK DISARMED") || payload.startsWith("OK RELEASED")) {
        setState(QStringLiteral("connected"));
        if (payload.startsWith("OK RELEASED")) {
            stopHeartbeat();
            m_wantLease = false;
            m_wantArm = false;
        }
        return;
    }
    if (payload.startsWith("OK LEASE")) {
        m_wantLease = true;
        setState(m_wantArm ? QStringLiteral("armed") : QStringLiteral("leased"));
        return;
    }
    if (payload.startsWith("OK STATUS")) {
        setState(QString::fromUtf8(payload));
        return;
    }
    if (payload.startsWith("ERR ARM_FAILED")) {
        m_wantArm = false;
        setState(QStringLiteral("arm-failed"));
    }
}

void BrokerIpcClient::setState(const QString &state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    emit stateChanged();
}

void BrokerIpcClient::startHeartbeat()
{
    if (!m_heartbeat.isActive()) {
        m_heartbeat.start();
    }
}

void BrokerIpcClient::stopHeartbeat()
{
    m_heartbeat.stop();
}

} // namespace contextdeck
