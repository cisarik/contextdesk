#include "core/Resolver.h"

namespace contextdeck {
namespace {

bool fieldAgrees(const std::optional<QString> &specified, const QString &observed)
{
    if (!specified.has_value()) {
        return true;
    }
    return specified.value() == observed;
}

bool matchAgrees(const MatchSpec &match, const ApplicationIdentity &identity)
{
    if (match.isEmpty()) {
        return false;
    }
    return fieldAgrees(match.desktopFileName, identity.desktopFileName)
        && fieldAgrees(match.resourceClass, identity.resourceClass)
        && fieldAgrees(match.resourceName, identity.resourceName);
}

int matchRank(const MatchSpec &match, const ApplicationIdentity &identity)
{
    if (!matchAgrees(match, identity)) {
        return 0;
    }
    if (match.desktopFileName.has_value() && match.desktopFileName.value() == identity.desktopFileName
        && !identity.desktopFileName.isEmpty()) {
        return 3;
    }
    if (match.resourceClass.has_value() && match.resourceClass.value() == identity.resourceClass
        && !identity.resourceClass.isEmpty()) {
        return 2;
    }
    if (match.resourceName.has_value() && match.resourceName.value() == identity.resourceName
        && !identity.resourceName.isEmpty()) {
        return 1;
    }
    return 0;
}

} // namespace

const ApplicationProfile *matchApplication(const ProfileDocument &document,
                                           const ApplicationIdentity &identity)
{
    if (!identity.isIdentified()) {
        return nullptr;
    }

    const ApplicationProfile *best = nullptr;
    int bestRank = 0;
    for (const ApplicationProfile &profile : document.applications) {
        const int rank = matchRank(profile.match, identity);
        if (rank > bestRank) {
            bestRank = rank;
            best = &profile;
        }
    }
    return best;
}

Assignment resolveAssignment(const ProfileDocument &document,
                             const ApplicationIdentity &identity,
                             ControlId control)
{
    const ApplicationProfile *profile = matchApplication(document, identity);
    if (profile != nullptr && profile->keys.contains(control)) {
        const Assignment &applicationAssignment = profile->keys.value(control);
        if (applicationAssignment.action != ActionType::InheritGlobal) {
            return applicationAssignment;
        }
    }

    if (document.globalKeys.contains(control)) {
        Assignment globalAssignment = document.globalKeys.value(control);
        if (globalAssignment.action == ActionType::InheritGlobal) {
            return passThroughAssignment();
        }
        return globalAssignment;
    }

    return passThroughAssignment();
}

Lighting resolveLighting(const ProfileDocument &document, const ApplicationIdentity &identity,
                         const std::optional<Lighting> &temporaryOverride)
{
    if (temporaryOverride.has_value()) {
        return *temporaryOverride;
    }
    const ApplicationProfile *profile = matchApplication(document, identity);
    if (profile != nullptr && profile->lighting.has_value()) {
        return profile->lighting.value();
    }
    return document.globalLighting;
}

Lighting resolveLighting(const ProfileDocument &document, const ApplicationIdentity &identity)
{
    return resolveLighting(document, identity, std::nullopt);
}

} // namespace contextdeck
