#pragma once

#include "core/Types.h"

#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace contextdeck {

struct WorkspaceCheckpointDesktop {
    int ordinal = 0;
    QString id;
    QString name;
};

struct WorkspaceCheckpointData {
    QString currentId;
    std::optional<int> rows;
    std::optional<bool> wrapping;
    QVector<WorkspaceCheckpointDesktop> desktops;
    QStringList createdIds;
};

class WorkspaceCheckpoint
{
public:
    explicit WorkspaceCheckpoint(QString path = {});

    [[nodiscard]] QString path() const { return m_path; }
    [[nodiscard]] bool exists() const;

    [[nodiscard]] bool save(const WorkspaceCheckpointData &data);
    [[nodiscard]] std::optional<WorkspaceCheckpointData> load() const;
    void remove();

private:
    QString m_path;
};

[[nodiscard]] WorkspaceCheckpointData checkpointFromState(const WorkspaceState &state);

} // namespace contextdeck
