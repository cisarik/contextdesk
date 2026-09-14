#include "workspace/WorkspaceCheckpoint.h"

#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>

namespace contextdeck {

namespace {

constexpr int kCheckpointSchemaVersion = 1;
constexpr qsizetype kMaxCheckpointBytes = 64 * 1024;
constexpr qsizetype kMaxCheckpointIdBytes = 128;
constexpr qsizetype kMaxCheckpointNameBytes = 256;

bool boundedText(const QString &text, qsizetype maxBytes, bool allowEmpty)
{
    if (!allowEmpty && text.isEmpty()) {
        return false;
    }
    if (text.toUtf8().size() > maxBytes) {
        return false;
    }
    for (const QChar ch : text) {
        if (ch.category() == QChar::Other_Control) {
            return false;
        }
    }
    return true;
}

QByteArray serialize(const WorkspaceCheckpointData &data)
{
    QJsonObject root;
    root.insert(QStringLiteral("schema_version"), kCheckpointSchemaVersion);
    root.insert(QStringLiteral("current_id"), data.currentId);
    if (data.rows.has_value()) {
        root.insert(QStringLiteral("rows"), *data.rows);
    }
    if (data.wrapping.has_value()) {
        root.insert(QStringLiteral("wrapping"), *data.wrapping);
    }
    QJsonArray desktops;
    for (const WorkspaceCheckpointDesktop &desktop : data.desktops) {
        QJsonObject entry;
        entry.insert(QStringLiteral("ordinal"), desktop.ordinal);
        entry.insert(QStringLiteral("id"), desktop.id);
        entry.insert(QStringLiteral("name"), desktop.name);
        desktops.push_back(entry);
    }
    root.insert(QStringLiteral("desktops"), desktops);
    QJsonArray created;
    for (const QString &id : data.createdIds) {
        created.push_back(id);
    }
    root.insert(QStringLiteral("created_ids"), created);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

std::optional<WorkspaceCheckpointData> parse(const QByteArray &bytes)
{
    if (bytes.isEmpty() || bytes.size() > kMaxCheckpointBytes) {
        return std::nullopt;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return std::nullopt;
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("schema_version")).toInt(-1) != kCheckpointSchemaVersion) {
        return std::nullopt;
    }
    if (!root.value(QStringLiteral("desktops")).isArray() || !root.value(QStringLiteral("created_ids")).isArray()) {
        return std::nullopt;
    }
    WorkspaceCheckpointData data;
    data.currentId = root.value(QStringLiteral("current_id")).toString();
    if (!root.value(QStringLiteral("current_id")).isString()) {
        return std::nullopt;
    }
    if (root.contains(QStringLiteral("rows"))) {
        const QJsonValue rows = root.value(QStringLiteral("rows"));
        if (!rows.isDouble()) {
            return std::nullopt;
        }
        const int value = rows.toInt(-1);
        if (value < 1 || value > kMaxWorkspaceDesktops) {
            return std::nullopt;
        }
        data.rows = value;
    }
    if (root.contains(QStringLiteral("wrapping"))) {
        const QJsonValue wrapping = root.value(QStringLiteral("wrapping"));
        if (!wrapping.isBool()) {
            return std::nullopt;
        }
        data.wrapping = wrapping.toBool();
    }
    const QJsonArray desktops = root.value(QStringLiteral("desktops")).toArray();
    if (desktops.isEmpty() || desktops.size() > kMaxWorkspaceDesktops) {
        return std::nullopt;
    }
    for (const QJsonValue &value : desktops) {
        if (!value.isObject()) {
            return std::nullopt;
        }
        const QJsonObject entry = value.toObject();
        WorkspaceCheckpointDesktop desktop;
        desktop.ordinal = entry.value(QStringLiteral("ordinal")).toInt(0);
        desktop.id = entry.value(QStringLiteral("id")).toString();
        desktop.name = entry.value(QStringLiteral("name")).toString();
        if (desktop.ordinal < 1 || desktop.ordinal > kMaxWorkspaceDesktops) {
            return std::nullopt;
        }
        if (!boundedText(desktop.id, kMaxCheckpointIdBytes, false)) {
            return std::nullopt;
        }
        if (!boundedText(desktop.name, kMaxCheckpointNameBytes, true)) {
            return std::nullopt;
        }
        data.desktops.push_back(desktop);
    }
    const QJsonArray created = root.value(QStringLiteral("created_ids")).toArray();
    if (created.size() > kMaxWorkspaceDesktops) {
        return std::nullopt;
    }
    for (const QJsonValue &value : created) {
        if (!value.isString()) {
            return std::nullopt;
        }
        const QString id = value.toString();
        if (!boundedText(id, kMaxCheckpointIdBytes, false)) {
            return std::nullopt;
        }
        data.createdIds.push_back(id);
    }
    return data;
}

} // namespace

WorkspaceCheckpoint::WorkspaceCheckpoint(QString path)
    : m_path(std::move(path))
{
}

bool WorkspaceCheckpoint::exists() const
{
    if (m_path.isEmpty()) {
        return false;
    }
    const QFileInfo info(m_path);
    return info.exists() && info.isFile();
}

bool WorkspaceCheckpoint::save(const WorkspaceCheckpointData &data)
{
    if (m_path.isEmpty()) {
        return false;
    }
    const QFileInfo info(m_path);
    QDir directory = info.dir();
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        return false;
    }
    QSaveFile file(m_path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    const QByteArray bytes = serialize(data);
    if (file.write(bytes) != bytes.size()) {
        file.cancelWriting();
        return false;
    }
    if (!file.flush()) {
        file.cancelWriting();
        return false;
    }
    if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

std::optional<WorkspaceCheckpointData> WorkspaceCheckpoint::load() const
{
    if (!exists()) {
        return std::nullopt;
    }
    const QFileInfo info(m_path);
    if (info.size() <= 0 || info.size() > kMaxCheckpointBytes) {
        return std::nullopt;
    }
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    const QByteArray bytes = file.readAll();
    file.close();
    return parse(bytes);
}

void WorkspaceCheckpoint::remove()
{
    if (m_path.isEmpty()) {
        return;
    }
    QFile::remove(m_path);
}

WorkspaceCheckpointData checkpointFromState(const WorkspaceState &state)
{
    WorkspaceCheckpointData data;
    data.currentId = state.currentId;
    data.rows = state.rows;
    data.wrapping = state.navigationWrappingAround;
    for (const WorkspaceDesktop &desktop : state.desktops) {
        WorkspaceCheckpointDesktop entry;
        entry.ordinal = desktop.ordinal;
        entry.id = desktop.id;
        entry.name = desktop.displayName;
        data.desktops.push_back(entry);
    }
    return data;
}

} // namespace contextdeck
