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

QByteArray schema3LightingDocument(const QByteArray &lighting)
{
    QByteArray json = QByteArrayLiteral(
        "{\"schema_version\": 3, \"device\": {\"vendor_id\": \"046d\", \"product_id\": \"c336\", \"model\": \"logitech-g213-prodigy\"}, \"global\": {\"lighting\": ");
    json += lighting;
    json += QByteArrayLiteral("}}");
    return json;
}

QByteArray schema3FiveStaticZones(const QByteArray &firstZone)
{
    QByteArray json = QByteArrayLiteral("{\"mode\": \"direct\", \"base_color\": \"#112233\", \"zones\": [");
    json += firstZone;
    json += QByteArrayLiteral(
        ", {\"role\": \"static\", \"color\": \"#222222\"}, {\"role\": \"static\", \"color\": \"#333333\"},");
    json += QByteArrayLiteral(
        " {\"role\": \"static\", \"color\": \"#444444\"}, {\"role\": \"static\", \"color\": \"#555555\"}]}");
    return json;
}

const QByteArray kSchema2Device =
    QByteArrayLiteral("\"device\": {\"vendor_id\": \"046d\", \"product_id\": \"c336\", \"model\": \"logitech-g213-prodigy\"}");

QByteArray schema2Document(const QByteArray &restAfterDeviceComma)
{
    QByteArray json = QByteArrayLiteral("{\"schema_version\": 2, ");
    json += kSchema2Device;
    json += restAfterDeviceComma;
    return json;
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
        json.replace(R"("schema_version": 1)", R"("schema_version": 4)");
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
        QCOMPARE(loaded.document.schemaVersion, 3);
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
        QFile originalFile(store.documentPath());
        QVERIFY(originalFile.open(QIODevice::ReadOnly));
        const QByteArray savedBytes = originalFile.readAll();
        originalFile.close();
        QVERIFY(savedBytes.contains(R"("schema_version": 3)"));
        QVERIFY(savedBytes.contains(R"("role": "static")"));
        const LoadOutcome loaded = store.load();
        QVERIFY(loaded.ok);
        QCOMPARE(loaded.document.schemaVersion, 3);
        QCOMPARE((*loaded.document.globalLighting.zones)[0].role, ZoneRole::Static);
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
        QCOMPARE(loaded.document.schemaVersion, 3);
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

    void schema2ReadDoesNotRewriteBytes()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ProfileStore store(dir.path());
        const QByteArray json = R"({
            "schema_version": 2,
            "device": {"vendor_id": "046d", "product_id": "c336", "model": "logitech-g213-prodigy"},
            "global": {"lighting": {"mode": "direct", "base_color": "#112233", "zones": ["#111111", "#222222", "#333333", "#444444", "#555555"]}}
        })";
        QVERIFY(writeBytes(store.documentPath(), json));
        const LoadOutcome loaded = store.load();
        QVERIFY(loaded.ok);
        QCOMPARE((*loaded.document.globalLighting.zones)[2].role, ZoneRole::Static);
        QVERIFY(!workspaceLayoutIsActive(loaded.document.globalLighting));
        QFile file(store.documentPath());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), json);
    }

    void schema3RoundTripAndRoleValidation()
    {
        const QByteArray json = R"({
            "schema_version": 3,
            "device": {"vendor_id": "046d", "product_id": "c336", "model": "logitech-g213-prodigy"},
            "global": {
                "lighting": {
                    "mode": "direct",
                    "restore_mode": "wave",
                    "base_color": "#7c3aed",
                    "zones": [
                        {"role": "desktop_indicator", "color": "#7c3aed"},
                        {"role": "desktop_indicator", "color": "#7c3aed"},
                        {"role": "desktop_indicator", "color": "#7c3aed"},
                        {"role": "desktop_indicator", "color": "#7c3aed"},
                        {"role": "app_color", "color": "#404040"}
                    ]
                }
            }
        })";
        const LoadOutcome loaded = ProfileStore::parseDocument(json);
        QVERIFY2(loaded.ok, qPrintable(loaded.error.reason));
        QVERIFY(workspaceLayoutIsActive(loaded.document.globalLighting));
        QCOMPARE((*loaded.document.globalLighting.zones)[4].role, ZoneRole::AppColor);

        QTemporaryDir dir;
        ProfileStore store(dir.path());
        QVERIFY(store.save(loaded.document).ok);
        const LoadOutcome roundTrip = store.load();
        QVERIFY(roundTrip.ok);
        QCOMPARE(roundTrip.document.schemaVersion, 3);
        QCOMPARE((*roundTrip.document.globalLighting.zones)[4].color.r, quint8(0x40));
    }

    void schema3RejectsWrongCountUnknownRoleAndApplicationDynamicRoles()
    {
        const QByteArray wrongCount = R"({
            "schema_version": 3,
            "device": {"vendor_id": "046d", "product_id": "c336", "model": "logitech-g213-prodigy"},
            "global": {"lighting": {"mode": "direct", "base_color": "#112233", "zones": [{"role": "static", "color": "#112233"}]}}
        })";
        QVERIFY(!ProfileStore::parseDocument(wrongCount).ok);

        const QByteArray unknownRole = R"({
            "schema_version": 3,
            "device": {"vendor_id": "046d", "product_id": "c336", "model": "logitech-g213-prodigy"},
            "global": {"lighting": {"mode": "direct", "base_color": "#112233", "zones": [
                {"role": "mapped_key_accent", "color": "#112233"},
                {"role": "static", "color": "#112233"},
                {"role": "static", "color": "#112233"},
                {"role": "static", "color": "#112233"},
                {"role": "static", "color": "#112233"}
            ]}}
        })";
        QVERIFY(!ProfileStore::parseDocument(unknownRole).ok);

        const QByteArray offWithColor = R"({
            "schema_version": 3,
            "device": {"vendor_id": "046d", "product_id": "c336", "model": "logitech-g213-prodigy"},
            "global": {"lighting": {"mode": "direct", "base_color": "#112233", "zones": [
                {"role": "off", "color": "#112233"},
                {"role": "static", "color": "#112233"},
                {"role": "static", "color": "#112233"},
                {"role": "static", "color": "#112233"},
                {"role": "static", "color": "#112233"}
            ]}}
        })";
        QVERIFY(!ProfileStore::parseDocument(offWithColor).ok);

        const QByteArray appDynamic = R"({
            "schema_version": 3,
            "device": {"vendor_id": "046d", "product_id": "c336", "model": "logitech-g213-prodigy"},
            "global": {"lighting": {"mode": "direct", "base_color": "#112233", "zones": [
                {"role": "static", "color": "#111111"},
                {"role": "static", "color": "#222222"},
                {"role": "static", "color": "#333333"},
                {"role": "static", "color": "#444444"},
                {"role": "static", "color": "#555555"}
            ]}},
            "applications": [{
                "id": "editor",
                "display_name": "Editor",
                "match": {"resource_class": "Foo"},
                "lighting": {"mode": "direct", "base_color": "#ff8000", "zones": [
                    {"role": "desktop_indicator", "color": "#ff8000"},
                    {"role": "static", "color": "#ff8000"},
                    {"role": "static", "color": "#ff8000"},
                    {"role": "static", "color": "#ff8000"},
                    {"role": "static", "color": "#ff8000"}
                ]}
            }]
        })";
        const LoadOutcome loaded = ProfileStore::parseDocument(appDynamic);
        QVERIFY(!loaded.ok);
        QVERIFY(loaded.error.reason.contains(QStringLiteral("static or off")));
    }

    void schema3RejectsUnknownFieldAndFutureSchema()
    {
        const QByteArray unknown = R"({
            "schema_version": 3,
            "device": {"vendor_id": "046d", "product_id": "c336", "model": "logitech-g213-prodigy"},
            "global": {"lighting": {"mode": "wave", "zones": null, "plugin": true}}
        })";
        QVERIFY(!ProfileStore::parseDocument(unknown).ok);

        const QByteArray future = R"({
            "schema_version": 4,
            "device": {"vendor_id": "046d", "product_id": "c336", "model": "logitech-g213-prodigy"},
            "global": {"lighting": {"mode": "wave", "zones": null}}
        })";
        const LoadOutcome loaded = ProfileStore::parseDocument(future);
        QVERIFY(!loaded.ok);
        QVERIFY(loaded.error.reason.contains(QStringLiteral("future schema_version")));
    }

    void refuseOverwriteOfInvalidAndFallbackDocuments()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ProfileStore store(dir.path());
        const QByteArray invalid = R"({ "schema_version": 4, "device": {"vendor_id": "046d", "product_id": "c336", "model": "logitech-g213-prodigy"}, "global": {"lighting": {"mode": "wave"}} })";
        QVERIFY(writeBytes(store.documentPath(), invalid));
        ProfileDocument valid;
        valid.globalLighting.mode = LightingMode::Wave;
        const SaveOutcome refused = store.save(valid);
        QVERIFY(!refused.ok);
        QFile file(store.documentPath());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), invalid);

        const QByteArray fallback = R"({
            "schema_version": 1,
            "device": {"vendor_id": "046d", "product_id": "c336", "model": "logitech-g213-prodigy"},
            "global": {"lighting": {"mode": "not-a-mode", "base_color": "#112233"}}
        })";
        QVERIFY(writeBytes(store.documentPath(), fallback));
        const LoadOutcome loaded = store.load();
        QVERIFY(loaded.ok);
        QVERIFY(loaded.migrationFallback);
        const SaveOutcome refusedFallback = store.save(loaded.document);
        QVERIFY(!refusedFallback.ok);
        QFile again(store.documentPath());
        QVERIFY(again.open(QIODevice::ReadOnly));
        QCOMPARE(again.readAll(), fallback);
    }

    void schema3InvalidColorsTypesZonesAndSpeedBounds()
    {
        const LoadOutcome badHex = ProfileStore::parseDocument(
            schema3LightingDocument(schema3FiveStaticZones(QByteArrayLiteral("{\"role\": \"static\", \"color\": \"#gg0000\"}"))));
        QVERIFY(!badHex.ok);
        QVERIFY(badHex.error.reason.contains(QStringLiteral("color")));

        const LoadOutcome shortHex = ProfileStore::parseDocument(
            schema3LightingDocument(schema3FiveStaticZones(QByteArrayLiteral("{\"role\": \"static\", \"color\": \"#fff\"}"))));
        QVERIFY(!shortHex.ok);
        QVERIFY(shortHex.error.reason.contains(QStringLiteral("color")));

        const LoadOutcome numericColor = ProfileStore::parseDocument(
            schema3LightingDocument(schema3FiveStaticZones(QByteArrayLiteral("{\"role\": \"static\", \"color\": 123}"))));
        QVERIFY(!numericColor.ok);
        QVERIFY(numericColor.error.reason.contains(QStringLiteral("color")));

        const LoadOutcome boolColor = ProfileStore::parseDocument(
            schema3LightingDocument(schema3FiveStaticZones(QByteArrayLiteral("{\"role\": \"static\", \"color\": true}"))));
        QVERIFY(!boolColor.ok);
        QVERIFY(boolColor.error.reason.contains(QStringLiteral("color")));

        const LoadOutcome nonObjectZones = ProfileStore::parseDocument(schema3LightingDocument(QByteArrayLiteral(
            "{\"mode\": \"direct\", \"base_color\": \"#112233\", \"zones\": [\"#111111\", \"#222222\", \"#333333\", \"#444444\", \"#555555\"]}")));
        QVERIFY(!nonObjectZones.ok);
        QVERIFY(nonObjectZones.error.reason.contains(QStringLiteral("objects")));

        const LoadOutcome nullZone =
            ProfileStore::parseDocument(schema3LightingDocument(schema3FiveStaticZones(QByteArrayLiteral("null"))));
        QVERIFY(!nullZone.ok);
        QVERIFY(nullZone.error.reason.contains(QStringLiteral("objects")));

        const LoadOutcome staticMissingColor = ProfileStore::parseDocument(
            schema3LightingDocument(schema3FiveStaticZones(QByteArrayLiteral("{\"role\": \"static\"}"))));
        QVERIFY(!staticMissingColor.ok);
        QVERIFY(staticMissingColor.error.reason.contains(QStringLiteral("color")));

        const LoadOutcome extraZoneField = ProfileStore::parseDocument(schema3LightingDocument(
            schema3FiveStaticZones(QByteArrayLiteral("{\"role\": \"static\", \"color\": \"#112233\", \"intensity\": 1}"))));
        QVERIFY(!extraZoneField.ok);

        const LoadOutcome offWithColor = ProfileStore::parseDocument(schema3LightingDocument(QByteArrayLiteral(
            "{\"mode\": \"direct\", \"base_color\": \"#112233\", \"zones\": [{\"role\": \"off\", \"color\": \"#112233\"}, {\"role\": \"static\", \"color\": \"#222222\"}, {\"role\": \"static\", \"color\": \"#333333\"}, {\"role\": \"static\", \"color\": \"#444444\"}, {\"role\": \"static\", \"color\": \"#555555\"}]}")));
        QVERIFY(!offWithColor.ok);
        QVERIFY(offWithColor.error.reason.contains(QStringLiteral("off"))
                || offWithColor.error.reason.contains(QStringLiteral("unknown"))
                || offWithColor.error.reason.contains(QStringLiteral("color")));

        const LoadOutcome negativeSpeed = ProfileStore::parseDocument(
            schema3LightingDocument(QByteArrayLiteral("{\"mode\": \"cycle\", \"zones\": null, \"speed\": -1}")));
        QVERIFY(!negativeSpeed.ok);
        QVERIFY(negativeSpeed.error.reason.contains(QStringLiteral("speed")));

        const LoadOutcome fractionalSpeed = ProfileStore::parseDocument(
            schema3LightingDocument(QByteArrayLiteral("{\"mode\": \"cycle\", \"zones\": null, \"speed\": 1.5}")));
        QVERIFY(!fractionalSpeed.ok);
        QVERIFY(fractionalSpeed.error.reason.contains(QStringLiteral("speed")));

        const LoadOutcome stringSpeed = ProfileStore::parseDocument(
            schema3LightingDocument(QByteArrayLiteral("{\"mode\": \"cycle\", \"zones\": null, \"speed\": \"fast\"}")));
        QVERIFY(!stringSpeed.ok);
        QVERIFY(stringSpeed.error.reason.contains(QStringLiteral("speed")));

        const LoadOutcome overBoundSpeed = ProfileStore::parseDocument(
            schema3LightingDocument(QByteArrayLiteral("{\"mode\": \"cycle\", \"zones\": null, \"speed\": 2147483648}")));
        QVERIFY(!overBoundSpeed.ok);
        QVERIFY(overBoundSpeed.error.reason.contains(QStringLiteral("speed")));
    }

    void schema2SuccessFormsPreserveFieldsAndReadBytes()
    {
        const QByteArray directAbsentZones = schema2Document(QByteArrayLiteral(
            ", \"global\": {\"keys\": {\"F1\": {\"action\": \"disabled\"}}, \"lighting\": {\"mode\": \"direct\", \"restore_mode\": \"wave\", \"base_color\": \"#aabbcc\"}}, \"applications\": [{\"id\": \"first\", \"display_name\": \"First\", \"match\": {\"resource_class\": \"Foo\"}, \"keys\": {\"F2\": {\"action\": \"pass_through\"}}}, {\"id\": \"second\", \"display_name\": \"Second\", \"match\": {\"desktop_file_name\": \"bar.desktop\"}}], \"preferences\": {\"automatic_enabled\": false, \"tray_notifications\": true}}"));
        const LoadOutcome direct = ProfileStore::parseDocument(directAbsentZones);
        QVERIFY2(direct.ok, qPrintable(direct.error.reason));
        QCOMPARE(direct.document.globalLighting.mode, LightingMode::Direct);
        QVERIFY(direct.document.globalLighting.baseColor.has_value());
        QCOMPARE(direct.document.globalLighting.baseColor->r, quint8(0xaa));
        QVERIFY(!direct.document.globalLighting.zones.has_value());
        QCOMPARE(direct.document.globalLighting.restoreMode, LightingMode::Wave);
        QCOMPARE(direct.document.globalKeys.value(ControlId::F1).action, ActionType::Disabled);
        QCOMPARE(direct.document.applications.size(), 2);
        QCOMPARE(direct.document.applications.at(0).id, QStringLiteral("first"));
        QCOMPARE(direct.document.applications.at(1).id, QStringLiteral("second"));
        QCOMPARE(direct.document.applications.at(0).match.resourceClass.value(), QStringLiteral("Foo"));
        QCOMPARE(direct.document.applications.at(1).match.desktopFileName.value(), QStringLiteral("bar.desktop"));
        QCOMPARE(direct.document.applications.at(0).keys.value(ControlId::F2).action, ActionType::PassThrough);
        QCOMPARE(direct.document.preferences.automaticEnabled, false);
        QCOMPARE(direct.document.preferences.trayNotifications, true);

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ProfileStore store(dir.path());
        QVERIFY(writeBytes(store.documentPath(), directAbsentZones));
        const LoadOutcome loadedDirect = store.load();
        QVERIFY(loadedDirect.ok);
        QFile directFile(store.documentPath());
        QVERIFY(directFile.open(QIODevice::ReadOnly));
        QCOMPARE(directFile.readAll(), directAbsentZones);

        const QByteArray cycle = schema2Document(QByteArrayLiteral(
            ", \"global\": {\"keys\": {\"F3\": {\"action\": \"pass_through\"}}, \"lighting\": {\"mode\": \"cycle\", \"restore_mode\": \"breathing\", \"base_color\": \"#010203\", \"zones\": null, \"speed\": 40}}, \"applications\": [{\"id\": \"alpha\", \"display_name\": \"Alpha\", \"match\": {\"resource_name\": \"alpha\"}}, {\"id\": \"beta\", \"display_name\": \"Beta\", \"match\": {\"resource_class\": \"Beta\"}}], \"preferences\": {\"automatic_enabled\": true, \"tray_notifications\": false}}"));
        QVERIFY(writeBytes(store.documentPath(), cycle));
        const LoadOutcome loadedCycle = store.load();
        QVERIFY2(loadedCycle.ok, qPrintable(loadedCycle.error.reason));
        QCOMPARE(loadedCycle.document.globalLighting.mode, LightingMode::Cycle);
        QCOMPARE(loadedCycle.document.globalLighting.restoreMode, LightingMode::Breathing);
        QCOMPARE(loadedCycle.document.globalLighting.baseColor->b, quint8(0x03));
        QVERIFY(!loadedCycle.document.globalLighting.zones.has_value());
        QCOMPARE(*loadedCycle.document.globalLighting.speed, quint32(40));
        QCOMPARE(loadedCycle.document.globalKeys.value(ControlId::F3).action, ActionType::PassThrough);
        QCOMPARE(loadedCycle.document.applications.at(0).id, QStringLiteral("alpha"));
        QCOMPARE(loadedCycle.document.applications.at(1).id, QStringLiteral("beta"));
        QCOMPARE(loadedCycle.document.applications.at(0).match.resourceName.value(), QStringLiteral("alpha"));
        QCOMPARE(loadedCycle.document.preferences.automaticEnabled, true);
        QFile cycleFile(store.documentPath());
        QVERIFY(cycleFile.open(QIODevice::ReadOnly));
        QCOMPARE(cycleFile.readAll(), cycle);

        const QByteArray off = schema2Document(QByteArrayLiteral(
            ", \"global\": {\"keys\": {\"F4\": {\"action\": \"disabled\"}}, \"lighting\": {\"mode\": \"off\", \"restore_mode\": \"wave\", \"zones\": null}}, \"applications\": [{\"id\": \"only\", \"display_name\": \"Only\", \"match\": {\"resource_class\": \"Only\"}}], \"preferences\": {\"automatic_enabled\": false, \"tray_notifications\": true}}"));
        QVERIFY(writeBytes(store.documentPath(), off));
        const LoadOutcome loadedOff = store.load();
        QVERIFY2(loadedOff.ok, qPrintable(loadedOff.error.reason));
        QCOMPARE(loadedOff.document.globalLighting.mode, LightingMode::Off);
        QCOMPARE(loadedOff.document.globalLighting.restoreMode, LightingMode::Wave);
        QVERIFY(!loadedOff.document.globalLighting.zones.has_value());
        QCOMPARE(loadedOff.document.globalKeys.value(ControlId::F4).action, ActionType::Disabled);
        QCOMPARE(loadedOff.document.applications.at(0).id, QStringLiteral("only"));
        QCOMPARE(loadedOff.document.preferences.trayNotifications, true);
        QFile offFile(store.documentPath());
        QVERIFY(offFile.open(QIODevice::ReadOnly));
        QCOMPARE(offFile.readAll(), off);

        const QByteArray schema2Zones = schema2Document(QByteArrayLiteral(
            ", \"global\": {\"lighting\": {\"mode\": \"direct\", \"base_color\": \"#112233\", \"zones\": [\"#111111\", \"#222222\", \"#333333\", \"#444444\", \"#555555\"]}}}"));
        QVERIFY(writeBytes(store.documentPath(), schema2Zones));
        const LoadOutcome loadedZones = store.load();
        QVERIFY(loadedZones.ok);
        QCOMPARE((*loadedZones.document.globalLighting.zones)[0].role, ZoneRole::Static);
        QCOMPARE((*loadedZones.document.globalLighting.zones)[4].role, ZoneRole::Static);
        QVERIFY(!workspaceLayoutIsActive(loadedZones.document.globalLighting));
        QFile zonesFile(store.documentPath());
        QVERIFY(zonesFile.open(QIODevice::ReadOnly));
        QCOMPARE(zonesFile.readAll(), schema2Zones);
    }
};

QTEST_MAIN(TestProfilePersistence)
#include "test_profile_persistence.moc"
