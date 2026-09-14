#pragma once

#include "core/Types.h"

#include <QJsonObject>
#include <optional>

namespace contextdeck::persistence {

std::optional<WorkspaceAssignment> parseWorkspaceAssignment(const QJsonObject &object, const QString &path,
                                                            PersistenceError &error);
std::optional<WorkspaceSession> parseWorkspaceSession(const QJsonObject &object, const QString &path,
                                                      PersistenceError &error);
QJsonObject workspaceAssignmentToJson(const WorkspaceAssignment &workspace);
QJsonObject workspaceSessionToJson(const WorkspaceSession &session);

} // namespace contextdeck::persistence
