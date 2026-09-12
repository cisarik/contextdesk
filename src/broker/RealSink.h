#pragma once

#include "broker/ISink.h"
#include "broker/Types.h"

#include <memory>
#include <vector>

struct libevdev;
struct libevdev_uinput;

namespace contextdeck::broker {

SinkCapabilities unionSourceCapabilities(const SinkCapabilities &if00, const SinkCapabilities &if01);

// Catalog of host-remappable G213 bits plus ordinary keyboard codes. Production
// ARM measures the live pair instead of using this set. EV_REP is never enabled.
SinkCapabilities passthroughCapabilities();

// Wraps libevdev_uinput. create() opens /dev/uinput; unit tests must not call it.
class RealSink final : public ISink {
public:
    static std::unique_ptr<RealSink> create(const SinkCapabilities &capabilities);
    ~RealSink() override;

    RealSink(const RealSink &) = delete;
    RealSink &operator=(const RealSink &) = delete;

    bool writeEvent(uint16_t type, uint16_t code, int32_t value) override;
    bool flushSyn() override;

    int fd() const;
    bool makeNonBlocking();
    std::vector<RecordedEvent> drainLed();

private:
    RealSink(::libevdev *templateDevice, ::libevdev_uinput *uinputDevice);

    ::libevdev *templateDevice_ = nullptr;
    ::libevdev_uinput *uinputDevice_ = nullptr;
};

} // namespace contextdeck::broker
