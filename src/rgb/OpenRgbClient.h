#pragma once

#include "core/Types.h"
#include "rgb/OpenRgbProtocol.h"

#include <optional>

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVector>

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
    void setDesiredState(const DesiredLighting &state);

    [[nodiscard]] LightingConnectionState connectionState() const { return m_state; }
    [[nodiscard]] QString lastError() const { return m_lastError; }
    [[nodiscard]] bool lightingEnabled() const { return m_lightingEnabled; }
    [[nodiscard]] DesiredLighting desiredState() const { return m_desired; }
    [[nodiscard]] LightingMode recordedRestoreMode() const { return m_recordedRestoreMode; }
    [[nodiscard]] bool hasG213() const { return m_deviceIndex.has_value(); }
    [[nodiscard]] bool hasTakenOver() const { return m_tookOver; }
    [[nodiscard]] QString socketStateText() const;
    [[nodiscard]] QString sdkEndpoint() const;

signals:
    void connectionStateChanged();
    void lastErrorChanged();
    void lightingEnabledChanged();
    void deviceSelectionChanged();
    void restoreModeChanged();

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
    void applyDesiredState();
    void recordRestoreMode(const openrgb::ControllerSnapshot &snapshot);
    void sendRestoreThenRelease();

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
    bool m_tookOver = false;
    bool m_restoreThenUntouched = false;
    quint32 m_controllerCount = 0;
    quint32 m_nextController = 0;
    quint32 m_serverProtocol = 0;
    std::optional<quint32> m_deviceIndex;
    DesiredLighting m_desired{};
    std::optional<DesiredLighting> m_lastSent;
    LightingMode m_recordedRestoreMode = LightingMode::Wave;
    QVector<openrgb::ControllerMode> m_modes;
    int m_backoffMs = 1000;
};

} // namespace contextdeck
