#pragma once

#include "core/Types.h"

#include <optional>

namespace contextdeck {

struct ControlInfo {
    ControlId id;
    const char *jsonName;
    bool conditionalOnHardwareEvidence = false;
};

[[nodiscard]] QVector<ControlInfo> controlCatalog();
[[nodiscard]] std::optional<ControlId> controlFromJsonName(QStringView name);
[[nodiscard]] QString controlJsonName(ControlId id);
[[nodiscard]] bool controlIsConditional(ControlId id);

[[nodiscard]] bool isAllowedChordKey(const QString &key);
[[nodiscard]] bool chordKeyLooksLikeShellOrPath(const QString &key);

[[nodiscard]] std::optional<Modifier> modifierFromJsonName(QStringView name);
[[nodiscard]] QString modifierJsonName(Modifier modifier);

[[nodiscard]] std::optional<ActionType> actionFromJsonName(QStringView name);
[[nodiscard]] QString actionJsonName(ActionType action);

[[nodiscard]] std::optional<SystemActionId> systemActionFromJsonName(QStringView name);
[[nodiscard]] QString systemActionJsonName(SystemActionId id);

[[nodiscard]] std::optional<LightingMode> lightingModeFromJsonName(QStringView name);
[[nodiscard]] QString lightingModeJsonName(LightingMode mode);

} // namespace contextdeck
