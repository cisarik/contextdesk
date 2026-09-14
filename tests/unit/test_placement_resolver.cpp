#include "core/Resolver.h"
#include "core/Types.h"
#include "workspace/PlacementResolver.h"

#include <QTest>

using namespace contextdeck;

namespace {

ProfileDocument placementDocument()
{
    ProfileDocument document;
    document.schemaVersion = kSchemaVersion;
    document.preferences.workspaceManagementEnabled = true;
    document.preferences.titleFallbackEnabled = true;

    WorkspaceSession session;
    session.id = QStringLiteral("coding");
    session.displayName = QStringLiteral("Coding");
    session.desktops.push_back(WorkspaceDesktopEntry{1, QStringLiteral("Build")});
    session.desktops.push_back(WorkspaceDesktopEntry{2, QStringLiteral("Browse")});
    document.workspaceSessions.push_back(session);

    ApplicationProfile editor;
    editor.id = QStringLiteral("editor.desktop");
    editor.displayName = QStringLiteral("Editor");
    editor.match.desktopFileName = QStringLiteral("editor.desktop");
    WorkspaceAssignment editorWorkspace;
    editorWorkspace.sessionId = QStringLiteral("coding");
    editorWorkspace.desktopOrdinal = 2;
    editorWorkspace.maximize = true;
    editorWorkspace.titleFallback = TitleFallback{true, TitleMatchMode::Contains, QStringLiteral("Editor")};
    editor.workspace = editorWorkspace;
    document.applications.push_back(editor);

    ApplicationProfile fallbackOnly;
    fallbackOnly.id = QStringLiteral("fallback.desktop");
    fallbackOnly.displayName = QStringLiteral("Fallback");
    fallbackOnly.match.resourceClass = QStringLiteral("FallbackClass");
    WorkspaceAssignment fallbackWorkspace;
    fallbackWorkspace.sessionId = QStringLiteral("coding");
    fallbackWorkspace.desktopOrdinal = 1;
    fallbackWorkspace.titleFallback = TitleFallback{true, TitleMatchMode::Contains, QStringLiteral("Fallback")};
    fallbackOnly.workspace = fallbackWorkspace;
    document.applications.push_back(fallbackOnly);

    return document;
}

WorkspaceState placementState()
{
    WorkspaceState state;
    state.availability = WorkspaceAvailability::Available;
    state.desktops.push_back(WorkspaceDesktop{0, 1, QStringLiteral("uuid-1"), QStringLiteral("Build")});
    state.desktops.push_back(WorkspaceDesktop{1, 2, QStringLiteral("uuid-2"), QStringLiteral("Browse")});
    state.currentId = QStringLiteral("uuid-1");
    state.currentOrdinal = 1;
    return state;
}

} // namespace

class TestPlacementResolver : public QObject
{
    Q_OBJECT

private slots:
    void identityMatchWinsAndMapsOrdinalToLiveId()
    {
        const ProfileDocument document = placementDocument();
        ApplicationIdentity identity;
        identity.desktopFileName = QStringLiteral("editor.desktop");
        const PlacementDecision decision = resolvePlacementDecision(
            document, identity, placementState(), QStringLiteral("coding"), std::nullopt);
        QCOMPARE(decision.desktopId, QStringLiteral("uuid-2"));
        QVERIFY(decision.maximize);
        QVERIFY(!decision.isNoOp());
    }

    void identityWinsOverTitleFallback()
    {
        ProfileDocument document = placementDocument();
        document.preferences.titleFallbackEnabled = true;
        ApplicationIdentity identity;
        identity.desktopFileName = QStringLiteral("editor.desktop");
        const PlacementDecision decision = resolvePlacementDecision(
            document, identity, placementState(), QStringLiteral("coding"), QStringLiteral("Fallback window"));
        QCOMPARE(decision.desktopId, QStringLiteral("uuid-2"));
        QVERIFY(decision.maximize);
    }

    void titleFallbackRequiresBothFlags()
    {
        ProfileDocument document = placementDocument();
        ApplicationIdentity identity;
        identity.resourceClass = QStringLiteral("Something else");
        PlacementDecision decision = resolvePlacementDecision(
            document, identity, placementState(), QStringLiteral("coding"), QStringLiteral("Fallback window"));
        QCOMPARE(decision.desktopId, QStringLiteral("uuid-1"));
        QVERIFY(!decision.maximize);

        document.preferences.titleFallbackEnabled = false;
        decision = resolvePlacementDecision(document, identity, placementState(), QStringLiteral("coding"),
                                            QStringLiteral("Fallback window"));
        QVERIFY(decision.isNoOp());

        document.preferences.titleFallbackEnabled = true;
        document.applications[1].workspace->titleFallback->enabled = false;
        decision = resolvePlacementDecision(document, identity, placementState(), QStringLiteral("coding"),
                                            QStringLiteral("Fallback window"));
        QVERIFY(decision.isNoOp());
    }

    void emptyIdIsNoOp()
    {
        ProfileDocument document = placementDocument();
        ApplicationIdentity identity;
        identity.desktopFileName = QStringLiteral("editor.desktop");
        WorkspaceState missingOrdinal = placementState();
        missingOrdinal.desktops.removeLast();
        QVERIFY(resolvePlacementDecision(document, identity, missingOrdinal, QStringLiteral("coding"), std::nullopt)
                    .isNoOp());

        WorkspaceState unknown;
        QVERIFY(resolvePlacementDecision(document, identity, unknown, QStringLiteral("coding"), std::nullopt).isNoOp());

        QVERIFY(resolvePlacementDecision(document, identity, placementState(), QString(), std::nullopt).isNoOp());
        QVERIFY(resolvePlacementDecision(document, identity, placementState(), QStringLiteral("other"), std::nullopt)
                    .isNoOp());
    }

    void unassignedIdentityIsNoOp()
    {
        const ProfileDocument document = placementDocument();
        ApplicationIdentity identity;
        identity.desktopFileName = QStringLiteral("unmatched.desktop");
        QVERIFY(resolvePlacementDecision(document, identity, placementState(), QStringLiteral("coding"), std::nullopt)
                    .isNoOp());
    }

    void managementDisabledIsNoOp()
    {
        ProfileDocument document = placementDocument();
        document.preferences.workspaceManagementEnabled = false;
        ApplicationIdentity identity;
        identity.desktopFileName = QStringLiteral("editor.desktop");
        QVERIFY(resolvePlacementDecision(document, identity, placementState(), QStringLiteral("coding"), std::nullopt)
                    .isNoOp());
    }
};

QTEST_GUILESS_MAIN(TestPlacementResolver)
#include "test_placement_resolver.moc"
