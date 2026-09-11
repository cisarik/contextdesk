#include "broker/IpcProtocol.h"

#include <charconv>
#include <cstddef>

namespace contextdeck::broker {
namespace {

bool isAsciiToken(std::string_view text)
{
    if (text.empty()) {
        return false;
    }
    for (char c : text) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (uc < 0x21 || uc > 0x7e) {
            return false;
        }
    }
    return true;
}

std::optional<uint32_t> parseDigits(std::string_view text)
{
    if (text.empty() || text.size() > 10) {
        return std::nullopt;
    }
    uint32_t value = 0;
    const auto *first = text.data();
    const auto *last = first + text.size();
    const auto parsed = std::from_chars(first, last, value, 10);
    if (parsed.ec != std::errc() || parsed.ptr != last) {
        return std::nullopt;
    }
    return value;
}

IpcVerb verbFromToken(std::string_view token)
{
    if (token == "STATUS") {
        return IpcVerb::Status;
    }
    if (token == "LEASE") {
        return IpcVerb::Lease;
    }
    if (token == "HEARTBEAT") {
        return IpcVerb::Heartbeat;
    }
    if (token == "ARM") {
        return IpcVerb::Arm;
    }
    if (token == "DISARM") {
        return IpcVerb::Disarm;
    }
    if (token == "RELEASE") {
        return IpcVerb::Release;
    }
    return IpcVerb::Unknown;
}

bool verbAllowsLeaseMs(IpcVerb verb)
{
    return verb == IpcVerb::Lease || verb == IpcVerb::Heartbeat || verb == IpcVerb::Arm;
}

} // namespace

uint32_t clampLeaseMs(uint32_t requestedMs)
{
    if (requestedMs < kMinLeaseMs) {
        return kMinLeaseMs;
    }
    if (requestedMs > kMaxLeaseMs) {
        return kMaxLeaseMs;
    }
    return requestedMs;
}

IpcRequest parseIpcPayload(std::string_view payload)
{
    IpcRequest request;
    if (payload.empty() || payload.size() > kIpcMaxPayload) {
        return request;
    }

    const auto space = payload.find(' ');
    const std::string_view token = space == std::string_view::npos ? payload : payload.substr(0, space);
    if (!isAsciiToken(token)) {
        return request;
    }

    request.verb = verbFromToken(token);
    if (space == std::string_view::npos) {
        request.wellFormed = true;
        return request;
    }

    const std::string_view rest = payload.substr(space + 1);
    if (rest.find(' ') != std::string_view::npos || !isAsciiToken(rest) || !verbAllowsLeaseMs(request.verb)
        || request.verb == IpcVerb::Unknown) {
        request.wellFormed = false;
        request.verb = IpcVerb::Unknown;
        return request;
    }

    const std::optional<uint32_t> ms = parseDigits(rest);
    if (!ms.has_value()) {
        request.wellFormed = false;
        request.verb = IpcVerb::Unknown;
        return request;
    }

    request.leaseMs = clampLeaseMs(*ms);
    request.wellFormed = true;
    return request;
}

bool encodeIpcFrame(std::string_view payload, std::vector<uint8_t> &out)
{
    if (payload.empty() || payload.size() > kIpcMaxPayload) {
        return false;
    }
    const auto length = static_cast<uint16_t>(payload.size());
    out.resize(static_cast<std::size_t>(2) + payload.size());
    out[0] = static_cast<uint8_t>(length & 0xffu);
    out[1] = static_cast<uint8_t>((length >> 8) & 0xffu);
    for (std::size_t i = 0; i < payload.size(); ++i) {
        out[i + 2] = static_cast<uint8_t>(payload[i]);
    }
    return true;
}

FrameStatus decodeIpcFrame(std::vector<uint8_t> &buffer, std::string &payload)
{
    payload.clear();
    if (buffer.size() < 2) {
        return FrameStatus::NeedMore;
    }
    const uint16_t length = static_cast<uint16_t>(buffer[0] | (static_cast<uint16_t>(buffer[1]) << 8));
    if (length == 0 || length > kIpcMaxPayload) {
        return FrameStatus::Malformed;
    }
    const std::size_t total = static_cast<std::size_t>(2) + length;
    if (buffer.size() < total) {
        return FrameStatus::NeedMore;
    }
    payload.assign(reinterpret_cast<const char *>(buffer.data() + 2), length);
    buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(total));
    return FrameStatus::Ok;
}

std::string ipcOkStatus(std::string_view lease, int armed, uint32_t ttlMs)
{
    std::string text("OK STATUS lease=");
    text.append(lease);
    text.append(" armed=");
    text.append(armed != 0 ? "1" : "0");
    text.append(" ttl=");
    text.append(std::to_string(ttlMs));
    return text;
}

std::string ipcOkLease(uint32_t ttlMs)
{
    std::string text("OK LEASE ttl=");
    text.append(std::to_string(ttlMs));
    return text;
}

} // namespace contextdeck::broker
