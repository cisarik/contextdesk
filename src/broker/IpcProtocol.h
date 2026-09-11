#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace contextdeck::broker {

inline constexpr std::size_t kIpcMaxPayload = 256;
inline constexpr uint32_t kDefaultLeaseMs = 6000;
inline constexpr uint32_t kMinLeaseMs = 1000;
inline constexpr uint32_t kMaxLeaseMs = 30000;
inline constexpr char kDefaultBrokerSocket[] = "/run/contextdeck/broker.sock";

enum class IpcVerb {
    Status,
    Lease,
    Heartbeat,
    Arm,
    Disarm,
    Release,
    Unknown,
};

struct IpcRequest {
    IpcVerb verb = IpcVerb::Unknown;
    std::optional<uint32_t> leaseMs;
    bool wellFormed = false;
};

enum class FrameStatus { NeedMore, Ok, Malformed };

uint32_t clampLeaseMs(uint32_t requestedMs);
IpcRequest parseIpcPayload(std::string_view payload);
bool encodeIpcFrame(std::string_view payload, std::vector<uint8_t> &out);
FrameStatus decodeIpcFrame(std::vector<uint8_t> &buffer, std::string &payload);

std::string ipcOkStatus(std::string_view lease, int armed, uint32_t ttlMs);
std::string ipcOkLease(uint32_t ttlMs);

} // namespace contextdeck::broker
