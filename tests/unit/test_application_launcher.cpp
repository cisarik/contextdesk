#include "workspace/ApplicationLauncher.h"

#include <QTest>

using namespace contextdeck;

namespace {

WorkspaceLaunchPlan launchEntry(const QString &profileId, const QString &desktopFileId,
                                WorkspaceLaunchIntent intent = WorkspaceLaunchIntent::WouldLaunch)
{
    WorkspaceLaunchPlan entry;
    entry.profileId = profileId;
    entry.displayName = profileId;
    entry.desktopOrdinal = 1;
    entry.desktopFileId = desktopFileId;
    entry.intent = intent;
    return entry;
}

} // namespace

class TestApplicationLauncher : public QObject
{
    Q_OBJECT

private slots:
    void requiresActiveTransaction()
    {
        ApplicationLauncher launcher;
        int calls = 0;
        launcher.setInvokerForTest([&calls](const QString &) {
            ++calls;
            return true;
        });
        QCOMPARE(launcher.requestLaunch(launchEntry(QStringLiteral("a.desktop"), QStringLiteral("a.desktop")), 0),
                 WorkspaceLaunchOutcome::NotInTransaction);
        QCOMPARE(calls, 0);
        QVERIFY(launcher.lastOutcomeName() == QStringLiteral("not_in_transaction"));
    }

    void nonLaunchIntentsNeverInvoke()
    {
        ApplicationLauncher launcher;
        int calls = 0;
        launcher.setInvokerForTest([&calls](const QString &) {
            ++calls;
            return true;
        });
        launcher.beginTransaction(0);
        QCOMPARE(launcher.requestLaunch(
                     launchEntry(QStringLiteral("a.desktop"), QStringLiteral("a.desktop"),
                                 WorkspaceLaunchIntent::Disabled),
                     0),
                 WorkspaceLaunchOutcome::Disabled);
        QCOMPARE(launcher.requestLaunch(
                     launchEntry(QStringLiteral("a.desktop"), QStringLiteral("a.desktop"),
                                 WorkspaceLaunchIntent::AlreadyRunning),
                     0),
                 WorkspaceLaunchOutcome::AlreadyRunning);
        QCOMPARE(launcher.requestLaunch(
                     launchEntry(QStringLiteral("a.desktop"), QStringLiteral("a.desktop"),
                                 WorkspaceLaunchIntent::MissingDesktopFile),
                     0),
                 WorkspaceLaunchOutcome::MissingDesktopFile);
        QCOMPARE(calls, 0);
        QCOMPARE(launcher.launchAttemptsInTransaction(), 0);
        launcher.endTransaction();
    }

    void invalidDesktopIdsAreRejected()
    {
        ApplicationLauncher launcher;
        int calls = 0;
        launcher.setInvokerForTest([&calls](const QString &) {
            ++calls;
            return true;
        });
        launcher.beginTransaction(0);
        const QStringList rejections{QString(), QStringLiteral("sh -c echo hi"),
                                     QStringLiteral("systemd-run --user foo"),
                                     QStringLiteral("kstart --desktop 1 foo"),
                                     QStringLiteral("/usr/bin/foo")};
        for (const QString &value : rejections) {
            QCOMPARE(launcher.requestLaunch(launchEntry(QStringLiteral("a.desktop"), value), 0),
                     WorkspaceLaunchOutcome::InvalidDesktopFile);
        }
        QCOMPARE(calls, 0);
        launcher.endTransaction();
    }

    void invokesSeamWithTypedIdAndDebounces()
    {
        ApplicationLauncher launcher;
        QStringList launched;
        launcher.setInvokerForTest([&launched](const QString &desktopFileId) {
            launched.push_back(desktopFileId);
            return true;
        });
        launcher.beginTransaction(0);
        QCOMPARE(launcher.requestLaunch(launchEntry(QStringLiteral("a.desktop"), QStringLiteral("org.example.A.desktop")), 0),
                 WorkspaceLaunchOutcome::Launched);
        QCOMPARE(launched.size(), 1);
        QCOMPARE(launched.at(0), QStringLiteral("org.example.A.desktop"));
        QVERIFY(launcher.hasAttempted(QStringLiteral("a.desktop")));
        QCOMPARE(launcher.attemptCount(QStringLiteral("a.desktop")), 1);

        QCOMPARE(launcher.requestLaunch(launchEntry(QStringLiteral("a.desktop"), QStringLiteral("org.example.A.desktop")), 500),
                 WorkspaceLaunchOutcome::Debounced);
        QCOMPARE(launched.size(), 1);
        QCOMPARE(launcher.requestLaunch(launchEntry(QStringLiteral("a.desktop"), QStringLiteral("org.example.A.desktop")), 2000),
                 WorkspaceLaunchOutcome::Launched);
        QCOMPARE(launched.size(), 2);
        QCOMPARE(launcher.requestLaunch(launchEntry(QStringLiteral("a.desktop"), QStringLiteral("org.example.A.desktop")), 100000),
                 WorkspaceLaunchOutcome::Debounced);
        QCOMPARE(launcher.attemptCount(QStringLiteral("a.desktop")), 2);
        QCOMPARE(launcher.launchAttemptsInTransaction(), 2);
        launcher.endTransaction();
    }

    void boundedRetryOnlyAfterFailureBeforeWindow()
    {
        ApplicationLauncher launcher;
        int calls = 0;
        bool succeed = false;
        launcher.setInvokerForTest([&calls, &succeed](const QString &) {
            ++calls;
            return succeed;
        });
        launcher.beginTransaction(0);
        QCOMPARE(launcher.requestLaunch(launchEntry(QStringLiteral("a.desktop"), QStringLiteral("a.desktop")), 0),
                 WorkspaceLaunchOutcome::Failed);
        QCOMPARE(calls, 1);
        QCOMPARE(launcher.attemptCount(QStringLiteral("a.desktop")), 1);
        succeed = true;
        QCOMPARE(launcher.retryForDesktopCreated(launchEntry(QStringLiteral("a.desktop"), QStringLiteral("a.desktop")), 10),
                 WorkspaceLaunchOutcome::Launched);
        QCOMPARE(calls, 2);
        QCOMPARE(launcher.attemptCount(QStringLiteral("a.desktop")), 2);
        QCOMPARE(launcher.retryForDesktopCreated(launchEntry(QStringLiteral("a.desktop"), QStringLiteral("a.desktop")), 20),
                 WorkspaceLaunchOutcome::Debounced);
        QCOMPARE(calls, 2);
        launcher.endTransaction();

        ApplicationLauncher noFailure;
        int noFailureCalls = 0;
        noFailure.setInvokerForTest([&noFailureCalls](const QString &) {
            ++noFailureCalls;
            return true;
        });
        noFailure.beginTransaction(0);
        QCOMPARE(noFailure.requestLaunch(launchEntry(QStringLiteral("b.desktop"), QStringLiteral("b.desktop")), 0),
                 WorkspaceLaunchOutcome::Launched);
        QCOMPARE(noFailure.retryForDesktopCreated(launchEntry(QStringLiteral("b.desktop"), QStringLiteral("b.desktop")), 10),
                 WorkspaceLaunchOutcome::Debounced);
        QCOMPARE(noFailureCalls, 1);
        noFailure.endTransaction();
    }

    void transactionEndResetsAttempts()
    {
        ApplicationLauncher launcher;
        int calls = 0;
        launcher.setInvokerForTest([&calls](const QString &) {
            ++calls;
            return true;
        });
        launcher.beginTransaction(0);
        QCOMPARE(launcher.requestLaunch(launchEntry(QStringLiteral("a.desktop"), QStringLiteral("a.desktop")), 0),
                 WorkspaceLaunchOutcome::Launched);
        launcher.endTransaction();
        QVERIFY(!launcher.transactionActive());
        QCOMPARE(launcher.requestLaunch(launchEntry(QStringLiteral("a.desktop"), QStringLiteral("a.desktop")), 100),
                 WorkspaceLaunchOutcome::NotInTransaction);
        launcher.beginTransaction(100);
        QCOMPARE(launcher.requestLaunch(launchEntry(QStringLiteral("a.desktop"), QStringLiteral("a.desktop")), 100),
                 WorkspaceLaunchOutcome::Launched);
        QCOMPARE(calls, 2);
        launcher.endTransaction();
    }

    void outcomeNamesAreBounded()
    {
        QCOMPARE(workspaceLaunchOutcomeName(WorkspaceLaunchOutcome::Launched), QStringLiteral("launched"));
        QCOMPARE(workspaceLaunchOutcomeName(WorkspaceLaunchOutcome::Failed), QStringLiteral("failed"));
        QCOMPARE(workspaceLaunchOutcomeName(WorkspaceLaunchOutcome::Debounced), QStringLiteral("debounced"));
        QCOMPARE(workspaceLaunchOutcomeName(WorkspaceLaunchOutcome::NotInTransaction),
                 QStringLiteral("not_in_transaction"));
    }
};

QTEST_GUILESS_MAIN(TestApplicationLauncher)
#include "test_application_launcher.moc"
