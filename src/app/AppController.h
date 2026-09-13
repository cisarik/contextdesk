#pragma once

#include "actions/PowerActions.h"
#include "context/ContextReceiver.h"
#include "core/Persistence.h"
#include "core/Types.h"
#include "rgb/OpenRgbClient.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace contextdeck {

class BrokerIpcClient;
class WorkspaceReceiver;

enum class SessionLightingMode {
    Automatic,
    TemporaryColor,
    LightsOff,
    DeviceDefault,
};

class AppController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentApplication READ currentApplication NOTIFY contextChanged)
    Q_PROPERTY(QString currentProfile READ currentProfile NOTIFY contextChanged)
    Q_PROPERTY(QString remappingState READ remappingState CONSTANT)
    Q_PROPERTY(QString lightingMode READ lightingMode NOTIFY lightingModeChanged)
    Q_PROPERTY(QString lightingLabel READ lightingLabel NOTIFY lightingModeChanged)
    Q_PROPERTY(QString sessionLighting READ sessionLighting NOTIFY lightingModeChanged)
    Q_PROPERTY(bool temporaryOverrideActive READ temporaryOverrideActive NOTIFY lightingModeChanged)
    Q_PROPERTY(QString lightingConnection READ lightingConnection NOTIFY diagnosticsChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY diagnosticsChanged)
    Q_PROPERTY(bool bridgeConnected READ bridgeConnected NOTIFY contextChanged)
    Q_PROPERTY(bool degraded READ degraded NOTIFY contextChanged)
    Q_PROPERTY(QString globalColor READ globalColor NOTIFY documentChanged)
    Q_PROPERTY(QString globalMode READ globalMode NOTIFY documentChanged)
    Q_PROPERTY(QStringList globalZones READ globalZones NOTIFY documentChanged)
    Q_PROPERTY(QStringList zoneNames READ zoneNames CONSTANT)
    Q_PROPERTY(QStringList lightingPresets READ lightingPresets CONSTANT)
    Q_PROPERTY(QStringList lightingPresetLabels READ lightingPresetLabels CONSTANT)
    Q_PROPERTY(int globalSpeedPercent READ globalSpeedPercent NOTIFY documentChanged)
    Q_PROPERTY(QString globalBreathingColor READ globalBreathingColor NOTIFY documentChanged)
    Q_PROPERTY(QVariantList inventory READ inventory NOTIFY inventoryChanged)
    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY documentChanged)
    Q_PROPERTY(QVariantList controls READ controls NOTIFY documentChanged)
    Q_PROPERTY(QVariantMap diagnostics READ diagnostics NOTIFY diagnosticsChanged)
    Q_PROPERTY(QString statusSummary READ statusSummary NOTIFY presentationChanged)
    Q_PROPERTY(bool isSelfWindow READ isSelfWindow NOTIFY presentationChanged)
    Q_PROPERTY(QString lastExternalApplication READ lastExternalApplication NOTIFY presentationChanged)
    Q_PROPERTY(QString contextDisplayName READ contextDisplayName NOTIFY presentationChanged)
    Q_PROPERTY(bool hasSavedProfiles READ hasSavedProfiles NOTIFY presentationChanged)
    Q_PROPERTY(QString heroKind READ heroKind NOTIFY presentationChanged)
    Q_PROPERTY(QString heroBadge READ heroBadge NOTIFY presentationChanged)
    Q_PROPERTY(QStringList heroZones READ heroZones NOTIFY presentationChanged)
    Q_PROPERTY(bool workspaceLayoutActive READ workspaceLayoutActive NOTIFY presentationChanged)
    Q_PROPERTY(QString workspaceSummary READ workspaceSummary NOTIFY presentationChanged)
    Q_PROPERTY(bool workspaceObservationPaused READ workspaceObservationPaused NOTIFY diagnosticsChanged)
    Q_PROPERTY(QVariantList globalZoneSlots READ globalZoneSlots NOTIFY documentChanged)

public:
    AppController(ContextReceiver *context, OpenRgbClient *rgb, PowerActions *power, QObject *parent = nullptr,
                  QString configRoot = {});

    void load();
    void applyLighting();
    void setBrokerIpc(BrokerIpcClient *client);
    void setWorkspaceReceiver(WorkspaceReceiver *receiver);

    [[nodiscard]] QString currentApplication() const;
    [[nodiscard]] QString currentProfile() const;
    [[nodiscard]] QString remappingState() const { return QStringLiteral("inactive-until-M2"); }
    [[nodiscard]] QString lightingMode() const;
    [[nodiscard]] QString lightingLabel() const;
    [[nodiscard]] QString sessionLighting() const;
    [[nodiscard]] bool temporaryOverrideActive() const;
    [[nodiscard]] QString lightingConnection() const;
    [[nodiscard]] QString lastError() const;
    [[nodiscard]] bool bridgeConnected() const;
    [[nodiscard]] bool degraded() const;
    [[nodiscard]] QString globalColor() const;
    [[nodiscard]] QString globalMode() const;
    [[nodiscard]] QStringList globalZones() const;
    [[nodiscard]] QStringList zoneNames() const;
    [[nodiscard]] QStringList lightingPresets() const;
    [[nodiscard]] QStringList lightingPresetLabels() const;
    [[nodiscard]] int globalSpeedPercent() const;
    [[nodiscard]] QString globalBreathingColor() const;
    [[nodiscard]] QString statusSummary() const;
    [[nodiscard]] bool isSelfWindow() const;
    [[nodiscard]] QString lastExternalApplication() const;
    [[nodiscard]] QString contextDisplayName() const;
    [[nodiscard]] bool hasSavedProfiles() const;
    [[nodiscard]] QString heroKind() const;
    [[nodiscard]] QString heroBadge() const;
    [[nodiscard]] QStringList heroZones() const;
    [[nodiscard]] bool workspaceLayoutActive() const;
    [[nodiscard]] QString workspaceSummary() const;
    [[nodiscard]] bool workspaceObservationPaused() const;
    [[nodiscard]] QVariantList globalZoneSlots() const;
    [[nodiscard]] QVariantList inventory() const;
    [[nodiscard]] QVariantList profiles() const;
    [[nodiscard]] QVariantList controls() const;
    [[nodiscard]] QVariantMap diagnostics() const;
    [[nodiscard]] const ProfileDocument &document() const { return m_document; }
    [[nodiscard]] SessionLightingMode sessionLightingMode() const { return m_sessionLighting; }

    Q_INVOKABLE QString saveStatus() const { return m_saveStatus; }
    Q_INVOKABLE bool save();
    Q_INVOKABLE void setGlobalColor(const QString &hex);
    Q_INVOKABLE void setApplicationColor(const QString &id, const QString &hex);
    Q_INVOKABLE void addProfileFromInventory(int index);
    Q_INVOKABLE void removeProfile(const QString &id);
    Q_INVOKABLE void setAutomatic(bool enabled);
    Q_INVOKABLE void lightsOff();
    Q_INVOKABLE void restoreAutomatic();
    Q_INVOKABLE void restoreDeviceDefault();
    Q_INVOKABLE void setTemporaryColor(const QString &hex);
    Q_INVOKABLE void setGlobalLightingMode(const QString &modeName);
    Q_INVOKABLE void setGlobalZoneColor(int index, const QString &hex);
    Q_INVOKABLE void applyGlobalGradient(const QString &startHex, const QString &endHex);
    Q_INVOKABLE void setGlobalZoneRole(int index, const QString &roleName);
    Q_INVOKABLE void useDefaultWorkspaceLayout();
    Q_INVOKABLE void useStaticZoneLayout();
    Q_INVOKABLE void setWorkspaceObservationPaused(bool paused);
    Q_INVOKABLE void setGlobalSpeed(int percent);
    Q_INVOKABLE void setGlobalBreathingColor(const QString &hex);
    Q_INVOKABLE void setApplicationLightingMode(const QString &id, const QString &modeName);
    Q_INVOKABLE void setApplicationZoneColor(const QString &id, int index, const QString &hex);
    Q_INVOKABLE void applyApplicationGradient(const QString &id, const QString &startHex, const QString &endHex);
    Q_INVOKABLE void setApplicationSpeed(const QString &id, int percent);
    Q_INVOKABLE void setApplicationBreathingColor(const QString &id, const QString &hex);
    Q_INVOKABLE void displaysOff();
    Q_INVOKABLE bool canSuspend() const;
    Q_INVOKABLE bool suspend();
    Q_INVOKABLE void assignEmitShortcut(const QString &controlName, const QString &key, const QStringList &modifiers,
                                        bool applicationLevel, const QString &applicationId);
    Q_INVOKABLE QStringList previewGradient(const QString &startHex, const QString &endHex) const;
    Q_INVOKABLE bool isValidHex(const QString &hex) const;
    Q_INVOKABLE void armPassThrough();
    Q_INVOKABLE void disarmPassThrough();
    Q_INVOKABLE void releaseBrokerLease();
    [[nodiscard]] QString brokerIpcState() const;

signals:
    void contextChanged();
    void lightingModeChanged();
    void diagnosticsChanged();
    void documentChanged();
    void inventoryChanged();
    void presentationChanged();

private:
    void onIdentityChanged();
    void onContextInputsChanged();
    void onInventoryChanged();
    void rememberExternalContext();
    void refreshResolvedProfile();
    void scheduleRecompute();
    void recompute();
    [[nodiscard]] bool isOwnSurface(const ApplicationIdentity &identity) const;
    [[nodiscard]] Lighting effectiveLighting() const;
    [[nodiscard]] std::optional<Lighting> sessionOverrideLighting() const;
    [[nodiscard]] WorkspaceState currentWorkspaceState() const;
    void sendLighting(const DesiredLighting &desired);
    [[nodiscard]] QString friendlyApplicationName(const ApplicationIdentity &identity) const;
    [[nodiscard]] QString openRgbPhrase() const;
    [[nodiscard]] QString lightsPhrase() const;
    [[nodiscard]] static std::optional<Rgb> parseHex(const QString &hex);
    [[nodiscard]] static QString toHex(const Rgb &color);
    void speedBounds(LightingMode mode, quint32 &slowest, quint32 &fastest) const;
    [[nodiscard]] quint32 percentToSpeed(int percent, LightingMode mode) const;
    [[nodiscard]] int speedToPercent(const Lighting &lighting) const;
    bool applySpeedPercent(Lighting &lighting, int percent);
    bool applyBreathingColor(Lighting &lighting, const Rgb &color);

    ContextReceiver *m_context = nullptr;
    WorkspaceReceiver *m_workspace = nullptr;
    OpenRgbClient *m_rgb = nullptr;
    PowerActions *m_power = nullptr;
    BrokerIpcClient *m_brokerIpc = nullptr;
    ProfileStore m_store;
    ProfileDocument m_document;
    SessionLightingMode m_sessionLighting = SessionLightingMode::Automatic;
    Rgb m_temporaryColor{0x7c, 0x3a, 0xed};
    LightingResolution m_resolution;
    DesiredLighting m_lastDesired;
    bool m_hasLastDesired = false;
    bool m_recomputeQueued = false;
    bool m_contextEventQueued = false;
    QString m_resolvedProfileId;
    QString m_saveStatus;
    QString m_lastExternalApplication;
    quint64 m_identityUpdates = 0;
    quint64 m_lightingUpdates = 0;
};

} // namespace contextdeck
