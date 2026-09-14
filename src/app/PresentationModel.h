#pragma once

#include "core/Types.h"

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace contextdeck {

class BrokerIpcClient;
class ContextReceiver;
class OpenRgbClient;
class PowerActions;
class WorkspaceApplyController;
class WorkspaceReceiver;
enum class SessionLightingMode;

// Read-only presentation queries extracted from AppController: hero/status,
// diagnostics, inventory/profile/control maps, and workspace observation and
// plan previews. Reads live state through references held in State.
class PresentationModel
{
public:
    struct State {
        ContextReceiver *&context;
        WorkspaceReceiver *&workspace;
        OpenRgbClient *&rgb;
        PowerActions *&power;
        BrokerIpcClient *&brokerIpc;
        const ProfileDocument &document;
        const LightingResolution &resolution;
        const SessionLightingMode &sessionLighting;
        const QString &lastExternalApplication;
        const quint64 &identityUpdates;
        const quint64 &lightingUpdates;
        const WorkspaceApplyController &apply;
    };

    explicit PresentationModel(State state);

    [[nodiscard]] QString lightingMode() const;
    [[nodiscard]] QString lightingLabel() const;
    [[nodiscard]] QString sessionLighting() const;
    [[nodiscard]] bool temporaryOverrideActive() const;
    [[nodiscard]] QString lightingConnection() const;
    [[nodiscard]] QString lastError() const;
    [[nodiscard]] bool bridgeConnected() const;
    [[nodiscard]] bool degraded() const;
    [[nodiscard]] QString brokerIpcState() const;
    [[nodiscard]] QString globalColor() const;
    [[nodiscard]] QString globalMode() const;
    [[nodiscard]] QStringList globalZones() const;
    [[nodiscard]] QStringList zoneNames() const;
    [[nodiscard]] QStringList lightingPresets() const;
    [[nodiscard]] QStringList lightingPresetLabels() const;
    [[nodiscard]] int globalSpeedPercent() const;
    [[nodiscard]] QString globalBreathingColor() const;
    [[nodiscard]] QVariantList globalZoneSlots() const;
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
    [[nodiscard]] QVariantList inventory() const;
    [[nodiscard]] QVariantList profiles() const;
    [[nodiscard]] QVariantList controls() const;
    [[nodiscard]] QVariantMap diagnostics() const;
    [[nodiscard]] QVariantMap workspaceObserved() const;
    [[nodiscard]] QVariantMap workspacePlanPreview() const;

    [[nodiscard]] static bool isOwnSurface(const ApplicationIdentity &identity);
    [[nodiscard]] static QString friendlyApplicationName(const ApplicationIdentity &identity);

private:
    [[nodiscard]] QVariantMap workspacePlanMap() const;
    [[nodiscard]] Lighting effectiveLighting() const;
    [[nodiscard]] QString openRgbPhrase() const;
    [[nodiscard]] QString lightsPhrase() const;

    State m_state;
};

} // namespace contextdeck
