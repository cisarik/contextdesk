#pragma once

#include "core/Types.h"
#include "rgb/OpenRgbProtocol.h"

#include <array>
#include <optional>

#include <QByteArray>
#include <QObject>
#include <QString>

class QTcpSocket;
class QTimer;

namespace contextdeck {

enum class LightingConnectionState {
    Disconnected,
    Connecting,
    Negotiating,
    Ready,
    Failed,
};

class OpenRgbClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(LightingConnectionState connectionState READ connectionState NOTIFY connectionStateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(bool lightingEnabled READ lightingEnabled NOTIFY lightingEnabledChanged)

public:
    explicit OpenRgbClient(QObject *parent = nullptr);

    void start();
    void stop();
    void setDesiredColors(const std::array<Rgb, openrgb::kLedCount> &colors);
    void setDesiredColor(const Rgb &color);

    [[nodiscard]] LightingConnectionState connectionState() const { return m_state; }
    [[nodiscard]] QString lastError() const { return m_lastError; }
    [[nodiscard]] bool lightingEnabled() const { return m_lightingEnabled; }
    [[nodiscard]] std::array<Rgb, openrgb::kLedCount> desiredColors() const { return m_desired; }
    [[nodiscard]] bool hasG213() const { return m_deviceIndex.has_value(); }

signals:
    void connectionStateChanged();
    void lastErrorChanged();
    void lightingEnabledChanged();
    void deviceSelectionChanged();

private:
    void connectToServer();
    void onConnected();
    void onReadyRead();
    void onSocketError();
    void onDisconnected();
    void onRequestTimeout();
    void onCoalesceTimeout();
    void scheduleReconnect();
    void disableLighting(const QString &reason);
    void setState(LightingConnectionState state);
    void setError(const QString &reason);
    void sendBytes(const QByteArray &bytes);
    void handlePacket(const openrgb::PacketHeader &header, const QByteArray &payload);
    void beginEnumeration();
    void applyPendingColors();

    QTcpSocket *m_socket = nullptr;
    QTimer *m_connectTimer = nullptr;
    QTimer *m_requestTimer = nullptr;
    QTimer *m_coalesceTimer = nullptr;
    QTimer *m_reconnectTimer = nullptr;
    QByteArray m_buffer;
    LightingConnectionState m_state = LightingConnectionState::Disconnected;
    QString m_lastError;
    bool m_lightingEnabled = true;
    bool m_pendingSend = false;
    quint32 m_controllerCount = 0;
    quint32 m_nextController = 0;
    quint32 m_serverProtocol = 0;
    std::optional<quint32> m_deviceIndex;
    std::array<Rgb, openrgb::kLedCount> m_desired{};
    int m_backoffMs = 1000;
};

} // namespace contextdeck
