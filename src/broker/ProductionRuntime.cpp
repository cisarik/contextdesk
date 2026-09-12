#include "broker/ProductionRuntime.h"

#include <linux/input.h>

namespace contextdeck::broker {
namespace {

class EvdevIf00Led final : public ILedTarget {
public:
    explicit EvdevIf00Led(EvdevSource &source)
        : source_(source)
    {
    }

    bool writeLed(uint16_t code, int32_t value) override { return source_.writeLed(code, value); }

private:
    EvdevSource &source_;
};

} // namespace

bool applyLedFeedback(const std::vector<RecordedEvent> &events, ILedTarget &if00, Logger &logger)
{
    bool ok = true;
    for (const RecordedEvent &ev : events) {
        if (ev.type != EV_LED) {
            continue;
        }
        if (!if00.writeLed(ev.code, ev.value)) {
            logger.error("led-write-failed");
            ok = false;
        }
    }
    return ok;
}

RealLifecycleSink::RealLifecycleSink(SinkCapabilities capabilities)
    : capabilities_(std::move(capabilities))
{
}

RealLifecycleSink::~RealLifecycleSink()
{
    destroyVirtual();
}

bool RealLifecycleSink::applyMeasuredCapabilities(const SinkCapabilities &capabilities)
{
    if (capabilities.keyCodes.empty()) {
        return false;
    }
    capabilities_ = capabilities;
    return true;
}

bool RealLifecycleSink::createVirtual()
{
    if (impl_ != nullptr) {
        return true;
    }
    impl_ = RealSink::create(capabilities_);
    return impl_ != nullptr;
}

void RealLifecycleSink::unwatchFeedback()
{
    if (wait_ != nullptr && watchedFd_ >= 0) {
        wait_->removeFd(watchedFd_);
    }
    watchedFd_ = -1;
}

void RealLifecycleSink::destroyVirtual()
{
    unwatchFeedback();
    impl_.reset();
}

bool RealLifecycleSink::prepareVirtual()
{
    if (impl_ == nullptr || impl_->fd() < 0 || !impl_->makeNonBlocking()) {
        return false;
    }
    if (wait_ == nullptr) {
        return true;
    }
    const int fd = impl_->fd();
    if (!wait_->addFd(fd)) {
        return false;
    }
    watchedFd_ = fd;
    return true;
}

int RealLifecycleSink::feedbackFd() const
{
    return impl_ != nullptr ? impl_->fd() : -1;
}

bool RealLifecycleSink::writeEvent(uint16_t type, uint16_t code, int32_t value)
{
    if (impl_ == nullptr) {
        return false;
    }
    return impl_->writeEvent(type, code, value);
}

bool RealLifecycleSink::flushSyn()
{
    if (impl_ == nullptr) {
        return false;
    }
    return impl_->flushSyn();
}

std::vector<RecordedEvent> RealLifecycleSink::drainLed()
{
    if (impl_ == nullptr) {
        return {};
    }
    return impl_->drainLed();
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
                               IArmControl &arm, Logger &logger, RealLifecycleSink *leds)
    : ipc_(ipc)
    , engine_(engine)
    , if00_(if00)
    , if01_(if01)
    , arm_(arm)
    , logger_(logger)
    , leds_(leds)
{
}

void BrokerLoopWork::afterWait()
{
    ipc_.afterWait();
    if (!arm_.armed()) {
        return;
    }
    if (!engine_.ingest(if00_) || !engine_.ingest(if01_)) {
        arm_.disarm();
        return;
    }
    if (if00_.hadError() || if01_.hadError()) {
        logger_.error("source-read-failed");
        arm_.disarm();
        return;
    }
    if (leds_ != nullptr) {
        EvdevIf00Led if00Led(if00_);
        (void)applyLedFeedback(leds_->drainLed(), if00Led, logger_);
    }
}

} // namespace contextdeck::broker
