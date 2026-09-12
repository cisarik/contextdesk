#pragma once

#include "broker/ISink.h"
#include "broker/KeyLedger.h"
#include "broker/Logger.h"
#include "broker/Types.h"

#include <string>
#include <vector>

namespace contextdeck::broker {

class ILifecycleSink {
public:
    virtual ~ILifecycleSink() = default;
    virtual bool createVirtual() = 0;
    virtual void destroyVirtual() = 0;
    virtual bool applyMeasuredCapabilities(const SinkCapabilities &) { return true; }
    virtual bool prepareVirtual() { return true; }
    virtual int feedbackFd() const { return -1; }
};

class ILifecycleSource {
public:
    virtual ~ILifecycleSource() = default;
    virtual SourceTag tag() const = 0;
    // claimSource is the exclusive grab; unclaimSource is the ungrab.
    // Production: EvdevGrabber (libevdev_grab → EVIOCGRAB). Tests: FakeGrabber.
    virtual bool openSource() = 0;
    virtual bool claimSource() = 0;
    virtual void unclaimSource() = 0;
    virtual void closeSource() = 0;
    virtual SinkCapabilities measuredCapabilities() const { return {}; }
};

class Acquisition {
public:
    Acquisition(ILifecycleSink &sink, ILifecycleSource &if00, ILifecycleSource &if01, KeyLedger &ledger, Logger &logger,
                ISink *events = nullptr);

    bool arm();
    void disarm();
    bool armed() const { return armed_; }
    int sinkFeedbackFd() const { return sink_.feedbackFd(); }
    const std::vector<std::string> &history() const { return history_; }

private:
    void rollbackFrom(bool if00Opened, bool if00Claimed, bool if01Opened, bool if01Claimed, bool virtualCreated);
    void releaseSourcesUngrabFirst();
    void emitSyntheticDisarm();
    void record(const char *step);

    ILifecycleSink &sink_;
    ILifecycleSource &if00_;
    ILifecycleSource &if01_;
    KeyLedger &ledger_;
    Logger &logger_;
    ISink *events_ = nullptr;
    bool armed_ = false;
    std::vector<std::string> history_;
};

} // namespace contextdeck::broker
