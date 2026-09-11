#pragma once

#include "broker/IGrabber.h"

#include <string>
#include <vector>

namespace contextdeck::broker {

class FakeGrabber final : public IGrabber {
public:
    FakeGrabber(std::string id, std::vector<std::string> &log, bool succeed = true);

    bool grab() override;
    void ungrab() override;

    bool grabbed() const { return grabbed_; }
    void setSucceed(bool succeed) { succeed_ = succeed; }

private:
    std::string id_;
    std::vector<std::string> &log_;
    bool succeed_ = true;
    bool grabbed_ = false;
};

} // namespace contextdeck::broker
