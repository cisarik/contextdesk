#include "core/Persistence.h"

#include "core/persistence/JsonCommon.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

namespace contextdeck {
using persistence::makeError;

ProfileStore::ProfileStore(QString configRoot)
    : m_configRoot(std::move(configRoot))
{
}

QString ProfileStore::configRoot() const
{
    if (!m_configRoot.isEmpty()) {
        return m_configRoot;
    }
    const QString xdg = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (!xdg.isEmpty()) {
        return xdg;
    }
    return QDir::homePath() + QStringLiteral("/.config");
}

QString ProfileStore::documentPath() const
{
    return configRoot() + QStringLiteral("/contextdeck/profiles.json");
}

QString ProfileStore::backupPath() const
{
    return documentPath() + QStringLiteral(".bak");
}

QByteArray ProfileStore::toJsonBytes(const ProfileDocument &document)
{
    return QJsonDocument(toJson(document)).toJson(QJsonDocument::Indented);
}

LoadOutcome ProfileStore::load() const
{
    LoadOutcome outcome;
    const QString path = documentPath();
    QFile file(path);
    if (!file.exists()) {
        outcome.ok = true;
        outcome.missing = true;
        return outcome;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        outcome.error = makeError(QStringLiteral("unable to read configuration"), path);
        return outcome;
    }
    const QByteArray bytes = file.readAll();
    return parseDocument(bytes, path);
}

SaveOutcome ProfileStore::save(const ProfileDocument &document) const
{
    SaveOutcome outcome;
    const PersistenceError validation = validate(document);
    if (!validation.reason.isEmpty()) {
        outcome.error = validation;
        return outcome;
    }

    const LoadOutcome parsed = parseDocument(toJsonBytes(document), QStringLiteral("$"));
    if (!parsed.ok) {
        outcome.error = parsed.error;
        return outcome;
    }

    const QString path = documentPath();
    QDir dir = QFileInfo(path).dir();
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        outcome.error = makeError(QStringLiteral("unable to create configuration directory"), path);
        return outcome;
    }

    const QByteArray bytes = toJsonBytes(document);
    if (bytes.size() > kMaxDocumentBytes) {
        outcome.error = makeError(QStringLiteral("document exceeds 1 MiB bound"), path);
        return outcome;
    }

    auto writeAtomically = [](const QString &target, const QByteArray &payload) -> bool {
        QSaveFile writer(target);
        writer.setDirectWriteFallback(false);
        if (!writer.open(QIODevice::WriteOnly)) {
            return false;
        }
        if (writer.write(payload) != payload.size()) {
            writer.cancelWriting();
            return false;
        }
        return writer.commit();
    };

    QFile existing(path);
    const bool hadExisting = existing.exists();
    if (hadExisting) {
        if (!existing.open(QIODevice::ReadOnly)) {
            outcome.error = makeError(QStringLiteral("unable to read existing configuration; original file preserved"),
                                      path);
            return outcome;
        }
        const QByteArray original = existing.readAll();
        existing.close();
        const LoadOutcome existingParsed = parseDocument(original, path);
        if (!existingParsed.ok || existingParsed.migrationFallback) {
            outcome.error = makeError(QStringLiteral("refusing to overwrite unsupported or invalid existing document"),
                                      path);
            outcome.error.preserved = true;
            return outcome;
        }
        if (!writeAtomically(backupPath(), original)) {
            outcome.error = makeError(QStringLiteral("unable to write backup; original file preserved"), path);
            return outcome;
        }
    }

    if (!writeAtomically(path, bytes)) {
        outcome.error = makeError(QStringLiteral("write failed; original file preserved"), path);
        return outcome;
    }

    outcome.ok = true;
    return outcome;
}

} // namespace contextdeck
