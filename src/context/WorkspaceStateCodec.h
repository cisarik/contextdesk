#pragma once

#include "context/WorkspaceReceiver.h"

#include <QDBusArgument>
#include <QString>
#include <QVariant>
#include <QVariantMap>

#include <optional>

namespace contextdeck {

inline constexpr qsizetype kMaxDesktopIdBytes = 128;
inline constexpr qsizetype kMaxDesktopNameBytes = 256;
inline constexpr qsizetype kMaxMetadataBytes = 16 * 1024;
inline constexpr int kMinDesktopCount = 1;
inline constexpr int kMaxDesktopCount = 32;
inline constexpr int kMaxPosition = 32;

bool hasControlCharacters(const QString &text);
bool boundedUtf8(const QString &text, qsizetype maxBytes);
bool decodePosition(const QDBusArgument &arg, int &position);
bool decodeDesktopStructure(const QDBusArgument &arg, int &position, QString &id, QString &name);
QVariant unwrapDbusVariant(QVariant value);
bool decodeGetAllProperties(const QVariant &first, QVariantMap &properties);
bool decodeCount(const QVariant &value, int &count);

[[nodiscard]] std::optional<WorkspaceState> decodeWorkspaceSnapshot(const QVariantMap &properties,
                                                                    QString &errorClass);

} // namespace contextdeck
