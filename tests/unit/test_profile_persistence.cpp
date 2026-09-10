#include "core/Persistence.h"
#include "core/Resolver.h"
#include "core/Types.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

using namespace contextdeck;

namespace {

QByteArray validJson()
{
    return R"({
        "schema_version": 1,
        "device": {"vendor_id": "046d", "product_id": "c336", "model": "logitech-g213-prodigy"},
        "global": {
            "keys": {
                "F1": {"action": "pass_through"}
            },
            "lighting": {"mode": "automatic", "base_color": "#112233", "zones": null}
        },
        "applications": [],
        "preferences": {"automatic_enabled": true, "tray_notifications": false}
    })";
}

bool writeBytes(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(bytes) == bytes.size();
}

} // namespace

class TestProfilePersistence : public QObject
{
    Q_OBJECT

private slots:
    void validRoundTrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ProfileStore store(dir.path());
        const LoadOutcome parsed = ProfileStore::parseDocument(validJson());
        QVERIFY(parsed.ok);
        const SaveOutcome saved = store.save(parsed.document);
        QVERIFY2(saved.ok, qPrintable(saved.error.reason));
        const LoadOutcome loaded = store.load();
        QVERIFY(loaded.ok);
        QCOMPARE(loaded.document.device.model, QString::fromLatin1(kDeviceModel));
        QCOMPARE(loaded.document.globalLighting.baseColor.r, quint8(0x11));
        QCOMPARE(loaded.document.globalKeys.value(ControlId::F1).action, ActionType::PassThrough);
    }

    void rejectUnknownActionType()
    {
        QByteArray json = validJson();
        json.replace(R"("action": "pass_through")", R"("action": "run_shell")");
        const LoadOutcome loaded = ProfileStore::parseDocument(json);
        QVERIFY(!loaded.ok);
        QVERIFY(loaded.error.reason.contains(QStringLiteral("unknown action type")));
        QVERIFY(loaded.error.preserved);
    }

    void rejectUnknownSemanticField()
    {
        QByteArray json = validJson();
        json.replace(R"("tray_notifications": false)", R"("tray_notifications": false, "telemetry": true)");
        const LoadOutcome loaded = ProfileStore::parseDocument(json);
        QVERIFY(!loaded.ok);
        QVERIFY(loaded.error.reason.contains(QStringLiteral("unknown semantic field")));
        QVERIFY(loaded.error.jsonPath.contains(QStringLiteral("telemetry")));
    }

    void rejectFutureSchemaVersion()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ProfileStore store(dir.path());
        QByteArray json = validJson();
        json.replace(R"("schema_version": 1)", R"("schema_version": 2)");
        QVERIFY(writeBytes(store.documentPath(), json));
        const QByteArray before = json;
        const LoadOutcome loaded = store.load();
        QVERIFY(!loaded.ok);
        QVERIFY(loaded.error.reason.contains(QStringLiteral("future schema_version")));
        QVERIFY(loaded.error.preserved);
        QFile file(store.documentPath());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), before);
    }

    void rejectNonG213DeviceScope()
    {
        QByteArray json = validJson();
        json.replace(R"("product_id": "c336")", R"("product_id": "c32b")");
        const LoadOutcome loaded = ProfileStore::parseDocument(json);
        QVERIFY(!loaded.ok);
        QVERIFY(loaded.error.reason.contains(QStringLiteral("G213")));
    }

    void rejectShellLookingChord()
    {
        QByteArray json = validJson();
        json.replace(R"("F1": {"action": "pass_through"})",
                     R"("F1": {"action": "emit_shortcut", "chord": {"key": "/bin/sh", "modifiers": []}})");
        const LoadOutcome loaded = ProfileStore::parseDocument(json);
        QVERIFY(!loaded.ok);
        QVERIFY(loaded.error.reason.contains(QStringLiteral("shell"))
                || loaded.error.reason.contains(QStringLiteral("path")));
    }

    void failedSavePreservesPreviousBytes()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ProfileStore store(dir.path());
        const LoadOutcome parsed = ProfileStore::parseDocument(validJson());
        QVERIFY(parsed.ok);
        QVERIFY(store.save(parsed.document).ok);
        QFile original(store.documentPath());
        QVERIFY(original.open(QIODevice::ReadOnly));
        const QByteArray before = original.readAll();
        original.close();

        QFile dirFile(dir.path() + QStringLiteral("/contextdeck"));
        const QFileDevice::Permissions previous = dirFile.permissions();
        QVERIFY(dirFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::ExeOwner));

        ProfileDocument mutated = parsed.document;
        mutated.preferences.trayNotifications = true;
        const SaveOutcome saved = store.save(mutated);
        QVERIFY(!saved.ok);
        QVERIFY(saved.error.preserved);

        QVERIFY(dirFile.setPermissions(previous));
        QFile after(store.documentPath());
        QVERIFY(after.open(QIODevice::ReadOnly));
        QCOMPARE(after.readAll(), before);
    }

    void backupRetainedAfterReplacement()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ProfileStore store(dir.path());
        const LoadOutcome first = ProfileStore::parseDocument(validJson());
        QVERIFY(first.ok);
        QVERIFY(store.save(first.document).ok);
        QFile original(store.documentPath());
        QVERIFY(original.open(QIODevice::ReadOnly));
        const QByteArray firstBytes = original.readAll();
        original.close();

        ProfileDocument second = first.document;
        second.preferences.trayNotifications = true;
        QVERIFY(store.save(second).ok);
        QFile backup(store.backupPath());
        QVERIFY(backup.exists());
        QVERIFY(backup.open(QIODevice::ReadOnly));
        QCOMPARE(backup.readAll(), firstBytes);
    }

    void coldStartMissingFileWritesNothing()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ProfileStore store(dir.path());
        const LoadOutcome loaded = store.load();
        QVERIFY(loaded.ok);
        QVERIFY(loaded.missing);
        QCOMPARE(loaded.document.globalKeys.size(), 0);
        QVERIFY(!QFile::exists(store.documentPath()));
        QCOMPARE(resolveAssignment(loaded.document, ApplicationIdentity{}, ControlId::F1).action,
                 ActionType::PassThrough);
    }
};

QTEST_MAIN(TestProfilePersistence)
#include "test_profile_persistence.moc"
