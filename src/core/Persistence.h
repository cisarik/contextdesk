#pragma once

#include "core/Types.h"

class QJsonObject;

namespace contextdeck {

class ProfileStore
{
public:
    explicit ProfileStore(QString configRoot = {});

    [[nodiscard]] QString configRoot() const;
    [[nodiscard]] QString documentPath() const;
    [[nodiscard]] QString backupPath() const;

    [[nodiscard]] LoadOutcome load() const;
    [[nodiscard]] SaveOutcome save(const ProfileDocument &document) const;

    [[nodiscard]] static LoadOutcome parseDocument(const QByteArray &bytes, const QString &sourcePath = {});
    [[nodiscard]] static PersistenceError validate(const ProfileDocument &document);
    [[nodiscard]] static QJsonObject toJson(const ProfileDocument &document);
    [[nodiscard]] static QByteArray toJsonBytes(const ProfileDocument &document);

private:
    QString m_configRoot;
};

} // namespace contextdeck
