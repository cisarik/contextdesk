#include "broker/ProductionRuntime.h"

namespace contextdeck::broker {

RealLifecycleSink::RealLifecycleSink(SinkCapabilities capabilities)
    : capabilities_(std::move(capabilities))
{
}

RealLifecycleSink::~RealLifecycleSink()
{
    destroyVirtual();
}

bool RealLifecycleSink::createVirtual()
{
    if (impl_ != nullptr) {
        return true;
    }
    impl_ = RealSink::create(capabilities_);
    return impl_ != nullptr;
}

void RealLifecycleSink::destroyVirtual()
{
    impl_.reset();
}

void RealLifecycleSink::writeEvent(uint16_t type, uint16_t code, int32_t value)
{
    if (impl_ != nullptr) {
        impl_->writeEvent(type, code, value);
    }
}

void RealLifecycleSink::flushSyn()
{
    if (impl_ != nullptr) {
        impl_->flushSyn();
    }
}

ProductionArmControl::ProductionArmControl(IDeviceEnumerator &enumerator, EvdevSource &if00, EvdevSource &if01,
                                           Acquisition &acquisition, Logger &logger, SignalEpollWait *wait)
    : enumerator_(enumerator)
    , if00_(if00)
    , if01_(if01)
    , acquisition_(acquisition)
    , logger_(logger)
    , wait_(wait)
{
}

bool ProductionArmControl::armed() const
{
    return acquisition_.armed();
}

void ProductionArmControl::unwatchSources()
{
    if (wait_ == nullptr) {
        return;
    }
    if (if01_.fd() >= 0) {
        wait_->removeFd(if01_.fd());
    }
    if (if00_.fd() >= 0) {
        wait_->removeFd(if00_.fd());
    }
}

bool ProductionArmControl::arm()
{
    if (acquisition_.armed()) {
        return true;
    }
    G213Selection selection;
    const EnumerateError error = enumerator_.resolve(selection);
    if (error != EnumerateError::Ok) {
        logger_.error(enumerateErrorClass(error));
        return false;
    }
    if00_.bind(selection.if00.devnode, selection.if00.candidate);
    if01_.bind(selection.if01.devnode, selection.if01.candidate);
    if (!acquisition_.arm()) {
        return false;
    }
    if (wait_ != nullptr) {
        if (if00_.fd() >= 0) {
            wait_->addFd(if00_.fd());
        }
        if (if01_.fd() >= 0) {
            wait_->addFd(if01_.fd());
        }
    }
    return true;
}

void ProductionArmControl::disarm()
{
    unwatchSources();
    acquisition_.disarm();
}

BrokerLoopWork::BrokerLoopWork(SessionIpc &ipc, ForwardingEngine &engine, EvdevSource &if00, EvdevSource &if01,
                               IArmControl &arm, Logger &logger)
    : ipc_(ipc)
    , engine_(engine)
    , if00_(if00)
    , if01_(if01)
    , arm_(arm)
    , logger_(logger)
{
}

void BrokerLoopWork::afterWait()
{
    ipc_.afterWait();
    if (!arm_.armed()) {
        return;
    }
    engine_.ingest(if00_);
    engine_.ingest(if01_);
    if (if00_.hadError() || if01_.hadError()) {
        logger_.error("source-read-failed");
        arm_.disarm();
    }
}

} // namespace contextdeck::broker
