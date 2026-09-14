#pragma once

#include <array>
#include <optional>

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace contextdeck {

inline constexpr int kSchemaVersion = 4;
inline constexpr quint16 kG213VendorId = 0x046d;
inline constexpr quint16 kG213ProductId = 0xc336;
inline constexpr const char *kVendorIdText = "046d";
inline constexpr const char *kProductIdText = "c336";
inline constexpr const char *kDeviceModel = "logitech-g213-prodigy";
inline constexpr int kZoneCount = 5;
inline constexpr qsizetype kMaxDocumentBytes = 1024 * 1024;
inline constexpr qsizetype kMaxIdentifierBytes = 128;
inline constexpr qsizetype kMaxDisplayNameBytes = 256;
inline constexpr qsizetype kMaxTitlePatternBytes = 128;
inline constexpr int kMaxWorkspaceDesktops = 32;
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

enum class ZoneRole {
    Static,
    DesktopIndicator,
    AppColor,
    Off,
};

enum class WorkspaceAvailability {
    Unknown,
    Available,
};

enum class TitleMatchMode {
    Exact,
    Contains,
    Prefix,
};

[[nodiscard]] inline std::optional<TitleMatchMode> titleMatchModeFromJsonName(QStringView name)
{
    if (name == QLatin1String("exact")) {
        return TitleMatchMode::Exact;
    }
    if (name == QLatin1String("contains")) {
        return TitleMatchMode::Contains;
    }
    if (name == QLatin1String("prefix")) {
        return TitleMatchMode::Prefix;
    }
    return std::nullopt;
}

[[nodiscard]] inline QString titleMatchModeJsonName(TitleMatchMode mode)
{
    switch (mode) {
    case TitleMatchMode::Exact:
        return QStringLiteral("exact");
    case TitleMatchMode::Contains:
        return QStringLiteral("contains");
    case TitleMatchMode::Prefix:
        return QStringLiteral("prefix");
    }
    return QStringLiteral("contains");
}

enum class SlotContribution {
    None,
    SessionOverride,
    Static,
    DesktopIndicatorCurrent,
    DesktopIndicatorInactive,
    DesktopIndicatorAbsent,
    AppColor,
    Off,
    DeviceDefault,
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
    ZoneRole role = ZoneRole::Static;
    Rgb color{};

    [[nodiscard]] bool operator==(const ZoneValue &other) const = default;
};

inline constexpr Rgb kDefaultEffectColor{0x7c, 0x3a, 0xed};

struct Lighting {
    LightingMode mode = LightingMode::Untouched;
    std::optional<Rgb> baseColor;
    std::optional<std::array<ZoneValue, kZoneCount>> zones;
    LightingMode restoreMode = LightingMode::Wave;
    std::optional<quint32> speed;

    [[nodiscard]] bool operator==(const Lighting &other) const = default;
};

struct DesiredLighting {
    LightingMode mode = LightingMode::Untouched;
    std::array<Rgb, kZoneCount> colors{};
    std::optional<Rgb> baseColor;
    std::optional<quint32> speed;

    [[nodiscard]] bool operator==(const DesiredLighting &other) const = default;
};

[[nodiscard]] inline bool isDeviceLightingMode(LightingMode mode)
{
    return mode != LightingMode::Untouched;
}

[[nodiscard]] inline bool workspaceDesktopIdLooksValid(const QString &value)
{
    if (value.isEmpty() || value.size() > 256) {
        return false;
    }
    for (const QChar ch : value) {
        if (ch.category() == QChar::Other_Control) {
            return false;
        }
        if (!(ch.isLetterOrNumber() || ch == QLatin1Char('.') || ch == QLatin1Char('-') || ch == QLatin1Char('_'))) {
            return false;
        }
    }
    if (value.endsWith(QLatin1String(".desktop"))) {
        return true;
    }
    if (!value.contains(QLatin1Char('.')) || value.startsWith(QLatin1Char('.')) || value.endsWith(QLatin1Char('.'))
        || value.contains(QLatin1String(".."))) {
        return false;
    }
    return true;
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

[[nodiscard]] inline std::array<Rgb, kZoneCount> gradientColors(const Rgb &start, const Rgb &end)
{
    std::array<Rgb, kZoneCount> colors{};
    constexpr int denom = kZoneCount - 1;
    for (int i = 0; i < kZoneCount; ++i) {
        colors[static_cast<size_t>(i)].r =
            static_cast<quint8>((static_cast<int>(start.r) * (denom - i) + static_cast<int>(end.r) * i) / denom);
        colors[static_cast<size_t>(i)].g =
            static_cast<quint8>((static_cast<int>(start.g) * (denom - i) + static_cast<int>(end.g) * i) / denom);
        colors[static_cast<size_t>(i)].b =
            static_cast<quint8>((static_cast<int>(start.b) * (denom - i) + static_cast<int>(end.b) * i) / denom);
    }
    return colors;
}

[[nodiscard]] inline std::array<ZoneValue, kZoneCount> zoneValuesFromColors(const std::array<Rgb, kZoneCount> &colors)
{
    std::array<ZoneValue, kZoneCount> zones{};
    for (int i = 0; i < kZoneCount; ++i) {
        zones[static_cast<size_t>(i)].color = colors[static_cast<size_t>(i)];
    }
    return zones;
}

[[nodiscard]] inline DesiredLighting toDesiredLighting(const Lighting &preset)
{
    DesiredLighting desired;
    desired.mode = preset.mode;
    desired.speed = preset.speed;
    desired.baseColor = preset.baseColor;
    if (preset.mode == LightingMode::Direct) {
        desired.colors = zoneColorsFromPreset(preset);
    }
    if (preset.mode == LightingMode::Breathing && !desired.baseColor) {
        desired.baseColor = kDefaultEffectColor;
    }
    return desired;
}

[[nodiscard]] inline bool zoneRoleIsDynamic(ZoneRole role)
{
    return role == ZoneRole::DesktopIndicator || role == ZoneRole::AppColor;
}

[[nodiscard]] inline bool lightingHasWorkspaceRoles(const Lighting &lighting)
{
    if (!lighting.zones.has_value()) {
        return false;
    }
    for (const ZoneValue &zone : *lighting.zones) {
        if (zoneRoleIsDynamic(zone.role)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] inline bool workspaceLayoutIsActive(const Lighting &globalLighting)
{
    return globalLighting.mode == LightingMode::Direct && lightingHasWorkspaceRoles(globalLighting);
}

[[nodiscard]] inline Rgb dimInactiveDesktop(const Rgb &color)
{
    return Rgb{static_cast<quint8>(color.r / 5), static_cast<quint8>(color.g / 5),
               static_cast<quint8>(color.b / 5)};
}

struct WorkspaceDesktop {
    int position = 0;
    int ordinal = 0;
    QString id;
    QString displayName;

    [[nodiscard]] bool operator==(const WorkspaceDesktop &other) const = default;
};

struct WorkspaceState {
    WorkspaceAvailability availability = WorkspaceAvailability::Unknown;
    QVector<WorkspaceDesktop> desktops;
    QString currentId;
    int currentOrdinal = 0;
    std::optional<int> rows;
    std::optional<bool> navigationWrappingAround;
    bool refreshPending = false;

    [[nodiscard]] bool operator==(const WorkspaceState &other) const = default;
};

struct LightingResolution {
    Lighting lighting;
    DesiredLighting desired;
    std::array<Rgb, kZoneCount> previewColors{};
    bool workspaceLayoutActive = false;
    bool workspaceUnavailable = false;
    bool currentDesktopUnrepresented = false;
    int indicatorCapacity = 0;
    int desktopCount = 0;
    int overflowCount = 0;
    std::array<SlotContribution, kZoneCount> slotContributions{};
    std::array<int, kZoneCount> representedOrdinals{};

    [[nodiscard]] bool operator==(const LightingResolution &other) const = default;
};

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

struct WorkspaceDesktopEntry {
    int ordinal = 0;
    QString name;

    [[nodiscard]] bool operator==(const WorkspaceDesktopEntry &other) const = default;
};

struct WorkspaceSession {
    QString id;
    QString displayName;
    std::optional<int> rows;
    std::optional<bool> navigationWrapping;
    QVector<WorkspaceDesktopEntry> desktops;

    [[nodiscard]] bool operator==(const WorkspaceSession &other) const = default;
};

struct TitleFallback {
    bool enabled = false;
    TitleMatchMode mode = TitleMatchMode::Contains;
    QString pattern;

    [[nodiscard]] bool operator==(const TitleFallback &other) const = default;
};

struct WorkspaceAssignment {
    QString sessionId;
    int desktopOrdinal = 0;
    bool launch = false;
    bool maximize = false;
    std::optional<QString> launchDesktopFile;
    std::optional<TitleFallback> titleFallback;

    [[nodiscard]] bool operator==(const WorkspaceAssignment &other) const = default;
};

struct ApplicationProfile {
    QString id;
    QString displayName;
    MatchSpec match;
    QHash<ControlId, Assignment> keys;
    std::optional<Lighting> lighting;
    std::optional<WorkspaceAssignment> workspace;
};

struct DeviceScope {
    QString vendorId = QString::fromLatin1(kVendorIdText);
    QString productId = QString::fromLatin1(kProductIdText);
    QString model = QString::fromLatin1(kDeviceModel);
};

struct Preferences {
    bool automaticEnabled = true;
    bool trayNotifications = false;
    bool workspaceManagementEnabled = false;
    bool titleFallbackEnabled = false;
    std::optional<QString> activeWorkspaceSessionId;
};

struct ProfileDocument {
    int schemaVersion = kSchemaVersion;
    DeviceScope device;
    QHash<ControlId, Assignment> globalKeys;
    Lighting globalLighting;
    QVector<ApplicationProfile> applications;
    QVector<WorkspaceSession> workspaceSessions;
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
    bool migrationFallback = false;
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
