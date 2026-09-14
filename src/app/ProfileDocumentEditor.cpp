#include "app/ProfileDocumentEditor.h"

#include "app/AppController.h"
#include "app/LightingEdit.h"
#include "context/ContextReceiver.h"
#include "core/ControlCatalog.h"
#include "core/Persistence.h"
#include "rgb/OpenRgbClient.h"

#include <QLoggingCategory>

namespace contextdeck {

ProfileDocumentEditor::ProfileDocumentEditor(ProfileStore &store, ProfileDocument &document, ContextReceiver *context,
                                             OpenRgbClient *rgb, SessionLightingMode &sessionLighting,
                                             Rgb &temporaryColor, QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_document(document)
    , m_context(context)
    , m_rgb(rgb)
    , m_sessionLighting(sessionLighting)
    , m_temporaryColor(temporaryColor)
{
}

bool ProfileDocumentEditor::save()
{
    const SaveOutcome outcome = m_store.save(m_document);
    if (!outcome.ok) {
        m_saveStatus = outcome.error.reason;
        qCWarning(lcUi) << "save refused:" << outcome.error.reason << outcome.error.jsonPath
                        << "preserved=" << outcome.error.preserved;
        return false;
    }
    m_saveStatus = QStringLiteral("saved");
    emit documentChanged();
    emit applyLightingRequested();
    return true;
}

void ProfileDocumentEditor::setGlobalColor(const QString &hex)
{
    const auto color = LightingEdit::parseHex(hex);
    if (!color) {
        return;
    }
    m_document.globalLighting.baseColor = *color;
    m_document.globalLighting.mode = LightingMode::Direct;
    m_document.globalLighting.zones.reset();
    m_sessionLighting = SessionLightingMode::Automatic;
    emit documentChanged();
    emit applyLightingRequested();
}

void ProfileDocumentEditor::setApplicationColor(const QString &id, const QString &hex)
{
    const auto color = LightingEdit::parseHex(hex);
    if (!color) {
        return;
    }
    for (ApplicationProfile &profile : m_document.applications) {
        if (profile.id == id) {
            Lighting lighting = LightingEdit::applicationLightingOrSanitized(profile, m_document.globalLighting);
            lighting.baseColor = *color;
            lighting.mode = LightingMode::Direct;
            lighting.zones.reset();
            profile.lighting = lighting;
            emit documentChanged();
            emit applyLightingRequested();
            return;
        }
    }
}

void ProfileDocumentEditor::addProfileFromInventory(int index)
{
    const auto entries = m_context->inventory();
    if (index < 0 || index >= entries.size()) {
        return;
    }
    const InventoryEntry &entry = entries.at(index);
    const QString id = !entry.desktopFileName.isEmpty() ? entry.desktopFileName : entry.resourceClass;
    if (id.isEmpty()) {
        return;
    }
    for (const ApplicationProfile &existing : m_document.applications) {
        if (existing.id == id) {
            return;
        }
    }
    ApplicationProfile profile;
    profile.id = id;
    profile.displayName = id;
    if (!entry.desktopFileName.isEmpty()) {
        profile.match.desktopFileName = entry.desktopFileName;
    } else if (!entry.resourceClass.isEmpty()) {
        profile.match.resourceClass = entry.resourceClass;
    } else {
        profile.match.resourceName = entry.resourceName;
    }
    Lighting lighting = m_document.globalLighting;
    lighting.mode = LightingMode::Direct;
    profile.lighting = LightingEdit::sanitizeApplicationLighting(lighting);
    m_document.applications.push_back(profile);
    emit documentChanged();
}

void ProfileDocumentEditor::removeProfile(const QString &id)
{
    for (int i = 0; i < m_document.applications.size(); ++i) {
        if (m_document.applications.at(i).id == id) {
            m_document.applications.removeAt(i);
            emit documentChanged();
            emit resolvedProfileInvalidated();
            emit applyLightingRequested();
            return;
        }
    }
}

void ProfileDocumentEditor::setAutomatic(bool enabled)
{
    if (enabled) {
        restoreAutomatic();
    } else {
        lightsOff();
    }
}

void ProfileDocumentEditor::lightsOff()
{
    m_sessionLighting = SessionLightingMode::LightsOff;
    m_document.preferences.automaticEnabled = false;
    emit lightingModeChanged();
    emit applyLightingRequested();
}

void ProfileDocumentEditor::restoreAutomatic()
{
    m_sessionLighting = SessionLightingMode::Automatic;
    m_document.preferences.automaticEnabled = true;
    emit lightingModeChanged();
    emit applyLightingRequested();
}

void ProfileDocumentEditor::restoreDeviceDefault()
{
    m_sessionLighting = SessionLightingMode::DeviceDefault;
    m_document.preferences.automaticEnabled = true;
    emit lightingModeChanged();
    emit applyLightingRequested();
}

void ProfileDocumentEditor::setTemporaryColor(const QString &hex)
{
    const auto color = LightingEdit::parseHex(hex);
    if (!color) {
        return;
    }
    m_temporaryColor = *color;
    m_sessionLighting = SessionLightingMode::TemporaryColor;
    m_document.preferences.automaticEnabled = false;
    emit lightingModeChanged();
    emit applyLightingRequested();
}

void ProfileDocumentEditor::setGlobalLightingMode(const QString &modeName)
{
    const auto mode = lightingModeFromJsonName(modeName);
    if (!mode) {
        return;
    }
    LightingEdit::applyMode(m_document.globalLighting, *mode, Rgb{0x7c, 0x3a, 0xed});
    m_sessionLighting = SessionLightingMode::Automatic;
    emit lightingModeChanged();
    emit documentChanged();
    emit applyLightingRequested();
}

void ProfileDocumentEditor::setGlobalZoneColor(int index, const QString &hex)
{
    const auto color = LightingEdit::parseHex(hex);
    if (!color) {
        return;
    }
    if (!LightingEdit::applyZoneColor(m_document.globalLighting, index, *color, Rgb{0x7c, 0x3a, 0xed})) {
        return;
    }
    m_sessionLighting = SessionLightingMode::Automatic;
    emit lightingModeChanged();
    emit documentChanged();
    emit applyLightingRequested();
}

void ProfileDocumentEditor::applyGlobalGradient(const QString &startHex, const QString &endHex)
{
    const auto start = LightingEdit::parseHex(startHex);
    const auto end = LightingEdit::parseHex(endHex);
    if (!start || !end) {
        return;
    }
    LightingEdit::applyGradient(m_document.globalLighting, *start, *end);
    m_sessionLighting = SessionLightingMode::Automatic;
    emit lightingModeChanged();
    emit documentChanged();
    emit applyLightingRequested();
}

void ProfileDocumentEditor::setGlobalZoneRole(int index, const QString &roleName)
{
    if (index < 0 || index >= kZoneCount) {
        return;
    }
    ZoneRole role = ZoneRole::Static;
    if (roleName == QLatin1String("desktop_indicator")) {
        role = ZoneRole::DesktopIndicator;
    } else if (roleName == QLatin1String("app_color")) {
        role = ZoneRole::AppColor;
    } else if (roleName == QLatin1String("off")) {
        role = ZoneRole::Off;
    } else if (roleName != QLatin1String("static")) {
        return;
    }
    std::array<ZoneValue, kZoneCount> zones = m_document.globalLighting.zones.value_or(
        zoneValuesFromColors(zoneColorsFromPreset(m_document.globalLighting)));
    if (!m_document.globalLighting.zones && !m_document.globalLighting.baseColor) {
        for (ZoneValue &zone : zones) {
            zone.color = kDefaultEffectColor;
        }
    }
    zones[static_cast<size_t>(index)].role = role;
    if (role == ZoneRole::Off) {
        zones[static_cast<size_t>(index)].color = {};
    } else if (zones[static_cast<size_t>(index)].color == Rgb{}) {
        zones[static_cast<size_t>(index)].color = kDefaultEffectColor;
    }
    m_document.globalLighting.zones = zones;
    m_document.globalLighting.mode = LightingMode::Direct;
    if (!m_document.globalLighting.baseColor) {
        m_document.globalLighting.baseColor = kDefaultEffectColor;
    }
    m_sessionLighting = SessionLightingMode::Automatic;
    emit lightingModeChanged();
    emit documentChanged();
    emit applyLightingRequested();
}

void ProfileDocumentEditor::useDefaultWorkspaceLayout()
{
    std::array<ZoneValue, kZoneCount> zones{};
    for (int i = 0; i < 4; ++i) {
        zones[static_cast<size_t>(i)].role = ZoneRole::DesktopIndicator;
        zones[static_cast<size_t>(i)].color = kDefaultEffectColor;
    }
    zones[4].role = ZoneRole::AppColor;
    zones[4].color = Rgb{0x40, 0x40, 0x40};
    m_document.globalLighting.mode = LightingMode::Direct;
    m_document.globalLighting.restoreMode = LightingMode::Wave;
    m_document.globalLighting.baseColor = kDefaultEffectColor;
    m_document.globalLighting.zones = zones;
    m_sessionLighting = SessionLightingMode::Automatic;
    emit lightingModeChanged();
    emit documentChanged();
    emit applyLightingRequested();
}

void ProfileDocumentEditor::useStaticZoneLayout()
{
    std::array<Rgb, kZoneCount> colors = zoneColorsFromPreset(m_document.globalLighting);
    if (!m_document.globalLighting.zones && !m_document.globalLighting.baseColor) {
        colors.fill(kDefaultEffectColor);
    }
    m_document.globalLighting.zones = zoneValuesFromColors(colors);
    m_document.globalLighting.mode = LightingMode::Direct;
    if (!m_document.globalLighting.baseColor) {
        m_document.globalLighting.baseColor = colors.front();
    }
    m_sessionLighting = SessionLightingMode::Automatic;
    emit lightingModeChanged();
    emit documentChanged();
    emit applyLightingRequested();
}

void ProfileDocumentEditor::setGlobalSpeed(int percent)
{
    if (!LightingEdit::applySpeedPercent(m_rgb, m_document.globalLighting, percent)) {
        return;
    }
    m_sessionLighting = SessionLightingMode::Automatic;
    emit lightingModeChanged();
    emit documentChanged();
    emit applyLightingRequested();
}

void ProfileDocumentEditor::setGlobalBreathingColor(const QString &hex)
{
    const auto color = LightingEdit::parseHex(hex);
    if (!color) {
        return;
    }
    LightingEdit::applyBreathingColor(m_document.globalLighting, *color);
    m_sessionLighting = SessionLightingMode::Automatic;
    emit lightingModeChanged();
    emit documentChanged();
    emit applyLightingRequested();
}

void ProfileDocumentEditor::setApplicationLightingMode(const QString &id, const QString &modeName)
{
    const auto mode = lightingModeFromJsonName(modeName);
    if (!mode) {
        return;
    }
    for (ApplicationProfile &profile : m_document.applications) {
        if (profile.id == id) {
            Lighting lighting = LightingEdit::applicationLightingOrSanitized(profile, m_document.globalLighting);
            LightingEdit::applyMode(lighting, *mode, Rgb{0x7c, 0x3a, 0xed});
            profile.lighting = lighting;
            emit documentChanged();
            emit applyLightingRequested();
            return;
        }
    }
}

void ProfileDocumentEditor::setApplicationZoneColor(const QString &id, int index, const QString &hex)
{
    const auto color = LightingEdit::parseHex(hex);
    if (!color) {
        return;
    }
    for (ApplicationProfile &profile : m_document.applications) {
        if (profile.id == id) {
            Lighting lighting = LightingEdit::applicationLightingOrSanitized(profile, m_document.globalLighting);
            if (!LightingEdit::applyZoneColor(lighting, index, *color, Rgb{0x7c, 0x3a, 0xed})) {
                return;
            }
            profile.lighting = lighting;
            emit documentChanged();
            emit applyLightingRequested();
            return;
        }
    }
}

void ProfileDocumentEditor::applyApplicationGradient(const QString &id, const QString &startHex, const QString &endHex)
{
    const auto start = LightingEdit::parseHex(startHex);
    const auto end = LightingEdit::parseHex(endHex);
    if (!start || !end) {
        return;
    }
    for (ApplicationProfile &profile : m_document.applications) {
        if (profile.id == id) {
            Lighting lighting = LightingEdit::applicationLightingOrSanitized(profile, m_document.globalLighting);
            LightingEdit::applyGradient(lighting, *start, *end);
            profile.lighting = lighting;
            emit documentChanged();
            emit applyLightingRequested();
            return;
        }
    }
}

void ProfileDocumentEditor::setApplicationSpeed(const QString &id, int percent)
{
    for (ApplicationProfile &profile : m_document.applications) {
        if (profile.id == id) {
            Lighting lighting = LightingEdit::applicationLightingOrSanitized(profile, m_document.globalLighting);
            if (!LightingEdit::applySpeedPercent(m_rgb, lighting, percent)) {
                return;
            }
            profile.lighting = lighting;
            emit documentChanged();
            emit applyLightingRequested();
            return;
        }
    }
}

void ProfileDocumentEditor::setApplicationBreathingColor(const QString &id, const QString &hex)
{
    const auto color = LightingEdit::parseHex(hex);
    if (!color) {
        return;
    }
    for (ApplicationProfile &profile : m_document.applications) {
        if (profile.id == id) {
            Lighting lighting = LightingEdit::applicationLightingOrSanitized(profile, m_document.globalLighting);
            LightingEdit::applyBreathingColor(lighting, *color);
            profile.lighting = lighting;
            emit documentChanged();
            emit applyLightingRequested();
            return;
        }
    }
}

void ProfileDocumentEditor::assignEmitShortcut(const QString &controlName, const QString &key,
                                               const QStringList &modifiers, bool applicationLevel,
                                               const QString &applicationId)
{
    const auto control = controlFromJsonName(controlName);
    if (!control || controlIsConditional(*control)) {
        return;
    }
    if (!isAllowedChordKey(key) || chordKeyLooksLikeShellOrPath(key)) {
        return;
    }
    Assignment assignment;
    assignment.action = ActionType::EmitShortcut;
    Chord chord;
    chord.key = key;
    for (const QString &name : modifiers) {
        const auto modifier = modifierFromJsonName(name);
        if (!modifier) {
            return;
        }
        chord.modifiers.push_back(*modifier);
    }
    assignment.chord = chord;
    if (applicationLevel) {
        for (ApplicationProfile &profile : m_document.applications) {
            if (profile.id == applicationId) {
                profile.keys.insert(*control, assignment);
                emit documentChanged();
                return;
            }
        }
    } else {
        m_document.globalKeys.insert(*control, assignment);
        emit documentChanged();
    }
}

void ProfileDocumentEditor::expireTemporaryColor()
{
    m_sessionLighting = SessionLightingMode::Automatic;
    m_document.preferences.automaticEnabled = true;
    emit lightingModeChanged();
    qCInfo(lcUi) << "temporary_color expired on external identity change";
}

} // namespace contextdeck
