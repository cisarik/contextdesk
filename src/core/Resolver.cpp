#include "core/Resolver.h"

#include <algorithm>

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

Rgb zoneColorAt(const Lighting &lighting, int index, const Rgb &fallback)
{
    if (lighting.zones.has_value()) {
        const ZoneValue &zone = (*lighting.zones)[static_cast<size_t>(index)];
        if (zone.role == ZoneRole::Off) {
            return Rgb{};
        }
        return zone.color;
    }
    if (lighting.baseColor.has_value()) {
        return *lighting.baseColor;
    }
    return fallback;
}

Rgb applicationSlotColor(const Lighting &preset, int index, const Rgb &fallback)
{
    switch (preset.mode) {
    case LightingMode::Direct:
        return zoneColorAt(preset, index, fallback);
    case LightingMode::Breathing:
        return preset.baseColor.value_or(kDefaultEffectColor);
    case LightingMode::Off:
        return Rgb{};
    case LightingMode::Untouched:
    case LightingMode::Wave:
    case LightingMode::Cycle:
        return fallback;
    }
    return fallback;
}

void fillPreview(LightingResolution &result)
{
    if (result.lighting.mode == LightingMode::Direct) {
        result.previewColors = zoneColorsFromPreset(result.lighting);
    } else if (result.lighting.mode == LightingMode::Off) {
        result.previewColors = {};
    } else {
        result.previewColors = zoneColorsFromPreset(result.lighting);
    }
    result.desired = toDesiredLighting(result.lighting);
}

Lighting ordinaryResolvedLighting(const ProfileDocument &document, const ApplicationIdentity &identity,
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

bool applicationMatches(const ApplicationProfile &profile, const ApplicationIdentity &identity)
{
    return matchAgrees(profile.match, identity);
}

bool titleFallbackMatches(const TitleFallback &fallback, const QString &caption)
{
    if (fallback.pattern.isEmpty()) {
        return false;
    }
    switch (fallback.mode) {
    case TitleMatchMode::Exact:
        return caption == fallback.pattern;
    case TitleMatchMode::Contains:
        return caption.contains(fallback.pattern);
    case TitleMatchMode::Prefix:
        return caption.startsWith(fallback.pattern);
    }
    return false;
}

WorkspaceResolution resolveWorkspaceAssignment(const ProfileDocument &document, const ApplicationIdentity &identity,
                                               const QString &caption)
{
    WorkspaceResolution resolution;
    const ApplicationProfile *profile = matchApplication(document, identity);
    if (profile == nullptr && document.preferences.titleFallbackEnabled && !caption.isEmpty()) {
        for (const ApplicationProfile &candidate : document.applications) {
            if (!candidate.workspace || !candidate.workspace->titleFallback) {
                continue;
            }
            const TitleFallback &fallback = *candidate.workspace->titleFallback;
            if (!fallback.enabled) {
                continue;
            }
            if (titleFallbackMatches(fallback, caption)) {
                profile = &candidate;
                resolution.matchedByTitleFallback = true;
                break;
            }
        }
    }
    resolution.profile = profile;
    resolution.assignment = (profile != nullptr && profile->workspace) ? &*profile->workspace : nullptr;
    return resolution;
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

LightingResolution resolveContextLighting(const ProfileDocument &document, const ApplicationIdentity &identity,
                                          const WorkspaceState &workspace,
                                          const std::optional<Lighting> &sessionOverride)
{
    LightingResolution result;
    if (sessionOverride.has_value()) {
        result.lighting = *sessionOverride;
        fillPreview(result);
        result.slotContributions.fill(SlotContribution::SessionOverride);
        return result;
    }

    result.workspaceLayoutActive = workspaceLayoutIsActive(document.globalLighting);
    if (!result.workspaceLayoutActive) {
        result.lighting = ordinaryResolvedLighting(document, identity, std::nullopt);
        fillPreview(result);
        return result;
    }

    if (workspace.availability != WorkspaceAvailability::Available) {
        result.workspaceUnavailable = true;
        result.lighting = untouchedLighting();
        fillPreview(result);
        result.slotContributions.fill(SlotContribution::DeviceDefault);
        return result;
    }

    const std::array<ZoneValue, kZoneCount> &layoutSlots = *document.globalLighting.zones;
    int indicatorCapacity = 0;
    for (const ZoneValue &slot : layoutSlots) {
        if (slot.role == ZoneRole::DesktopIndicator) {
            ++indicatorCapacity;
        }
    }
    result.indicatorCapacity = indicatorCapacity;
    result.desktopCount = workspace.desktops.size();
    result.overflowCount = std::max(0, result.desktopCount - indicatorCapacity);
    result.currentDesktopUnrepresented =
        indicatorCapacity == 0 || workspace.currentOrdinal > indicatorCapacity || workspace.currentOrdinal < 1;

    const ApplicationProfile *matched = matchApplication(document, identity);
    std::array<Rgb, kZoneCount> colors{};
    int indicatorN = 0;
    bool allBlack = true;
    for (int i = 0; i < kZoneCount; ++i) {
        const ZoneValue &slot = layoutSlots[static_cast<size_t>(i)];
        Rgb color{};
        SlotContribution contribution = SlotContribution::None;
        int represented = 0;
        switch (slot.role) {
        case ZoneRole::Static:
            color = slot.color;
            contribution = SlotContribution::Static;
            break;
        case ZoneRole::Off:
            color = Rgb{};
            contribution = SlotContribution::Off;
            break;
        case ZoneRole::DesktopIndicator: {
            ++indicatorN;
            represented = indicatorN;
            if (indicatorN > result.desktopCount) {
                color = Rgb{};
                contribution = SlotContribution::DesktopIndicatorAbsent;
            } else if (!result.currentDesktopUnrepresented && workspace.currentOrdinal == indicatorN) {
                color = slot.color;
                contribution = SlotContribution::DesktopIndicatorCurrent;
            } else {
                color = dimInactiveDesktop(slot.color);
                contribution = SlotContribution::DesktopIndicatorInactive;
            }
            break;
        }
        case ZoneRole::AppColor: {
            const Rgb fallback = slot.color;
            if (matched != nullptr && matched->lighting.has_value()) {
                color = applicationSlotColor(*matched->lighting, i, fallback);
            } else {
                color = fallback;
            }
            contribution = SlotContribution::AppColor;
            break;
        }
        }
        colors[static_cast<size_t>(i)] = color;
        result.slotContributions[static_cast<size_t>(i)] = contribution;
        result.representedOrdinals[static_cast<size_t>(i)] = represented;
        if (color.r != 0 || color.g != 0 || color.b != 0) {
            allBlack = false;
        }
    }

    result.previewColors = colors;
    if (allBlack) {
        result.lighting.mode = LightingMode::Off;
        result.desired = toDesiredLighting(result.lighting);
        result.desired.colors = colors;
    } else {
        result.lighting.mode = LightingMode::Direct;
        result.lighting.zones = zoneValuesFromColors(colors);
        result.lighting.baseColor = colors.front();
        result.desired = toDesiredLighting(result.lighting);
        result.desired.colors = colors;
    }
    return result;
}

Lighting resolveLighting(const ProfileDocument &document, const ApplicationIdentity &identity,
                         const std::optional<Lighting> &temporaryOverride)
{
    return resolveContextLighting(document, identity, WorkspaceState{}, temporaryOverride).lighting;
}

Lighting resolveLighting(const ProfileDocument &document, const ApplicationIdentity &identity)
{
    return resolveLighting(document, identity, std::nullopt);
}

} // namespace contextdeck
