#pragma once

#include <QByteArray>
#include <QLocalSocket>
#include <QObject>
#include <QString>
#include <QTimer>
#include <vector>

namespace contextdeck {

// Session-side client for the broker Unix socket. Does not arm on start.
class BrokerIpcClient : public QObject
{
    Q_OBJECT

public:
    explicit BrokerIpcClient(QObject *parent = nullptr);

    void start();
    void requestStatus();
    void acquireLease();
    void arm();
    void disarm();
    void releaseLease();

    [[nodiscard]] QString stateText() const { return m_state; }
    [[nodiscard]] bool connected() const { return m_socket.state() == QLocalSocket::ConnectedState; }

signals:
    void stateChanged();

private:
    void sendPayload(const QByteArray &payload);
    void onConnected();
    void onReadyRead();
    void onError(QLocalSocket::LocalSocketError error);
    void handleReply(const QByteArray &payload);
    void setState(const QString &state);
    void startHeartbeat();
    void stopHeartbeat();

    QLocalSocket m_socket;
    QTimer m_retry;
    QTimer m_heartbeat;
    std::vector<uint8_t> m_buffer;
    QString m_state = QStringLiteral("disconnected");
    QString m_path;
    bool m_wantLease = false;
    bool m_wantArm = false;
};

} // namespace contextdeck
