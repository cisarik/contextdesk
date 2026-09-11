#include "core/ZoneMap.h"

namespace contextdeck {
namespace {

// Hypothesis only. Every entry ships verified=false until the COOPERATOR IRL
// probe fills docs/hardware/g213-zone-map.md. Do not treat these as measured.
const ZoneMapEntry kZoneMap[] = {
    {ControlId::F1, 0, kZoneNames[0], false,
     "F1 sits in the left function-key block, hypothesized over Left Area."},
    {ControlId::F2, 0, kZoneNames[0], false,
     "F2 continues the left function-key block, hypothesized over Left Area."},
    {ControlId::F3, 0, kZoneNames[0], false,
     "F3 continues the left function-key block, hypothesized over Left Area."},
    {ControlId::F4, 0, kZoneNames[0], false,
     "F4 ends the left function-key block, hypothesized over Left Area."},
    {ControlId::F5, 1, kZoneNames[1], false,
     "F5 starts the center function-key block, hypothesized over Middle Area."},
    {ControlId::F6, 1, kZoneNames[1], false,
     "F6 continues the center function-key block, hypothesized over Middle Area."},
    {ControlId::F7, 1, kZoneNames[1], false,
     "F7 continues the center function-key block, hypothesized over Middle Area."},
    {ControlId::F8, 1, kZoneNames[1], false,
     "F8 ends the center function-key block, hypothesized over Middle Area."},
    {ControlId::F9, 2, kZoneNames[2], false,
     "F9 starts the right function-key block, hypothesized over Right Area."},
    {ControlId::F10, 2, kZoneNames[2], false,
     "F10 continues the right function-key block, hypothesized over Right Area."},
    {ControlId::F11, 2, kZoneNames[2], false,
     "F11 continues the right function-key block, hypothesized over Right Area."},
    {ControlId::F12, 2, kZoneNames[2], false,
     "F12 ends the right function-key block, hypothesized over Right Area."},
    {ControlId::Previous, 4, kZoneNames[4], false,
     "Dedicated media keys on the G213 sit on the far-right deck, hypothesized over Numpad."},
    {ControlId::PlayPause, 4, kZoneNames[4], false,
     "Play/Pause is in the far-right media cluster, hypothesized over Numpad."},
    {ControlId::Next, 4, kZoneNames[4], false,
     "Next is in the far-right media cluster, hypothesized over Numpad."},
    {ControlId::Mute, 4, kZoneNames[4], false,
     "Mute is in the far-right media cluster, hypothesized over Numpad."},
    {ControlId::VolumeDown, 4, kZoneNames[4], false,
     "Volume Down is in the far-right media cluster, hypothesized over Numpad."},
    {ControlId::VolumeUp, 4, kZoneNames[4], false,
     "Volume Up is in the far-right media cluster, hypothesized over Numpad."},
    {ControlId::GameMode, 3, kZoneNames[3], false,
     "Game Mode is hypothesized nearer the navigation cluster (Arrow and Homekeys), not the letter field."},
    {ControlId::Backlight, 3, kZoneNames[3], false,
     "Backlight is hypothesized beside Game Mode over Arrow and Homekeys."},
};

} // namespace

QVector<ZoneMapEntry> zoneMap()
{
    QVector<ZoneMapEntry> entries;
    entries.reserve(static_cast<int>(sizeof(kZoneMap) / sizeof(kZoneMap[0])));
    for (const ZoneMapEntry &entry : kZoneMap) {
        entries.push_back(entry);
    }
    return entries;
}

const ZoneMapEntry *zoneMapEntry(ControlId control)
{
    for (const ZoneMapEntry &entry : kZoneMap) {
        if (entry.control == control) {
            return &entry;
        }
    }
    return nullptr;
}

Lighting applyZoneAccent(const Lighting &base, ControlId control, const Rgb &accent)
{
    const ZoneMapEntry *entry = zoneMapEntry(control);
    if (entry == nullptr || !entry->verified) {
        return base;
    }
    if (entry->zoneIndex < 0 || entry->zoneIndex >= kZoneCount) {
        return base;
    }
    Lighting lighting = base;
    lighting.mode = LightingMode::Direct;
    std::array<Rgb, kZoneCount> colors = zoneColorsFromPreset(base);
    colors[static_cast<size_t>(entry->zoneIndex)] = accent;
    lighting.zones = zoneValuesFromColors(colors);
    lighting.baseColor = accent;
    return lighting;
}

} // namespace contextdeck
