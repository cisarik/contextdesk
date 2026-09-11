#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace contextdeck::broker {

inline constexpr std::string_view kVirtualDeviceName = "ContextDeck G213 passthrough";
inline constexpr std::string_view kForbiddenNamePrefix = "ContextDeck";
inline constexpr uint16_t kG213VendorId = 0x046d;
inline constexpr uint16_t kG213ProductId = 0xc336;
inline constexpr uint16_t kVirtualVendorId = 0x0000;
inline constexpr uint16_t kVirtualProductId = 0x0001;
inline constexpr uint16_t kVirtualVersion = 0x0001;

enum class SourceTag { If00, If01 };

inline constexpr std::string_view sourceTagName(SourceTag tag)
{
    return tag == SourceTag::If00 ? "if00" : "if01";
}

inline constexpr int sourceIndex(SourceTag tag)
{
    return tag == SourceTag::If00 ? 0 : 1;
}

struct DeviceCandidate {
    std::optional<std::string> vendorId;
    std::optional<std::string> modelId;
    std::optional<std::string> interfaceNum;
    std::optional<uint16_t> bustype;
    std::optional<std::string> name;
};

enum class IdentityVerdict {
    AcceptIf00,
    AcceptIf01,
    RejectVirtualBus,
    RejectNamePrefix,
    RejectUnresolved,
    RejectIdentityMismatch,
};

struct LedgerEmit {
    uint16_t code = 0;
    int32_t value = 0;
};

struct BrokerCounters {
    uint64_t droppedSync = 0;
    uint64_t keysDownPhysical = 0;
    uint64_t keysDownSynthetic = 0;
};

} // namespace contextdeck::broker
