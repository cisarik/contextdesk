#pragma once

namespace contextdeck::broker {

// Exclusive claim of a physical source. Production uses EvdevGrabber
// (libevdev_grab → EVIOCGRAB). Tests use FakeGrabber. Never call grab()
// against a live device from unit tests.
class IGrabber {
public:
    virtual ~IGrabber() = default;
    virtual bool grab() = 0;
    virtual void ungrab() = 0;
};

} // namespace contextdeck::broker
