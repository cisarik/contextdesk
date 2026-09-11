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
};

class Acquisition {
public:
    Acquisition(ILifecycleSink &sink, ILifecycleSource &if00, ILifecycleSource &if01, KeyLedger &ledger, Logger &logger,
                ISink *events = nullptr);

    bool arm();
    void disarm();
    bool armed() const { return armed_; }
    const std::vector<std::string> &history() const { return history_; }

private:
    void rollbackFrom(int openedSources, bool if00Claimed, bool if01Opened, bool if01Claimed);
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
