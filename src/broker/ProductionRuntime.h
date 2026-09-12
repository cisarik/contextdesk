#pragma once

#include "broker/Acquisition.h"
#include "broker/DeviceEnumerator.h"
#include "broker/EventLoop.h"
#include "broker/EvdevSource.h"
#include "broker/ForwardingEngine.h"
#include "broker/IdleWait.h"
#include "broker/RealSink.h"
#include "broker/SessionIpc.h"

#include <memory>
#include <vector>

namespace contextdeck::broker {

class ILedTarget {
public:
    virtual ~ILedTarget() = default;
    virtual bool writeLed(uint16_t code, int32_t value) = 0;
};

// Forwards compositor LED events to physical if00 only. Runtime failure is
// logged as led-write-failed and does not disarm. Returns false if any write failed.
bool applyLedFeedback(const std::vector<RecordedEvent> &events, ILedTarget &if00, Logger &logger);

class RealLifecycleSink final : public ILifecycleSink, public ISink {
public:
    explicit RealLifecycleSink(SinkCapabilities capabilities = {});
    ~RealLifecycleSink() override;

    RealLifecycleSink(const RealLifecycleSink &) = delete;
    RealLifecycleSink &operator=(const RealLifecycleSink &) = delete;

    void setWait(SignalEpollWait *wait) { wait_ = wait; }

    bool createVirtual() override;
    void destroyVirtual() override;
    bool applyMeasuredCapabilities(const SinkCapabilities &capabilities) override;
    bool prepareVirtual() override;
    int feedbackFd() const override;
    bool writeEvent(uint16_t type, uint16_t code, int32_t value) override;
    bool flushSyn() override;
    bool created() const { return impl_ != nullptr; }
    std::vector<RecordedEvent> drainLed();
    const SinkCapabilities &capabilities() const { return capabilities_; }

private:
    void unwatchFeedback();

    SinkCapabilities capabilities_;
    std::unique_ptr<RealSink> impl_;
    SignalEpollWait *wait_ = nullptr;
    int watchedFd_ = -1;
};

class ProductionArmControl final : public IArmControl {
public:
    ProductionArmControl(IDeviceEnumerator &enumerator, EvdevSource &if00, EvdevSource &if01, Acquisition &acquisition,
                         Logger &logger, SignalEpollWait *wait = nullptr);

    bool arm() override;
    void disarm() override;
    bool armed() const override;

private:
    void unwatchSources();

    IDeviceEnumerator &enumerator_;
    EvdevSource &if00_;
    EvdevSource &if01_;
    Acquisition &acquisition_;
    Logger &logger_;
    SignalEpollWait *wait_ = nullptr;
};

class BrokerLoopWork final : public ILoopWork {
public:
    BrokerLoopWork(SessionIpc &ipc, ForwardingEngine &engine, EvdevSource &if00, EvdevSource &if01, IArmControl &arm,
                   Logger &logger, RealLifecycleSink *leds = nullptr);

    void afterWait() override;

private:
    SessionIpc &ipc_;
    ForwardingEngine &engine_;
    EvdevSource &if00_;
    EvdevSource &if01_;
    IArmControl &arm_;
    Logger &logger_;
    RealLifecycleSink *leds_ = nullptr;
};

} // namespace contextdeck::broker
