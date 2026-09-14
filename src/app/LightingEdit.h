#pragma once

#include "core/Types.h"

#include <QString>
#include <QStringList>

#include <optional>

namespace contextdeck {

class OpenRgbClient;

// Stateless lighting helpers extracted from AppController: preset/zone/gradient
// edits, application lighting sanitization, hex parsing, and speed conversion.
class LightingEdit
{
public:
    [[nodiscard]] static Lighting defaultLighting();
    [[nodiscard]] static Rgb displayColor(const Lighting &lighting);
    [[nodiscard]] static QString toHex(const Rgb &color);
    [[nodiscard]] static QString hexOf(const Rgb &color);
    [[nodiscard]] static QStringList zoneHexList(const Lighting &lighting);
    static bool applyMode(Lighting &lighting, LightingMode mode, const Rgb &fallback);
    static bool applyZoneColor(Lighting &lighting, int index, const Rgb &color, const Rgb &fallback);
    [[nodiscard]] static Lighting sanitizeApplicationLighting(const Lighting &source);
    [[nodiscard]] static Lighting applicationLightingOrSanitized(const ApplicationProfile &profile,
                                                                 const Lighting &global);
    static bool applyGradient(Lighting &lighting, const Rgb &start, const Rgb &end);
    [[nodiscard]] static std::optional<Rgb> parseHex(const QString &hex);
    static void speedBounds(OpenRgbClient *rgb, LightingMode mode, quint32 &slowest, quint32 &fastest);
    [[nodiscard]] static quint32 percentToSpeed(OpenRgbClient *rgb, int percent, LightingMode mode);
    [[nodiscard]] static int speedToPercent(OpenRgbClient *rgb, const Lighting &lighting);
    static bool applySpeedPercent(OpenRgbClient *rgb, Lighting &lighting, int percent);
    static bool applyBreathingColor(Lighting &lighting, const Rgb &color);
};

} // namespace contextdeck
