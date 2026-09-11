#pragma once

#include "core/Types.h"

#include <optional>

#include <QVector>

namespace contextdeck {

struct ZoneMapEntry {
    ControlId control = ControlId::F1;
    int zoneIndex = 0;
    const char *zoneName = kZoneNames[0];
    bool verified = false;
    const char *rationale = "";
};

[[nodiscard]] QVector<ZoneMapEntry> zoneMap();
[[nodiscard]] const ZoneMapEntry *zoneMapEntry(ControlId control);
[[nodiscard]] Lighting applyZoneAccent(const Lighting &base, ControlId control, const Rgb &accent);

} // namespace contextdeck
