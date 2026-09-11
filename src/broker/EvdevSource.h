#pragma once

#include "broker/Acquisition.h"
#include "broker/EvdevGrabber.h"
#include "broker/ISource.h"
#include "broker/IdentityMatcher.h"

#include <memory>
#include <set>
#include <string>

struct libevdev;

namespace contextdeck::broker {

// Production ILifecycleSource + ISource. Path is bound from the enumerator and
// is not opened until openSource() (ARM). claimSource uses EvdevGrabber.
class EvdevSource final : public ILifecycleSource, public ISource {
public:
    explicit EvdevSource(SourceTag tag);
    ~EvdevSource() override;

    EvdevSource(const EvdevSource &) = delete;
    EvdevSource &operator=(const EvdevSource &) = delete;

    void bind(std::string devnode, DeviceCandidate identity);

    SourceTag tag() const override { return tag_; }
    bool openSource() override;
    bool claimSource() override;
    void unclaimSource() override;
    void closeSource() override;

    std::optional<InputEvent> read() override;
    std::set<uint16_t> keysDown() const override;

    bool opened() const { return opened_; }
    int fd() const { return fd_; }
    bool hadError() const { return failed_; }
    const std::string &path() const { return path_; }

private:
    void releaseOpened();
    bool matchesBoundIdentity() const;

    SourceTag tag_;
    std::string path_;
    DeviceCandidate identity_;
    int fd_ = -1;
    ::libevdev *dev_ = nullptr;
    std::unique_ptr<EvdevGrabber> grabber_;
    bool opened_ = false;
    bool failed_ = false;
};

} // namespace contextdeck::broker
