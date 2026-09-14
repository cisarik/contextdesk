#include "app/LightingEdit.h"

#include "rgb/OpenRgbClient.h"

#include <algorithm>

namespace contextdeck {

namespace {

void ensureDirectColors(Lighting &lighting, const Rgb &fallback)
{
    if (!lighting.baseColor && !lighting.zones) {
        lighting.baseColor = fallback;
    }
}

} // namespace

Lighting LightingEdit::defaultLighting()
{
    return untouchedLighting();
}

Rgb LightingEdit::displayColor(const Lighting &lighting)
{
    if (lighting.baseColor) {
        return *lighting.baseColor;
    }
    if (lighting.zones) {
        return (*lighting.zones)[0].color;
    }
    return Rgb{};
}

QString LightingEdit::toHex(const Rgb &color)
{
    return QStringLiteral("#%1%2%3")
        .arg(color.r, 2, 16, QLatin1Char('0'))
        .arg(color.g, 2, 16, QLatin1Char('0'))
        .arg(color.b, 2, 16, QLatin1Char('0'));
}

QString LightingEdit::hexOf(const Rgb &color)
{
    return toHex(color);
}

QStringList LightingEdit::zoneHexList(const Lighting &lighting)
{
    QStringList list;
    const std::array<Rgb, kZoneCount> colors = zoneColorsFromPreset(lighting);
    for (const Rgb &color : colors) {
        list.push_back(hexOf(color));
    }
    return list;
}

bool LightingEdit::applyMode(Lighting &lighting, LightingMode mode, const Rgb &fallback)
{
    lighting.mode = mode;
    if (mode == LightingMode::Direct) {
        ensureDirectColors(lighting, fallback);
    }
    if (mode == LightingMode::Breathing && !lighting.baseColor) {
        lighting.baseColor = fallback;
    }
    return true;
}

bool LightingEdit::applyZoneColor(Lighting &lighting, int index, const Rgb &color, const Rgb &fallback)
{
    if (index < 0 || index >= kZoneCount) {
        return false;
    }
    if (lighting.zones.has_value()) {
        ZoneValue &zone = (*lighting.zones)[static_cast<size_t>(index)];
        if (zone.role == ZoneRole::Off) {
            return false;
        }
        zone.color = color;
        lighting.baseColor = color;
        lighting.mode = LightingMode::Direct;
        return true;
    }
    std::array<Rgb, kZoneCount> colors = zoneColorsFromPreset(lighting);
    if (!lighting.baseColor) {
        colors.fill(fallback);
    }
    colors[static_cast<size_t>(index)] = color;
    lighting.zones = zoneValuesFromColors(colors);
    lighting.baseColor = color;
    lighting.mode = LightingMode::Direct;
    return true;
}

Lighting LightingEdit::sanitizeApplicationLighting(const Lighting &source)
{
    Lighting lighting = source;
    if (lighting.zones) {
        std::array<ZoneValue, kZoneCount> zones = *lighting.zones;
        for (ZoneValue &zone : zones) {
            if (zone.role == ZoneRole::Off) {
                zone.color = {};
            } else {
                zone.role = ZoneRole::Static;
            }
        }
        lighting.zones = zones;
    }
    if (lighting.mode == LightingMode::Untouched) {
        lighting.mode = LightingMode::Direct;
        if (!lighting.baseColor && !lighting.zones) {
            lighting.baseColor = kDefaultEffectColor;
        }
    }
    return lighting;
}

Lighting LightingEdit::applicationLightingOrSanitized(const ApplicationProfile &profile, const Lighting &global)
{
    if (profile.lighting.has_value()) {
        return *profile.lighting;
    }
    return sanitizeApplicationLighting(global);
}

bool LightingEdit::applyGradient(Lighting &lighting, const Rgb &start, const Rgb &end)
{
    lighting.zones = zoneValuesFromColors(gradientColors(start, end));
    lighting.baseColor = start;
    lighting.mode = LightingMode::Direct;
    return true;
}

std::optional<Rgb> LightingEdit::parseHex(const QString &hex)
{
    QString text = hex.trimmed();
    if (!text.startsWith(QLatin1Char('#'))) {
        text.prepend(QLatin1Char('#'));
    }
    if (text.size() != 7) {
        return std::nullopt;
    }
    bool ok = false;
    const int rgb = text.sliced(1).toInt(&ok, 16);
    if (!ok) {
        return std::nullopt;
    }
    Rgb color;
    color.r = static_cast<quint8>((rgb >> 16) & 0xff);
    color.g = static_cast<quint8>((rgb >> 8) & 0xff);
    color.b = static_cast<quint8>(rgb & 0xff);
    return color;
}

void LightingEdit::speedBounds(OpenRgbClient *rgb, LightingMode mode, quint32 &slowest, quint32 &fastest)
{
    quint32 speedMin = 0;
    quint32 speedMax = 0;
    if (rgb->speedRangeFor(mode, speedMin, speedMax)) {
        slowest = speedMin;
        fastest = speedMax;
        return;
    }
    slowest = 0xC8;
    fastest = 0x0A;
}

quint32 LightingEdit::percentToSpeed(OpenRgbClient *rgb, int percent, LightingMode mode)
{
    quint32 slowest = 0;
    quint32 fastest = 0;
    speedBounds(rgb, mode, slowest, fastest);
    const int clamped = std::clamp(percent, 0, 100);
    const qint64 span = static_cast<qint64>(fastest) - static_cast<qint64>(slowest);
    return static_cast<quint32>(static_cast<qint64>(slowest) + span * clamped / 100);
}

int LightingEdit::speedToPercent(OpenRgbClient *rgb, const Lighting &lighting)
{
    if (!lighting.speed.has_value()) {
        return 50;
    }
    quint32 slowest = 0;
    quint32 fastest = 0;
    speedBounds(rgb, lighting.mode, slowest, fastest);
    if (slowest == fastest) {
        return 50;
    }
    const qint64 span = static_cast<qint64>(fastest) - static_cast<qint64>(slowest);
    const qint64 delta = static_cast<qint64>(*lighting.speed) - static_cast<qint64>(slowest);
    return std::clamp(static_cast<int>((delta * 100 + span / 2) / span), 0, 100);
}

bool LightingEdit::applySpeedPercent(OpenRgbClient *rgb, Lighting &lighting, int percent)
{
    if (percent < 0 || percent > 100) {
        return false;
    }
    LightingMode mode = lighting.mode;
    if (mode != LightingMode::Wave && mode != LightingMode::Cycle && mode != LightingMode::Breathing) {
        mode = LightingMode::Wave;
    }
    lighting.speed = percentToSpeed(rgb, percent, mode);
    return true;
}

bool LightingEdit::applyBreathingColor(Lighting &lighting, const Rgb &color)
{
    lighting.baseColor = color;
    if (lighting.mode != LightingMode::Breathing) {
        lighting.mode = LightingMode::Breathing;
    }
    return true;
}

} // namespace contextdeck
