#pragma once

#include "core/Types.h"

namespace contextdeck {

[[nodiscard]] Assignment resolveAssignment(const ProfileDocument &document,
                                           const ApplicationIdentity &identity,
                                           ControlId control);

[[nodiscard]] const ApplicationProfile *matchApplication(const ProfileDocument &document,
                                                         const ApplicationIdentity &identity);

[[nodiscard]] bool applicationMatches(const ApplicationProfile &profile, const ApplicationIdentity &identity);

[[nodiscard]] bool titleFallbackMatches(const TitleFallback &fallback, const QString &caption);

struct WorkspaceResolution {
    const ApplicationProfile *profile = nullptr;
    const WorkspaceAssignment *assignment = nullptr;
    bool matchedByTitleFallback = false;
};

[[nodiscard]] WorkspaceResolution resolveWorkspaceAssignment(const ProfileDocument &document,
                                                             const ApplicationIdentity &identity,
                                                             const QString &caption);

[[nodiscard]] Lighting resolveLighting(const ProfileDocument &document,
                                       const ApplicationIdentity &identity);

[[nodiscard]] Lighting resolveLighting(const ProfileDocument &document,
                                       const ApplicationIdentity &identity,
                                       const std::optional<Lighting> &temporaryOverride);

[[nodiscard]] LightingResolution resolveContextLighting(
    const ProfileDocument &document, const ApplicationIdentity &identity, const WorkspaceState &workspace,
    const std::optional<Lighting> &sessionOverride = std::nullopt);

} // namespace contextdeck
