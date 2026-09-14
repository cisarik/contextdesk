#pragma once

#include "core/Types.h"

#include <QObject>
#include <QString>
#include <QVector>

#include <functional>
#include <optional>

namespace contextdeck {

struct PlacementHintDecision {
    QString desktopId;
    bool maximize = false;
};

struct InventoryEntry {
    QString desktopFileName;
    QString resourceClass;
    QString resourceName;

    [[nodiscard]] bool operator==(const InventoryEntry &other) const = default;

    [[nodiscard]] QString identityKey() const
    {
        return desktopFileName + QLatin1Char('\x1f') + resourceClass + QLatin1Char('\x1f') + resourceName;
    }
};

class ContextReceiver : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentIdentity READ currentIdentity NOTIFY currentIdentityChanged)
    Q_PROPERTY(bool bridgeConnected READ bridgeConnected NOTIFY bridgeConnectedChanged)
    Q_PROPERTY(quint32 policyRevision READ policyRevision NOTIFY policyRevisionChanged)
    Q_PROPERTY(bool degraded READ isDegraded NOTIFY degradedChanged)

public:
    using PlacementHintProvider =
        std::function<PlacementHintDecision(const ApplicationIdentity &identity, const std::optional<QString> &caption)>;

    explicit ContextReceiver(QObject *parent = nullptr);
    ~ContextReceiver() override;

    [[nodiscard]] bool start();
    [[nodiscard]] bool isDegraded() const { return m_degraded; }
    [[nodiscard]] bool bridgeConnected() const { return m_bridgeConnected; }
    [[nodiscard]] quint32 policyRevision() const { return m_policyRevision; }
    [[nodiscard]] QString currentIdentity() const;
    [[nodiscard]] QString bridgeId() const { return m_bridgeId; }
    [[nodiscard]] ApplicationIdentity identity() const { return m_identity; }
    [[nodiscard]] QVector<InventoryEntry> inventory() const { return m_inventory; }
    [[nodiscard]] QString lastError() const { return m_lastError; }
    [[nodiscard]] bool titleFallbackEnabled() const { return m_titleFallbackEnabled; }

    void setTitleFallbackEnabled(bool enabled) { m_titleFallbackEnabled = enabled; }
    void setPlacementHintProvider(PlacementHintProvider provider) { m_placementProvider = std::move(provider); }
    [[nodiscard]] std::optional<QString> takeTitleHint();

signals:
    void currentIdentityChanged();
    void bridgeConnectedChanged();
    void policyRevisionChanged();
    void degradedChanged();
    void inventoryChanged();
    void bridgeLost();

private:
    class Object;
    friend class Object;

    void onContextReport(const QString &bridgeId, quint32 sequence, const ApplicationIdentity &identity,
                         qint64 parentWindowId);
    void onInventoryReport(const QString &bridgeId, quint32 sequence, const QString &payloadJson);
    void onHeartbeat(const QString &bridgeId, quint32 sequence);
    void onTitleHint(const QString &bridgeId, quint32 sequence, const QString &caption);
    [[nodiscard]] PlacementHintDecision onPlacementHint(const ApplicationIdentity &identity);
    void onWatchdog();
    void markBridgeLost(const QString &reason);
    void bumpPolicy();
    [[nodiscard]] bool acceptSequence(const QString &bridgeId, quint32 sequence);

    Object *m_object = nullptr;
    bool m_degraded = false;
    bool m_bridgeConnected = false;
    bool m_registered = false;
    bool m_objectRegistered = false;
    bool m_warnedLoss = false;
    bool m_titleFallbackEnabled = false;
    quint32 m_policyRevision = 0;
    quint32 m_lastSequence = 0;
    QString m_bridgeId;
    QString m_lastError;
    ApplicationIdentity m_identity;
    QVector<InventoryEntry> m_inventory;
    qint64 m_lastHeartbeatMs = 0;
    std::optional<QString> m_pendingCaption;
    PlacementHintProvider m_placementProvider;
};

} // namespace contextdeck
