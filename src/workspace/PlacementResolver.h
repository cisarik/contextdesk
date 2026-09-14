#pragma once

#include "core/Types.h"

#include <QString>

#include <optional>

namespace contextdeck {

struct PlacementDecision {
    QString desktopId;
    bool maximize = false;

    [[nodiscard]] bool operator==(const PlacementDecision &other) const = default;
    [[nodiscard]] bool isNoOp() const { return desktopId.isEmpty(); }
};

[[nodiscard]] PlacementDecision resolvePlacementDecision(const ProfileDocument &document,
                                                         const ApplicationIdentity &identity,
                                                         const WorkspaceState &observed,
                                                         const QString &sessionId,
                                                         const std::optional<QString> &caption);

} // namespace contextdeck
