#include "workspace/PlacementResolver.h"

#include "core/Resolver.h"
#include "workspace/WorkspacePlan.h"

namespace contextdeck {

PlacementDecision resolvePlacementDecision(const ProfileDocument &document, const ApplicationIdentity &identity,
                                           const WorkspaceState &observed, const QString &sessionId,
                                           const std::optional<QString> &caption)
{
    PlacementDecision decision;
    if (!document.preferences.workspaceManagementEnabled || sessionId.isEmpty()
        || observed.availability != WorkspaceAvailability::Available) {
        return decision;
    }
    if (findWorkspaceSession(document, sessionId) == nullptr) {
        return decision;
    }
    const WorkspaceResolution resolution =
        resolveWorkspaceAssignment(document, identity, caption.value_or(QString()));
    if (resolution.profile == nullptr || resolution.assignment == nullptr
        || resolution.assignment->sessionId != sessionId) {
        return decision;
    }
    for (const WorkspaceDesktop &desktop : observed.desktops) {
        if (desktop.ordinal == resolution.assignment->desktopOrdinal) {
            decision.desktopId = desktop.id;
            decision.maximize = resolution.assignment->maximize;
            return decision;
        }
    }
    return decision;
}

} // namespace contextdeck
