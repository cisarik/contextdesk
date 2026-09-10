#pragma once

#include <QLatin1String>

namespace contextdeck {

inline constexpr QLatin1String kServiceName{"io.github.cisarik.ContextDeck"};
inline constexpr QLatin1String kContextObjectPath{"/io/github/cisarik/ContextDeck/Context1"};
inline constexpr QLatin1String kContextInterface{"io.github.cisarik.ContextDeck.Context1"};

inline constexpr int kHeartbeatIntervalMs = 5000;
inline constexpr int kMissedHeartbeatsForLoss = 3;
inline constexpr int kIdentityDebounceMs = 250;
inline constexpr int kMaxInventoryEntries = 200;
inline constexpr qsizetype kMaxInventoryBytes = 64 * 1024;
inline constexpr qsizetype kMaxDbusStringBytes = 256;
inline constexpr qsizetype kMaxBridgeIdBytes = 128;

} // namespace contextdeck
