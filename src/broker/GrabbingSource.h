#pragma once

#include "broker/Acquisition.h"
#include "broker/IGrabber.h"

namespace contextdeck::broker {

// ILifecycleSource whose claimSource/unclaimSource are the exclusive grab.
// Production supplies EvdevSource (open + EvdevGrabber). Tests supply FakeGrabber.
class GrabbingSource final : public ILifecycleSource {
public:
    GrabbingSource(SourceTag tag, IGrabber &grabber);

    SourceTag tag() const override { return tag_; }
    bool openSource() override;
    bool claimSource() override;
    void unclaimSource() override;
    void closeSource() override;

    bool openOk = true;
    bool opened() const { return opened_; }

private:
    SourceTag tag_;
    IGrabber &grabber_;
    bool opened_ = false;
};

} // namespace contextdeck::broker
