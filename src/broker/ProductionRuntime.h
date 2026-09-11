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

namespace contextdeck::broker {

class RealLifecycleSink final : public ILifecycleSink, public ISink {
public:
    explicit RealLifecycleSink(SinkCapabilities capabilities);
    ~RealLifecycleSink() override;

    RealLifecycleSink(const RealLifecycleSink &) = delete;
    RealLifecycleSink &operator=(const RealLifecycleSink &) = delete;

    bool createVirtual() override;
    void destroyVirtual() override;
    void writeEvent(uint16_t type, uint16_t code, int32_t value) override;
    void flushSyn() override;
    bool created() const { return impl_ != nullptr; }

private:
    SinkCapabilities capabilities_;
    std::unique_ptr<RealSink> impl_;
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
                   Logger &logger);

    void afterWait() override;

private:
    SessionIpc &ipc_;
    ForwardingEngine &engine_;
    EvdevSource &if00_;
    EvdevSource &if01_;
    IArmControl &arm_;
    Logger &logger_;
};

} // namespace contextdeck::broker
