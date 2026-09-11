#include "broker/EvdevGrabber.h"

#include <libevdev/libevdev.h>

namespace contextdeck::broker {

EvdevGrabber::EvdevGrabber(::libevdev *dev)
    : dev_(dev)
{
}

std::unique_ptr<EvdevGrabber> EvdevGrabber::create(::libevdev *dev)
{
    if (dev == nullptr) {
        return nullptr;
    }
    if (libevdev_get_fd(dev) < 0) {
        return nullptr;
    }
    return std::unique_ptr<EvdevGrabber>(new EvdevGrabber(dev));
}

EvdevGrabber::~EvdevGrabber()
{
    ungrab();
    dev_ = nullptr;
}

bool EvdevGrabber::grab()
{
    if (dev_ == nullptr || libevdev_get_fd(dev_) < 0) {
        grabbed_ = false;
        return false;
    }
    const int rc = libevdev_grab(dev_, LIBEVDEV_GRAB);
    grabbed_ = (rc == 0);
    return grabbed_;
}

void EvdevGrabber::ungrab()
{
    if (!grabbed_ || dev_ == nullptr) {
        return;
    }
    if (libevdev_get_fd(dev_) >= 0) {
        (void)libevdev_grab(dev_, LIBEVDEV_UNGRAB);
    }
    grabbed_ = false;
}

} // namespace contextdeck::broker
