#include "core/Persistence.h"
#include "core/Resolver.h"
#include "core/Types.h"
#include "workspace/WorkspacePlan.h"

#include <QTest>

using namespace contextdeck;

namespace {

ProfileDocument planDocument()
{
    ProfileDocument document;
    document.schemaVersion = kSchemaVersion;
    document.preferences.workspaceManagementEnabled = true;

    WorkspaceSession session;
    session.id = QStringLiteral("coding");
    session.displayName = QStringLiteral("Coding");
    session.rows = 1;
    session.navigationWrapping = true;
    session.desktops.push_back(WorkspaceDesktopEntry{1, QStringLiteral("Build")});
    session.desktops.push_back(WorkspaceDesktopEntry{2, QStringLiteral("Browse")});
    document.workspaceSessions.push_back(session);

    ApplicationProfile profile;
    profile.id = QStringLiteral("editor.desktop");
    profile.displayName = QStringLiteral("Editor");
    profile.match.desktopFileName = QStringLiteral("editor.desktop");
    WorkspaceAssignment workspace;
    workspace.sessionId = QStringLiteral("coding");
    workspace.desktopOrdinal = 1;
    workspace.launch = true;
    workspace.maximize = true;
    workspace.launchDesktopFile = QStringLiteral("editor.desktop");
    profile.workspace = workspace;
    document.applications.push_back(profile);
    return document;
}

WorkspaceState observedTwo(int rows = 1, bool wrapping = true)
{
    WorkspaceState state;
    state.availability = WorkspaceAvailability::Available;
    for (int i = 0; i < 2; ++i) {
        WorkspaceDesktop desktop;
        desktop.position = i;
        desktop.ordinal = i + 1;
        desktop.id = QStringLiteral("id-%1").arg(i + 1);
        desktop.displayName = (i == 0) ? QStringLiteral("Build") : QStringLiteral("Browse");
        state.desktops.push_back(desktop);
    }
    state.currentOrdinal = 1;
    state.currentId = QStringLiteral("id-1");
    state.rows = rows;
    state.navigationWrappingAround = wrapping;
    return state;
}

} // namespace

class TestWorkspacePlan : public QObject
{
    Q_OBJECT

private slots:
    void matchingSessionProducesNoDiff()
    {
        const ProfileDocument document = planDocument();
        const WorkspacePlan plan = computeWorkspacePlan(document, QStringLiteral("coding"), observedTwo(), {});
        QVERIFY(plan.sessionFound);
        QVERIFY(plan.managementEnabled);
        QVERIFY(plan.observationAvailable);
        QVERIFY(!plan.drift);
        QVERIFY(!plan.extraDesktop);
        QVERIFY(!plan.rowsChange);
        QVERIFY(!plan.wrappingChange);
        QCOMPARE(plan.desiredDesktopCount, 2);
        QCOMPARE(plan.observedDesktopCount, 2);
        QCOMPARE(plan.desktops.size(), 2);
        QVERIFY(!plan.desktops.at(0).create);
        QVERIFY(!plan.desktops.at(0).rename);
        QVERIFY(!plan.desktops.at(1).create);
        QVERIFY(!plan.desktops.at(1).rename);
    }

    void defaultDiffCreatesAndRenamesWithoutRemoval()
    {
        ProfileDocument document = planDocument();
        WorkspaceState observed = observedTwo();
        observed.desktops[0].displayName = QStringLiteral("Old name");
        WorkspaceDesktop extra;
        extra.position = 2;
        extra.ordinal = 3;
        extra.id = QStringLiteral("id-3");
        extra.displayName = QStringLiteral("Extra");
        observed.desktops.push_back(extra);

        const WorkspacePlan plan = computeWorkspacePlan(document, QStringLiteral("coding"), observed, {});
        QVERIFY(plan.drift);
        QVERIFY(plan.extraDesktop);
        QCOMPARE(plan.desktops.size(), 2);
        QVERIFY(plan.desktops.at(0).rename);
        QCOMPARE(plan.desktops.at(0).observedName, QStringLiteral("Old name"));
        QVERIFY(!plan.desktops.at(1).rename);
        QVERIFY(!plan.desktops.at(1).create);
        QCOMPARE(plan.observedDesktopCount, 3);
    }

    void missingDesktopIsCreated()
    {
        ProfileDocument document = planDocument();
        WorkspaceState observed = observedTwo();
        observed.desktops.removeLast();
        const WorkspacePlan plan = computeWorkspacePlan(document, QStringLiteral("coding"), observed, {});
        QVERIFY(plan.desktops.at(1).create);
        QVERIFY(!plan.desktops.at(1).rename);
        QVERIFY(plan.drift);
    }

    void unknownSessionAndUnavailableObservation()
    {
        const ProfileDocument document = planDocument();
        const WorkspacePlan missing = computeWorkspacePlan(document, QStringLiteral("other"), observedTwo(), {});
        QVERIFY(!missing.sessionFound);
        QVERIFY(!missing.drift);
        QVERIFY(missing.desktops.isEmpty());
        QVERIFY(missing.launches.isEmpty());
        QCOMPARE(workspaceLaunchIntentName(WorkspaceLaunchIntent::Disabled), QStringLiteral("disabled"));

        const WorkspacePlan unavailable = computeWorkspacePlan(document, QStringLiteral("coding"), WorkspaceState{}, {});
        QVERIFY(unavailable.sessionFound);
        QVERIFY(!unavailable.observationAvailable);
        QVERIFY(unavailable.desktops.at(0).create);
        QVERIFY(unavailable.desktops.at(1).create);
    }

    void rowsAndWrappingChange()
    {
        ProfileDocument document = planDocument();
        const WorkspaceState observed = observedTwo(2, false);
        const WorkspacePlan plan = computeWorkspacePlan(document, QStringLiteral("coding"), observed, {});
        QVERIFY(plan.rowsChange);
        QVERIFY(plan.wrappingChange);
        QVERIFY(plan.drift);
        QCOMPARE(plan.desiredRows.value(), 1);
        QCOMPARE(plan.observedRows.value(), 2);
        QCOMPARE(plan.desiredWrapping.value(), true);
        QCOMPARE(plan.observedWrapping.value(), false);
    }

    void launchIntentRequiresManagementAndLaunch()
    {
        ProfileDocument document = planDocument();
        document.preferences.workspaceManagementEnabled = false;
        WorkspacePlan plan = computeWorkspacePlan(document, QStringLiteral("coding"), observedTwo(), {});
        QCOMPARE(plan.launches.size(), 1);
        QCOMPARE(plan.launches.at(0).intent, WorkspaceLaunchIntent::Disabled);
        QCOMPARE(plan.launches.at(0).profileId, QStringLiteral("editor.desktop"));

        document.preferences.workspaceManagementEnabled = true;
        plan = computeWorkspacePlan(document, QStringLiteral("coding"), observedTwo(), {});
        QCOMPARE(plan.launches.at(0).intent, WorkspaceLaunchIntent::WouldLaunch);
        QVERIFY(plan.launches.at(0).maximize);

        document.applications[0].workspace->launch = false;
        plan = computeWorkspacePlan(document, QStringLiteral("coding"), observedTwo(), {});
        QCOMPARE(plan.launches.at(0).intent, WorkspaceLaunchIntent::Disabled);
    }

    void alreadyRunningSkipsLaunch()
    {
        const ProfileDocument document = planDocument();
        ApplicationIdentity running;
        running.desktopFileName = QStringLiteral("editor.desktop");
        const WorkspacePlan plan = computeWorkspacePlan(document, QStringLiteral("coding"), observedTwo(), {running});
        QCOMPARE(plan.launches.size(), 1);
        QCOMPARE(plan.launches.at(0).intent, WorkspaceLaunchIntent::AlreadyRunning);
    }

    void missingDesktopFileAndDesktopFileFallback()
    {
        ProfileDocument document = planDocument();
        document.applications[0].workspace->launchDesktopFile.reset();
        document.applications[0].match.desktopFileName = QStringLiteral("org.example.Editor");
        WorkspacePlan plan = computeWorkspacePlan(document, QStringLiteral("coding"), observedTwo(), {});
        QCOMPARE(plan.launches.at(0).intent, WorkspaceLaunchIntent::WouldLaunch);

        document.applications[0].match.desktopFileName.reset();
        document.applications[0].match.resourceClass = QStringLiteral("editor.desktop");
        plan = computeWorkspacePlan(document, QStringLiteral("coding"), observedTwo(), {});
        QCOMPARE(plan.launches.at(0).intent, WorkspaceLaunchIntent::MissingDesktopFile);
    }

    void assignmentsForOtherSessionsAreNotIncluded()
    {
        ProfileDocument document = planDocument();
        ApplicationProfile other;
        other.id = QStringLiteral("browser.desktop");
        other.displayName = QStringLiteral("Browser");
        other.match.desktopFileName = QStringLiteral("browser.desktop");
        WorkspaceAssignment workspace;
        workspace.sessionId = QStringLiteral("browsing");
        workspace.desktopOrdinal = 1;
        workspace.launch = true;
        workspace.launchDesktopFile = QStringLiteral("browser.desktop");
        other.workspace = workspace;
        document.applications.push_back(other);

        const WorkspacePlan plan = computeWorkspacePlan(document, QStringLiteral("coding"), observedTwo(), {});
        QCOMPARE(plan.launches.size(), 1);
        QCOMPARE(plan.launches.at(0).profileId, QStringLiteral("editor.desktop"));
    }

    void triggerClassification()
    {
        QVERIFY(workspaceEventIsLaunchTrigger(WorkspaceEventKind::ApplySession, false));
        QVERIFY(workspaceEventIsLaunchTrigger(WorkspaceEventKind::DesktopCreated, true));
        QVERIFY(!workspaceEventIsLaunchTrigger(WorkspaceEventKind::DesktopCreated, false));
        QVERIFY(!workspaceEventIsLaunchTrigger(WorkspaceEventKind::SessionLogin, true));
        QVERIFY(!workspaceEventIsLaunchTrigger(WorkspaceEventKind::SessionAppStart, true));
        QVERIFY(!workspaceEventIsLaunchTrigger(WorkspaceEventKind::CurrentDesktopChanged, true));
        QVERIFY(!workspaceEventIsLaunchTrigger(WorkspaceEventKind::UserDesktopChange, true));
        QVERIFY(!workspaceEventIsLaunchTrigger(WorkspaceEventKind::BrokerEvent, true));
    }

    void debounceKeys()
    {
        WorkspaceLaunchDebounce debounce;
        QVERIFY(!debounce.tryAttempt(QStringLiteral("editor.desktop"), 0));
        debounce.beginTransaction(0);
        QVERIFY(debounce.isActive());
        QVERIFY(debounce.tryAttempt(QStringLiteral("editor.desktop"), 0));
        QVERIFY(debounce.hasAttempted(QStringLiteral("editor.desktop")));
        QVERIFY(!debounce.tryAttempt(QStringLiteral("editor.desktop"), 500));
        QVERIFY(debounce.tryAttempt(QStringLiteral("editor.desktop"), 2000));
        QVERIFY(!debounce.tryAttempt(QStringLiteral("editor.desktop"), 100000));
        QCOMPARE(debounce.attemptCount(QStringLiteral("editor.desktop")), 2);
        QVERIFY(debounce.tryAttempt(QStringLiteral("browser.desktop"), 2100));
        debounce.endTransaction();
        QVERIFY(!debounce.isActive());
        QVERIFY(!debounce.tryAttempt(QStringLiteral("editor.desktop"), 2200));
        debounce.beginTransaction(0);
        QVERIFY(debounce.tryAttempt(QStringLiteral("editor.desktop"), 0));
    }

    void intentNames()
    {
        QCOMPARE(workspaceLaunchIntentName(WorkspaceLaunchIntent::Disabled), QStringLiteral("disabled"));
        QCOMPARE(workspaceLaunchIntentName(WorkspaceLaunchIntent::WouldLaunch), QStringLiteral("would_launch"));
        QCOMPARE(workspaceLaunchIntentName(WorkspaceLaunchIntent::AlreadyRunning),
                 QStringLiteral("already_running"));
        QCOMPARE(workspaceLaunchIntentName(WorkspaceLaunchIntent::MissingDesktopFile),
                 QStringLiteral("missing_desktop_file"));
    }
};

QTEST_MAIN(TestWorkspacePlan)
#include "test_workspace_plan.moc"
