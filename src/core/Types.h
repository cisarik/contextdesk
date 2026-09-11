#pragma once

#include <array>
#include <optional>

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace contextdeck {

inline constexpr int kSchemaVersion = 2;
inline constexpr quint16 kG213VendorId = 0x046d;
inline constexpr quint16 kG213ProductId = 0xc336;
inline constexpr const char *kVendorIdText = "046d";
inline constexpr const char *kProductIdText = "c336";
inline constexpr const char *kDeviceModel = "logitech-g213-prodigy";
inline constexpr int kZoneCount = 5;
inline constexpr qsizetype kMaxDocumentBytes = 1024 * 1024;
inline constexpr const char *kZoneNames[kZoneCount] = {
    "Left Area",
    "Middle Area",
    "Right Area",
    "Arrow and Homekeys",
    "Numpad",
};

enum class ControlId {
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
    Previous,
    PlayPause,
    Next,
    Mute,
    VolumeDown,
    VolumeUp,
    GameMode,
    Backlight,
};

inline size_t qHash(ControlId id, size_t seed = 0) noexcept
{
    return ::qHash(static_cast<int>(id), seed);
}

enum class ActionType {
    InheritGlobal,
    PassThrough,
    Disabled,
    EmitShortcut,
    ApprovedSystemAction,
};

enum class SystemActionId {
    Suspend,
    DisplaysOff,
};

enum class LightingMode {
    Untouched,
    Direct,
    Wave,
    Cycle,
    Breathing,
    Off,
};

enum class Modifier {
    Ctrl,
    Shift,
    Alt,
    Super,
};

inline size_t qHash(Modifier modifier, size_t seed = 0) noexcept
{
    return ::qHash(static_cast<int>(modifier), seed);
}

struct Rgb {
    quint8 r = 0;
    quint8 g = 0;
    quint8 b = 0;

    [[nodiscard]] bool operator==(const Rgb &other) const = default;
};

struct Chord {
    QString key;
    QVector<Modifier> modifiers;

    [[nodiscard]] bool operator==(const Chord &other) const = default;
};

struct Assignment {
    ActionType action = ActionType::PassThrough;
    std::optional<Chord> chord;
    std::optional<SystemActionId> systemAction;

    [[nodiscard]] bool operator==(const Assignment &other) const = default;
};

struct ZoneValue {
    Rgb color{};

    [[nodiscard]] bool operator==(const ZoneValue &other) const = default;
};

struct Lighting {
    LightingMode mode = LightingMode::Untouched;
    std::optional<Rgb> baseColor;
    std::optional<std::array<ZoneValue, kZoneCount>> zones;
    LightingMode restoreMode = LightingMode::Wave;

    [[nodiscard]] bool operator==(const Lighting &other) const = default;
};

struct DesiredLighting {
    LightingMode mode = LightingMode::Untouched;
    std::array<Rgb, kZoneCount> colors{};

    [[nodiscard]] bool operator==(const DesiredLighting &other) const = default;
};

[[nodiscard]] inline bool isDeviceLightingMode(LightingMode mode)
{
    return mode != LightingMode::Untouched;
}

[[nodiscard]] inline Lighting untouchedLighting()
{
    return Lighting{};
}

[[nodiscard]] inline std::array<Rgb, kZoneCount> zoneColorsFromPreset(const Lighting &lighting)
{
    std::array<Rgb, kZoneCount> colors{};
    if (lighting.zones) {
        for (int i = 0; i < kZoneCount; ++i) {
            colors[static_cast<size_t>(i)] = (*lighting.zones)[static_cast<size_t>(i)].color;
        }
        return colors;
    }
    if (lighting.baseColor) {
        colors.fill(*lighting.baseColor);
    }
    return colors;
}

[[nodiscard]] inline DesiredLighting toDesiredLighting(const Lighting &preset)
{
    DesiredLighting desired;
    desired.mode = preset.mode;
    if (preset.mode == LightingMode::Direct) {
        desired.colors = zoneColorsFromPreset(preset);
    }
    return desired;
}

struct MatchSpec {
    std::optional<QString> desktopFileName;
    std::optional<QString> resourceClass;
    std::optional<QString> resourceName;

    [[nodiscard]] bool isEmpty() const
    {
        return !desktopFileName && !resourceClass && !resourceName;
    }
};

struct ApplicationIdentity {
    QString desktopFileName;
    QString resourceClass;
    QString resourceName;

    [[nodiscard]] bool isIdentified() const
    {
        return !desktopFileName.isEmpty() || !resourceClass.isEmpty() || !resourceName.isEmpty();
    }
};

struct ApplicationProfile {
    QString id;
    QString displayName;
    MatchSpec match;
    QHash<ControlId, Assignment> keys;
    std::optional<Lighting> lighting;
};

struct DeviceScope {
    QString vendorId = QString::fromLatin1(kVendorIdText);
    QString productId = QString::fromLatin1(kProductIdText);
    QString model = QString::fromLatin1(kDeviceModel);
};

struct Preferences {
    bool automaticEnabled = true;
    bool trayNotifications = false;
};

struct ProfileDocument {
    int schemaVersion = kSchemaVersion;
    DeviceScope device;
    QHash<ControlId, Assignment> globalKeys;
    Lighting globalLighting;
    QVector<ApplicationProfile> applications;
    Preferences preferences;
};

struct PersistenceError {
    QString reason;
    QString jsonPath;
    bool preserved = true;
};

struct LoadOutcome {
    bool ok = false;
    bool missing = false;
    ProfileDocument document;
    PersistenceError error;
};

struct SaveOutcome {
    bool ok = false;
    PersistenceError error;
};

[[nodiscard]] inline Assignment passThroughAssignment()
{
    Assignment assignment;
    assignment.action = ActionType::PassThrough;
    return assignment;
}

} // namespace contextdeck
