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

// Full pass-through capability set for the union virtual device. Enables the
// host-remappable G213 catalog (F-block + media/volume) plus ordinary keyboard
// bits so 1:1 forwarding does not need to open devices before ARM. Game Mode
// and Backlight stay firmware-only: they have no host EV_KEY and are not a
// remap catalog. Construction of RealSink still opens /dev/uinput.
SinkCapabilities passthroughCapabilities();

// Wraps libevdev_uinput. create() opens /dev/uinput; unit tests must not call it.
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
