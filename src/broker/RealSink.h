#pragma once

#include "broker/ISink.h"

#include <memory>
#include <vector>

struct libevdev;
struct libevdev_uinput;

namespace contextdeck::broker {

struct SinkCapabilities {
    std::vector<uint16_t> keyCodes;
    std::vector<uint16_t> ledCodes;
    std::vector<uint16_t> mscCodes;
};

SinkCapabilities unionSourceCapabilities(const SinkCapabilities &if00, const SinkCapabilities &if01);

// Wraps libevdev_uinput. Construction opens /dev/uinput and is forbidden in this exchange.
class RealSink final : public ISink {
public:
    static std::unique_ptr<RealSink> create(const SinkCapabilities &capabilities);
    ~RealSink() override;

    RealSink(const RealSink &) = delete;
    RealSink &operator=(const RealSink &) = delete;

    void writeEvent(uint16_t type, uint16_t code, int32_t value) override;
    void flushSyn() override;

private:
    RealSink(::libevdev *templateDevice, ::libevdev_uinput *uinputDevice);

    ::libevdev *templateDevice_ = nullptr;
    ::libevdev_uinput *uinputDevice_ = nullptr;
};

} // namespace contextdeck::broker
