#include "broker/FakeGrabber.h"

namespace contextdeck::broker {

FakeGrabber::FakeGrabber(std::string id, std::vector<std::string> &log, bool succeed)
    : id_(std::move(id))
    , log_(log)
    , succeed_(succeed)
{
}

bool FakeGrabber::grab()
{
    log_.push_back("grab-" + id_);
    if (!succeed_) {
        grabbed_ = false;
        return false;
    }
    grabbed_ = true;
    return true;
}

void FakeGrabber::ungrab()
{
    log_.push_back("ungrab-" + id_);
    grabbed_ = false;
}

} // namespace contextdeck::broker
