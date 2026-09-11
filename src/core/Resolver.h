#pragma once

#include "core/Types.h"

namespace contextdeck {

[[nodiscard]] Assignment resolveAssignment(const ProfileDocument &document,
                                           const ApplicationIdentity &identity,
                                           ControlId control);

[[nodiscard]] const ApplicationProfile *matchApplication(const ProfileDocument &document,
                                                         const ApplicationIdentity &identity);

[[nodiscard]] Lighting resolveLighting(const ProfileDocument &document,
                                       const ApplicationIdentity &identity);

[[nodiscard]] Lighting resolveLighting(const ProfileDocument &document,
                                       const ApplicationIdentity &identity,
                                       const std::optional<Lighting> &temporaryOverride);

} // namespace contextdeck
