#include "core/ControlCatalog.h"
#include "core/Persistence.h"
#include "core/Resolver.h"
#include "core/Types.h"
#include "core/ZoneMap.h"

#include <QTest>

using namespace contextdeck;

namespace {

ProfileDocument sampleDocument()
{
    const QByteArray json = R"({
        "schema_version": 1,
        "device": {"vendor_id": "046d", "product_id": "c336", "model": "logitech-g213-prodigy"},
        "global": {
            "keys": {
                "F1": {"action": "pass_through"},
                "F2": {"action": "disabled"},
                "F5": {"action": "emit_shortcut", "chord": {"key": "d", "modifiers": ["ctrl", "shift"]}}
            },
            "lighting": {"mode": "automatic", "base_color": "#222222", "zones": null}
        },
        "applications": [
            {
                "id": "by-class",
                "display_name": "Class App",
                "match": {"resource_class": "Foo"},
                "keys": {
                    "F1": {"action": "disabled"},
                    "F3": {"action": "inherit_global"}
                }
            },
            {
                "id": "by-desktop",
                "display_name": "Desktop App",
                "match": {"desktop_file_name": "bar"},
                "keys": {
                    "F1": {"action": "emit_shortcut", "chord": {"key": "a", "modifiers": ["ctrl"]}}
                }
            }
        ],
        "preferences": {"automatic_enabled": true, "tray_notifications": false}
    })";
    const LoadOutcome loaded = ProfileStore::parseDocument(json);
    Q_ASSERT(loaded.ok);
    return loaded.document;
}

Lighting workspaceLayout()
{
    Lighting lighting;
    lighting.mode = LightingMode::Direct;
    lighting.baseColor = kDefaultEffectColor;
    std::array<ZoneValue, kZoneCount> zones{};
    for (int i = 0; i < 4; ++i) {
        zones[static_cast<size_t>(i)].role = ZoneRole::DesktopIndicator;
        zones[static_cast<size_t>(i)].color = Rgb{0x32, 0x00, 0x00};
    }
    zones[4].role = ZoneRole::AppColor;
    zones[4].color = Rgb{0x40, 0x40, 0x40};
    lighting.zones = zones;
    return lighting;
}

WorkspaceState availableDesktops(int count, int currentOrdinal)
{
    WorkspaceState state;
    state.availability = WorkspaceAvailability::Available;
    for (int i = 0; i < count; ++i) {
        WorkspaceDesktop desktop;
        desktop.position = i;
        desktop.ordinal = i + 1;
        desktop.id = QStringLiteral("d%1").arg(i + 1);
        desktop.displayName = QStringLiteral("Plocha %1").arg(i + 1);
        state.desktops.push_back(desktop);
    }
    state.currentOrdinal = currentOrdinal;
    state.currentId = QStringLiteral("d%1").arg(currentOrdinal);
    return state;
}

} // namespace

class TestProfileResolver : public QObject
{
    Q_OBJECT

private slots:
    void inheritVersusPassThroughVersusDisabled()
    {
        const ProfileDocument document = sampleDocument();
        ApplicationIdentity identity;
        identity.resourceClass = QStringLiteral("Foo");

        QCOMPARE(resolveAssignment(document, identity, ControlId::F1).action, ActionType::Disabled);
        QCOMPARE(resolveAssignment(document, identity, ControlId::F2).action, ActionType::Disabled);
        QCOMPARE(resolveAssignment(document, identity, ControlId::F3).action, ActionType::PassThrough);
        QCOMPARE(resolveAssignment(document, identity, ControlId::F5).action, ActionType::EmitShortcut);
    }

    void applicationPrecedesGlobal()
    {
        const ProfileDocument document = sampleDocument();
        ApplicationIdentity identity;
        identity.desktopFileName = QStringLiteral("bar");
        const Assignment assignment = resolveAssignment(document, identity, ControlId::F1);
        QCOMPARE(assignment.action, ActionType::EmitShortcut);
        QVERIFY(assignment.chord.has_value());
        QCOMPARE(assignment.chord->key, QStringLiteral("a"));
    }

    void missingApplicationFallsBackToGlobalThenPassThrough()
    {
        const ProfileDocument document = sampleDocument();
        ApplicationIdentity identity;
        identity.desktopFileName = QStringLiteral("unknown.desktop");
        QCOMPARE(resolveAssignment(document, identity, ControlId::F2).action, ActionType::Disabled);
        QCOMPARE(resolveAssignment(document, identity, ControlId::F12).action, ActionType::PassThrough);
    }

    void inheritGlobalRejectedOnGlobalProfile()
    {
        ProfileDocument document = sampleDocument();
        Assignment inherit;
        inherit.action = ActionType::InheritGlobal;
        document.globalKeys.insert(ControlId::F4, inherit);
        const PersistenceError error = ProfileStore::validate(document);
        QVERIFY(!error.reason.isEmpty());
        QVERIFY(error.reason.contains(QStringLiteral("inherit_global")));
    }

    void unidentifiedIdentityUsesGlobal()
    {
        const ProfileDocument document = sampleDocument();
        const ApplicationIdentity empty;
        QVERIFY(!empty.isIdentified());
        QCOMPARE(resolveAssignment(document, empty, ControlId::F2).action, ActionType::Disabled);
        QCOMPARE(resolveAssignment(document, empty, ControlId::F9).action, ActionType::PassThrough);
        QVERIFY(matchApplication(document, empty) == nullptr);
    }

    void matcherPrefersDesktopFileNameAndIgnoresCaption()
    {
        const ProfileDocument document = sampleDocument();
        ApplicationIdentity identity;
        identity.desktopFileName = QStringLiteral("bar");
        identity.resourceClass = QStringLiteral("Foo");
        const ApplicationProfile *matched = matchApplication(document, identity);
        QVERIFY(matched != nullptr);
        QCOMPARE(matched->id, QStringLiteral("by-desktop"));

        const QByteArray withCaption = R"({
            "schema_version": 1,
            "device": {"vendor_id": "046d", "product_id": "c336", "model": "logitech-g213-prodigy"},
            "global": {"lighting": {"mode": "automatic", "base_color": "#000000"}},
            "applications": [{
                "id": "captioned",
                "display_name": "Captioned",
                "match": {"caption": "Secret Window", "resource_class": "Foo"}
            }]
        })";
        const LoadOutcome loaded = ProfileStore::parseDocument(withCaption);
        QVERIFY(!loaded.ok);
        QVERIFY(loaded.error.jsonPath.contains(QStringLiteral("caption")));
    }

    void lightingPresetApplicationOverGlobal()
    {
        ProfileDocument document;
        document.globalLighting.mode = LightingMode::Wave;
        ApplicationProfile profile;
        profile.id = QStringLiteral("app");
        profile.displayName = QStringLiteral("App");
        profile.match.resourceClass = QStringLiteral("Foo");
        Lighting appLighting;
        appLighting.mode = LightingMode::Cycle;
        profile.lighting = appLighting;
        document.applications.push_back(profile);

        ApplicationIdentity identity;
        identity.resourceClass = QStringLiteral("Foo");
        QCOMPARE(resolveLighting(document, identity).mode, LightingMode::Cycle);
    }

    void lightingUntouchedWhenNothingSet()
    {
        const ProfileDocument document;
        QCOMPARE(resolveLighting(document, ApplicationIdentity{}).mode, LightingMode::Untouched);
        QCOMPARE(toDesiredLighting(resolveLighting(document, ApplicationIdentity{})).mode, LightingMode::Untouched);
    }

    void unidentifiedContextFallsBackToGlobalPreset()
    {
        ProfileDocument document;
        document.globalLighting.mode = LightingMode::Breathing;
        ApplicationProfile profile;
        profile.id = QStringLiteral("app");
        profile.displayName = QStringLiteral("App");
        profile.match.resourceClass = QStringLiteral("Foo");
        Lighting appLighting;
        appLighting.mode = LightingMode::Off;
        profile.lighting = appLighting;
        document.applications.push_back(profile);

        const ApplicationIdentity empty;
        QVERIFY(!empty.isIdentified());
        QCOMPARE(resolveLighting(document, empty).mode, LightingMode::Breathing);

        ApplicationIdentity unknown;
        unknown.resourceClass = QStringLiteral("Other");
        QCOMPARE(resolveLighting(document, unknown).mode, LightingMode::Breathing);
    }

    void temporaryOverrideOutranksResolvedPreset()
    {
        ProfileDocument document;
        document.globalLighting.mode = LightingMode::Wave;
        ApplicationProfile profile;
        profile.id = QStringLiteral("app");
        profile.displayName = QStringLiteral("App");
        profile.match.resourceClass = QStringLiteral("Foo");
        Lighting appLighting;
        appLighting.mode = LightingMode::Cycle;
        profile.lighting = appLighting;
        document.applications.push_back(profile);

        Lighting override;
        override.mode = LightingMode::Direct;
        override.baseColor = Rgb{0x10, 0x20, 0x30};

        ApplicationIdentity identity;
        identity.resourceClass = QStringLiteral("Foo");
        QCOMPARE(resolveLighting(document, identity).mode, LightingMode::Cycle);
        QCOMPARE(resolveLighting(document, identity, override).mode, LightingMode::Direct);
        QCOMPARE(resolveLighting(document, identity, override).baseColor->r, quint8(0x10));
    }

    void unverifiedZoneAccentIsInert()
    {
        Lighting base;
        base.mode = LightingMode::Direct;
        base.baseColor = Rgb{0x11, 0x22, 0x33};
        std::array<ZoneValue, kZoneCount> zones{};
        zones[0].color = Rgb{0x10, 0x00, 0x00};
        zones[1].color = Rgb{0x00, 0x10, 0x00};
        zones[2].color = Rgb{0x00, 0x00, 0x10};
        zones[3].color = Rgb{0x10, 0x10, 0x00};
        zones[4].color = Rgb{0x00, 0x10, 0x10};
        base.zones = zones;

        const Lighting accented = applyZoneAccent(base, ControlId::F1, Rgb{0xff, 0x00, 0x00});
        QCOMPARE(accented, base);
        QVERIFY(zoneMapEntry(ControlId::F1) != nullptr);
        QVERIFY(!zoneMapEntry(ControlId::F1)->verified);
        QCOMPARE(zoneMap().size(), 20);
    }

    void migratedWorkspaceRolesStayInactiveUntilDirect()
    {
        ProfileDocument document;
        document.globalLighting.mode = LightingMode::Wave;
        std::array<ZoneValue, kZoneCount> zones{};
        zones[0].role = ZoneRole::DesktopIndicator;
        zones[0].color = Rgb{0xff, 0x00, 0x00};
        document.globalLighting.zones = zones;
        QVERIFY(!workspaceLayoutIsActive(document.globalLighting));
        QCOMPARE(resolveLighting(document, ApplicationIdentity{}).mode, LightingMode::Wave);
    }

    void unknownWorkspaceReleasesToDeviceDefault()
    {
        ProfileDocument document;
        document.globalLighting = workspaceLayout();
        const LightingResolution resolution =
            resolveContextLighting(document, ApplicationIdentity{}, WorkspaceState{});
        QVERIFY(resolution.workspaceLayoutActive);
        QVERIFY(resolution.workspaceUnavailable);
        QCOMPARE(resolution.lighting.mode, LightingMode::Untouched);
        QCOMPARE(resolution.slotContributions[0], SlotContribution::DeviceDefault);
    }

    void indicatorBrightnessAndOverflow()
    {
        ProfileDocument document;
        document.globalLighting = workspaceLayout();
        LightingResolution current = resolveContextLighting(document, ApplicationIdentity{}, availableDesktops(2, 1));
        QCOMPARE(current.previewColors[0].r, quint8(0x32));
        QCOMPARE(current.previewColors[1].r, quint8(0x32 / 5));
        QCOMPARE(current.previewColors[2].r, quint8(0));
        QCOMPARE(current.slotContributions[2], SlotContribution::DesktopIndicatorAbsent);
        QVERIFY(!current.currentDesktopUnrepresented);

        LightingResolution overflow = resolveContextLighting(document, ApplicationIdentity{}, availableDesktops(6, 5));
        QCOMPARE(overflow.overflowCount, 2);
        QVERIFY(overflow.currentDesktopUnrepresented);
        QCOMPARE(overflow.previewColors[0].r, quint8(0x32 / 5));
        QCOMPARE(overflow.slotContributions[0], SlotContribution::DesktopIndicatorInactive);
    }

    void applicationContributionTable()
    {
        ProfileDocument document;
        document.globalLighting = workspaceLayout();
        ApplicationProfile profile;
        profile.id = QStringLiteral("app");
        profile.displayName = QStringLiteral("App");
        profile.match.resourceClass = QStringLiteral("Foo");
        Lighting appLighting;
        appLighting.mode = LightingMode::Direct;
        appLighting.baseColor = Rgb{0x10, 0x20, 0x30};
        profile.lighting = appLighting;
        document.applications.push_back(profile);

        ApplicationIdentity identity;
        identity.resourceClass = QStringLiteral("Foo");
        LightingResolution directBase =
            resolveContextLighting(document, identity, availableDesktops(1, 1));
        QCOMPARE(directBase.previewColors[4].r, quint8(0x10));
        QCOMPARE(directBase.previewColors[4].g, quint8(0x20));
        QCOMPARE(directBase.slotContributions[4], SlotContribution::AppColor);
        QCOMPARE(directBase.previewColors[0].r, quint8(0x32));

        std::array<ZoneValue, kZoneCount> zoned{};
        zoned[0].role = ZoneRole::Static;
        zoned[0].color = Rgb{0xaa, 0x00, 0x00};
        zoned[1].role = ZoneRole::Off;
        zoned[2].role = ZoneRole::Static;
        zoned[2].color = Rgb{0x00, 0xbb, 0x00};
        zoned[3].role = ZoneRole::Static;
        zoned[3].color = Rgb{0x00, 0x00, 0xcc};
        zoned[4].role = ZoneRole::Static;
        zoned[4].color = Rgb{0xdd, 0xee, 0xff};
        appLighting.mode = LightingMode::Direct;
        appLighting.baseColor.reset();
        appLighting.zones = zoned;
        document.applications[0].lighting = appLighting;
        LightingResolution directZones =
            resolveContextLighting(document, identity, availableDesktops(1, 1));
        QCOMPARE(directZones.previewColors[4].r, quint8(0xdd));
        QCOMPARE(directZones.previewColors[4].g, quint8(0xee));
        QCOMPARE(directZones.previewColors[4].b, quint8(0xff));

        appLighting.zones.reset();
        appLighting.mode = LightingMode::Breathing;
        appLighting.baseColor = Rgb{0x01, 0x02, 0x03};
        document.applications[0].lighting = appLighting;
        LightingResolution breathingExplicit =
            resolveContextLighting(document, identity, availableDesktops(1, 1));
        QCOMPARE(breathingExplicit.previewColors[4].r, quint8(0x01));
        QCOMPARE(breathingExplicit.previewColors[4].g, quint8(0x02));
        QCOMPARE(breathingExplicit.previewColors[4].b, quint8(0x03));

        appLighting.baseColor.reset();
        document.applications[0].lighting = appLighting;
        LightingResolution breathingDefault =
            resolveContextLighting(document, identity, availableDesktops(1, 1));
        QCOMPARE(breathingDefault.previewColors[4].r, kDefaultEffectColor.r);
        QCOMPARE(breathingDefault.previewColors[4].g, kDefaultEffectColor.g);
        QCOMPARE(breathingDefault.previewColors[4].b, kDefaultEffectColor.b);

        appLighting.mode = LightingMode::Off;
        document.applications[0].lighting = appLighting;
        LightingResolution off = resolveContextLighting(document, identity, availableDesktops(1, 1));
        QCOMPARE(off.previewColors[4].r, quint8(0));
        QCOMPARE(off.previewColors[4].g, quint8(0));
        QCOMPARE(off.previewColors[4].b, quint8(0));

        appLighting.mode = LightingMode::Untouched;
        document.applications[0].lighting = appLighting;
        LightingResolution untouched = resolveContextLighting(document, identity, availableDesktops(1, 1));
        QCOMPARE(untouched.previewColors[4].r, quint8(0x40));

        appLighting.mode = LightingMode::Wave;
        document.applications[0].lighting = appLighting;
        LightingResolution wave = resolveContextLighting(document, identity, availableDesktops(1, 1));
        QCOMPARE(wave.previewColors[4].r, quint8(0x40));

        appLighting.mode = LightingMode::Cycle;
        document.applications[0].lighting = appLighting;
        LightingResolution cycle = resolveContextLighting(document, identity, availableDesktops(1, 1));
        QCOMPARE(cycle.previewColors[4].r, quint8(0x40));

        document.applications[0].lighting.reset();
        LightingResolution matchedWithoutLighting =
            resolveContextLighting(document, identity, availableDesktops(1, 1));
        QCOMPARE(matchedWithoutLighting.previewColors[4].r, quint8(0x40));
        QCOMPARE(matchedWithoutLighting.previewColors[0].r, quint8(0x32));

        LightingResolution unmatched =
            resolveContextLighting(document, ApplicationIdentity{}, availableDesktops(1, 1));
        QCOMPARE(unmatched.previewColors[4].r, quint8(0x40));
        QCOMPARE(unmatched.previewColors[0].r, quint8(0x32));
        QCOMPARE(unmatched.slotContributions[0], SlotContribution::DesktopIndicatorCurrent);

        ProfileDocument ordinary;
        ordinary.globalLighting.mode = LightingMode::Wave;
        QCOMPARE(resolveLighting(ordinary, identity).mode, LightingMode::Wave);
        ApplicationProfile ordinaryApp;
        ordinaryApp.id = QStringLiteral("app");
        ordinaryApp.displayName = QStringLiteral("App");
        ordinaryApp.match.resourceClass = QStringLiteral("Foo");
        Lighting ordinaryLighting;
        ordinaryLighting.mode = LightingMode::Cycle;
        ordinaryApp.lighting = ordinaryLighting;
        ordinary.applications.push_back(ordinaryApp);
        QCOMPARE(resolveLighting(ordinary, identity).mode, LightingMode::Cycle);
        QCOMPARE(resolveLighting(ordinary, ApplicationIdentity{}).mode, LightingMode::Wave);
    }

    void identityLossKeepsWorkspaceIndicators()
    {
        ProfileDocument document;
        document.globalLighting = workspaceLayout();
        ApplicationProfile profile;
        profile.id = QStringLiteral("app");
        profile.displayName = QStringLiteral("App");
        profile.match.resourceClass = QStringLiteral("Foo");
        Lighting appLighting;
        appLighting.mode = LightingMode::Direct;
        appLighting.baseColor = Rgb{0xaa, 0xbb, 0xcc};
        profile.lighting = appLighting;
        document.applications.push_back(profile);

        ApplicationIdentity identity;
        identity.resourceClass = QStringLiteral("Foo");
        LightingResolution matched = resolveContextLighting(document, identity, availableDesktops(2, 1));
        QCOMPARE(matched.previewColors[4].r, quint8(0xaa));
        QCOMPARE(matched.previewColors[0].r, quint8(0x32));
        QCOMPARE(matched.previewColors[1].r, quint8(0x32 / 5));

        LightingResolution lostIdentity =
            resolveContextLighting(document, ApplicationIdentity{}, availableDesktops(2, 1));
        QVERIFY(lostIdentity.workspaceLayoutActive);
        QVERIFY(!lostIdentity.workspaceUnavailable);
        QCOMPARE(lostIdentity.previewColors[0].r, quint8(0x32));
        QCOMPARE(lostIdentity.previewColors[1].r, quint8(0x32 / 5));
        QCOMPARE(lostIdentity.slotContributions[0], SlotContribution::DesktopIndicatorCurrent);
        QCOMPARE(lostIdentity.slotContributions[1], SlotContribution::DesktopIndicatorInactive);
        QCOMPARE(lostIdentity.previewColors[4].r, quint8(0x40));
        QCOMPARE(lostIdentity.slotContributions[4], SlotContribution::AppColor);
    }

    void allBlackWorkspaceUsesOff()
    {
        ProfileDocument document;
        document.globalLighting.mode = LightingMode::Direct;
        std::array<ZoneValue, kZoneCount> zones{};
        for (ZoneValue &zone : zones) {
            zone.role = ZoneRole::DesktopIndicator;
            zone.color = Rgb{};
        }
        document.globalLighting.zones = zones;
        const LightingResolution resolution =
            resolveContextLighting(document, ApplicationIdentity{}, availableDesktops(1, 1));
        QVERIFY(resolution.workspaceLayoutActive);
        QCOMPARE(resolution.lighting.mode, LightingMode::Off);
        QCOMPARE(resolution.desired.mode, LightingMode::Off);
        QCOMPARE(resolution.previewColors[0].r, quint8(0));
    }

    void ordinaryResolveDoesNotMaterializeDynamicRoles()
    {
        ProfileDocument document;
        document.globalLighting = workspaceLayout();
        const Lighting lighting = resolveLighting(document, ApplicationIdentity{});
        QCOMPARE(lighting.mode, LightingMode::Untouched);
        QVERIFY(workspaceLayoutIsActive(document.globalLighting));
    }

    void workspaceAssignmentIdentityBeatsTitleFallback()
    {
        ProfileDocument document;
        document.preferences.titleFallbackEnabled = true;

        ApplicationProfile identityProfile;
        identityProfile.id = QStringLiteral("identity");
        identityProfile.displayName = QStringLiteral("Identity");
        identityProfile.match.desktopFileName = QStringLiteral("editor.desktop");
        WorkspaceAssignment identityWorkspace;
        identityWorkspace.sessionId = QStringLiteral("s");
        identityWorkspace.desktopOrdinal = 1;
        identityProfile.workspace = identityWorkspace;
        document.applications.push_back(identityProfile);

        ApplicationProfile fallbackProfile;
        fallbackProfile.id = QStringLiteral("fallback");
        fallbackProfile.displayName = QStringLiteral("Fallback");
        fallbackProfile.match.resourceClass = QStringLiteral("Something");
        WorkspaceAssignment fallbackWorkspace;
        fallbackWorkspace.sessionId = QStringLiteral("s");
        fallbackWorkspace.desktopOrdinal = 2;
        TitleFallback fallback;
        fallback.enabled = true;
        fallback.mode = TitleMatchMode::Contains;
        fallback.pattern = QStringLiteral("Downloads");
        fallbackWorkspace.titleFallback = fallback;
        fallbackProfile.workspace = fallbackWorkspace;
        document.applications.push_back(fallbackProfile);

        ApplicationIdentity identity;
        identity.desktopFileName = QStringLiteral("editor.desktop");
        const WorkspaceResolution resolution = resolveWorkspaceAssignment(document, identity, QStringLiteral("Downloads"));
        QVERIFY(resolution.profile != nullptr);
        QCOMPARE(resolution.profile->id, QStringLiteral("identity"));
        QVERIFY(!resolution.matchedByTitleFallback);
        QVERIFY(resolution.assignment != nullptr);
        QCOMPARE(resolution.assignment->desktopOrdinal, 1);
    }

    void workspaceTitleFallbackRequiresBothFlags()
    {
        ProfileDocument document;
        ApplicationProfile fallbackProfile;
        fallbackProfile.id = QStringLiteral("fallback");
        fallbackProfile.displayName = QStringLiteral("Fallback");
        fallbackProfile.match.resourceClass = QStringLiteral("Something");
        WorkspaceAssignment workspace;
        workspace.sessionId = QStringLiteral("s");
        workspace.desktopOrdinal = 1;
        TitleFallback fallback;
        fallback.enabled = true;
        fallback.mode = TitleMatchMode::Contains;
        fallback.pattern = QStringLiteral("Downloads");
        workspace.titleFallback = fallback;
        fallbackProfile.workspace = workspace;
        document.applications.push_back(fallbackProfile);

        ApplicationIdentity unmatched;
        unmatched.resourceClass = QStringLiteral("Other");
        QVERIFY(resolveWorkspaceAssignment(document, unmatched, QStringLiteral("Downloads")).profile == nullptr);

        document.preferences.titleFallbackEnabled = true;
        const WorkspaceResolution matched = resolveWorkspaceAssignment(document, unmatched, QStringLiteral("Downloads"));
        QVERIFY(matched.profile != nullptr);
        QVERIFY(matched.matchedByTitleFallback);
        QVERIFY(matched.assignment != nullptr);

        document.applications[0].workspace->titleFallback->enabled = false;
        QVERIFY(resolveWorkspaceAssignment(document, unmatched, QStringLiteral("Downloads")).profile == nullptr);
    }

    void workspaceTitleFallbackModes()
    {
        TitleFallback exact;
        exact.enabled = true;
        exact.mode = TitleMatchMode::Exact;
        exact.pattern = QStringLiteral("My Window");
        QVERIFY(titleFallbackMatches(exact, QStringLiteral("My Window")));
        QVERIFY(!titleFallbackMatches(exact, QStringLiteral("My Window Extra")));

        TitleFallback contains;
        contains.enabled = true;
        contains.mode = TitleMatchMode::Contains;
        contains.pattern = QStringLiteral("Window");
        QVERIFY(titleFallbackMatches(contains, QStringLiteral("My Window Extra")));
        QVERIFY(!titleFallbackMatches(contains, QStringLiteral("unrelated")));

        TitleFallback prefix;
        prefix.enabled = true;
        prefix.mode = TitleMatchMode::Prefix;
        prefix.pattern = QStringLiteral("My ");
        QVERIFY(titleFallbackMatches(prefix, QStringLiteral("My Window")));
        QVERIFY(!titleFallbackMatches(prefix, QStringLiteral("Not My Window")));

        TitleFallback empty;
        empty.enabled = true;
        empty.mode = TitleMatchMode::Contains;
        QVERIFY(!titleFallbackMatches(empty, QStringLiteral("anything")));

        ApplicationIdentity unmatched;
        unmatched.resourceClass = QStringLiteral("Other");
        ProfileDocument document;
        document.preferences.titleFallbackEnabled = true;
        ApplicationProfile profile;
        profile.id = QStringLiteral("fallback");
        profile.displayName = QStringLiteral("Fallback");
        profile.match.resourceClass = QStringLiteral("Something");
        WorkspaceAssignment workspace;
        workspace.sessionId = QStringLiteral("s");
        workspace.desktopOrdinal = 1;
        workspace.titleFallback = exact;
        profile.workspace = workspace;
        document.applications.push_back(profile);
        QVERIFY(resolveWorkspaceAssignment(document, unmatched, QStringLiteral("My Window")).profile != nullptr);
        QVERIFY(resolveWorkspaceAssignment(document, unmatched, QStringLiteral("Other caption")).profile == nullptr);
    }

    void workspaceTitleFallbackLeavesMatchSpecUnchanged()
    {
        ProfileDocument document;
        document.preferences.titleFallbackEnabled = true;
        ApplicationProfile profile;
        profile.id = QStringLiteral("fallback");
        profile.displayName = QStringLiteral("Fallback");
        profile.match.desktopFileName = QStringLiteral("svc.desktop");
        profile.match.resourceClass = QStringLiteral("Svc");
        WorkspaceAssignment workspace;
        workspace.sessionId = QStringLiteral("s");
        workspace.desktopOrdinal = 1;
        TitleFallback fallback;
        fallback.enabled = true;
        fallback.mode = TitleMatchMode::Contains;
        fallback.pattern = QStringLiteral("Downloads");
        workspace.titleFallback = fallback;
        profile.workspace = workspace;
        document.applications.push_back(profile);

        const MatchSpec before = document.applications.at(0).match;
        ApplicationIdentity unmatched;
        unmatched.resourceClass = QStringLiteral("Other");
        const WorkspaceResolution resolution = resolveWorkspaceAssignment(document, unmatched, QStringLiteral("Downloads"));
        QVERIFY(resolution.matchedByTitleFallback);
        QCOMPARE(document.applications.at(0).match.desktopFileName, before.desktopFileName);
        QCOMPARE(document.applications.at(0).match.resourceClass, before.resourceClass);
        QCOMPARE(document.applications.at(0).match.resourceName, before.resourceName);
        QVERIFY(!document.applications.at(0).match.isEmpty());
    }
};

QTEST_MAIN(TestProfileResolver)
#include "test_profile_resolver.moc"
