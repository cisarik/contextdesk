#include "core/ControlCatalog.h"
#include "core/Persistence.h"
#include "core/Resolver.h"
#include "core/Types.h"

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
};

QTEST_MAIN(TestProfileResolver)
#include "test_profile_resolver.moc"
