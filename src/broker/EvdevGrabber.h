#pragma once

#include "broker/IGrabber.h"

#include <memory>

struct libevdev;

namespace contextdeck::broker {

// Exclusive claim via libevdev_grab(dev, LIBEVDEV_GRAB), which issues
// EVIOCGRAB. create() refuses a null handle or a handle with no fd, so
// unit tests never reach the ioctl. RealSink::create stays unused here too.
class EvdevGrabber final : public IGrabber {
public:
    static std::unique_ptr<EvdevGrabber> create(::libevdev *dev);
    ~EvdevGrabber() override;

    EvdevGrabber(const EvdevGrabber &) = delete;
    EvdevGrabber &operator=(const EvdevGrabber &) = delete;

    bool grab() override;
    void ungrab() override;

private:
    explicit EvdevGrabber(::libevdev *dev);

    ::libevdev *dev_ = nullptr;
    bool grabbed_ = false;
};

} // namespace contextdeck::broker
