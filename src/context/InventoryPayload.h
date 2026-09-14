#pragma once

#include "context/ContextReceiver.h"

#include <QString>
#include <QVector>

#include <optional>

namespace contextdeck {

[[nodiscard]] std::optional<QVector<InventoryEntry>> parseInventoryPayload(const QString &payloadJson);

} // namespace contextdeck
