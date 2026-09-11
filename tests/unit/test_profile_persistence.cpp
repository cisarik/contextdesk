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
        QCOMPARE(loaded.document.schemaVersion, kSchemaVersion);
        QVERIFY(loaded.document.globalLighting.baseColor.has_value());
        QCOMPARE(loaded.document.globalLighting.baseColor->r, quint8(0x11));
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
        json.replace(R"("schema_version": 1)", R"("schema_version": 3)");
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
        QCOMPARE(resolveLighting(loaded.document, ApplicationIdentity{}).mode, LightingMode::Untouched);
    }

    void schema1MigratesInMemoryWithoutRewritingFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ProfileStore store(dir.path());
        const QByteArray v1 = validJson();
        QVERIFY(writeBytes(store.documentPath(), v1));
        const LoadOutcome loaded = store.load();
        QVERIFY2(loaded.ok, qPrintable(loaded.error.reason));
        QCOMPARE(loaded.document.schemaVersion, 2);
        QCOMPARE(loaded.document.globalLighting.mode, LightingMode::Untouched);
        QVERIFY(loaded.document.globalLighting.baseColor.has_value());
        QCOMPARE(loaded.document.globalLighting.baseColor->r, quint8(0x11));
        QCOMPARE(loaded.document.globalLighting.restoreMode, LightingMode::Wave);
        QFile file(store.documentPath());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), v1);
    }

    void failedSchema1MigrationPreservesBytesAndYieldsPassThroughUntouched()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ProfileStore store(dir.path());
        const QByteArray v1 = R"({
            "schema_version": 1,
            "device": {"vendor_id": "046d", "product_id": "c336", "model": "logitech-g213-prodigy"},
            "global": {
                "keys": {"F1": {"action": "disabled"}},
                "lighting": {"mode": "not-a-mode", "base_color": "#112233"}
            }
        })";
        QVERIFY(writeBytes(store.documentPath(), v1));
        const LoadOutcome loaded = store.load();
        QVERIFY(loaded.ok);
        QVERIFY(loaded.error.preserved);
        QCOMPARE(loaded.document.globalLighting.mode, LightingMode::Untouched);
        QCOMPARE(resolveAssignment(loaded.document, ApplicationIdentity{}, ControlId::F1).action,
                 ActionType::PassThrough);
        QFile file(store.documentPath());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), v1);
    }

    void schema2RejectsUnknownLightingMode()
    {
        const QByteArray json = R"({
            "schema_version": 2,
            "device": {"vendor_id": "046d", "product_id": "c336", "model": "logitech-g213-prodigy"},
            "global": {"lighting": {"mode": "rainbow", "zones": null}}
        })";
        const LoadOutcome loaded = ProfileStore::parseDocument(json);
        QVERIFY(!loaded.ok);
        QVERIFY(loaded.error.reason.contains(QStringLiteral("unknown lighting mode")));
        QVERIFY(loaded.error.preserved);
    }

    void schema2RejectsWrongZoneCount()
    {
        const QByteArray json = R"({
            "schema_version": 2,
            "device": {"vendor_id": "046d", "product_id": "c336", "model": "logitech-g213-prodigy"},
            "global": {"lighting": {"mode": "direct", "base_color": "#112233", "zones": ["#000000"]}}
        })";
        const LoadOutcome loaded = ProfileStore::parseDocument(json);
        QVERIFY(!loaded.ok);
        QVERIFY(loaded.error.reason.contains(QStringLiteral("exactly five")));
    }

    void schema2RejectsUnknownSemanticField()
    {
        const QByteArray json = R"({
            "schema_version": 2,
            "device": {"vendor_id": "046d", "product_id": "c336", "model": "logitech-g213-prodigy"},
            "global": {"lighting": {"mode": "wave", "zones": null, "plugin": true}}
        })";
        const LoadOutcome loaded = ProfileStore::parseDocument(json);
        QVERIFY(!loaded.ok);
        QVERIFY(loaded.error.reason.contains(QStringLiteral("unknown semantic field")));
        QVERIFY(loaded.error.jsonPath.contains(QStringLiteral("plugin")));
    }

    void schema2RoundTrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ProfileStore store(dir.path());
        const QByteArray json = R"({
            "schema_version": 2,
            "device": {"vendor_id": "046d", "product_id": "c336", "model": "logitech-g213-prodigy"},
            "global": {
                "keys": {"F1": {"action": "pass_through"}},
                "lighting": {
                    "mode": "direct",
                    "restore_mode": "wave",
                    "base_color": "#ff0000",
                    "zones": ["#111111", "#222222", "#333333", "#444444", "#555555"]
                }
            },
            "applications": [],
            "preferences": {"automatic_enabled": true, "tray_notifications": false}
        })";
        const LoadOutcome parsed = ProfileStore::parseDocument(json);
        QVERIFY2(parsed.ok, qPrintable(parsed.error.reason));
        QCOMPARE(parsed.document.globalLighting.mode, LightingMode::Direct);
        QCOMPARE(parsed.document.globalLighting.restoreMode, LightingMode::Wave);
        QVERIFY(parsed.document.globalLighting.zones.has_value());
        QCOMPARE((*parsed.document.globalLighting.zones)[4].color.r, quint8(0x55));
        QVERIFY(store.save(parsed.document).ok);
        const LoadOutcome loaded = store.load();
        QVERIFY(loaded.ok);
        QCOMPARE(loaded.document.schemaVersion, 2);
        QCOMPARE(loaded.document.globalLighting.mode, LightingMode::Direct);
        QCOMPARE((*loaded.document.globalLighting.zones)[0].color.r, quint8(0x11));
        QCOMPARE(loaded.document.globalLighting.baseColor->r, quint8(0xff));
        QVERIFY(!loaded.document.globalLighting.speed.has_value());
    }

    void schema2SpeedRoundTrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ProfileStore store(dir.path());
        const QByteArray json = R"({
            "schema_version": 2,
            "device": {"vendor_id": "046d", "product_id": "c336", "model": "logitech-g213-prodigy"},
            "global": {
                "lighting": {
                    "mode": "breathing",
                    "restore_mode": "wave",
                    "base_color": "#7c3aed",
                    "zones": null,
                    "speed": 80
                }
            }
        })";
        const LoadOutcome parsed = ProfileStore::parseDocument(json);
        QVERIFY2(parsed.ok, qPrintable(parsed.error.reason));
        QCOMPARE(parsed.document.globalLighting.mode, LightingMode::Breathing);
        QVERIFY(parsed.document.globalLighting.speed.has_value());
        QCOMPARE(*parsed.document.globalLighting.speed, quint32(80));
        QCOMPARE(parsed.document.globalLighting.baseColor->r, quint8(0x7c));
        QVERIFY(store.save(parsed.document).ok);
        const LoadOutcome loaded = store.load();
        QVERIFY(loaded.ok);
        QVERIFY(loaded.document.globalLighting.speed.has_value());
        QCOMPARE(*loaded.document.globalLighting.speed, quint32(80));
        QCOMPARE(loaded.document.globalLighting.mode, LightingMode::Breathing);
        QCOMPARE(loaded.document.globalLighting.baseColor->b, quint8(0xed));
    }

    void schema2AbsentSpeedRemainsUnset()
    {
        const QByteArray json = R"({
            "schema_version": 2,
            "device": {"vendor_id": "046d", "product_id": "c336", "model": "logitech-g213-prodigy"},
            "global": {"lighting": {"mode": "wave", "zones": null}}
        })";
        const LoadOutcome loaded = ProfileStore::parseDocument(json);
        QVERIFY2(loaded.ok, qPrintable(loaded.error.reason));
        QVERIFY(!loaded.document.globalLighting.speed.has_value());
    }

    void schema2RejectsNonIntegerSpeed()
    {
        const QByteArray json = R"({
            "schema_version": 2,
            "device": {"vendor_id": "046d", "product_id": "c336", "model": "logitech-g213-prodigy"},
            "global": {"lighting": {"mode": "cycle", "zones": null, "speed": 12.5}}
        })";
        const LoadOutcome loaded = ProfileStore::parseDocument(json);
        QVERIFY(!loaded.ok);
        QVERIFY(loaded.error.reason.contains(QStringLiteral("speed")));
        QVERIFY(loaded.error.preserved);
    }
};

QTEST_MAIN(TestProfilePersistence)
#include "test_profile_persistence.moc"
