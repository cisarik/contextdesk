#include "broker/Acquisition.h"
#include "broker/RealSink.h"

#include <linux/input.h>

namespace contextdeck::broker {

Acquisition::Acquisition(ILifecycleSink &sink, ILifecycleSource &if00, ILifecycleSource &if01, KeyLedger &ledger,
                         Logger &logger, ISink *events)
    : sink_(sink)
    , if00_(if00)
    , if01_(if01)
    , ledger_(ledger)
    , logger_(logger)
    , events_(events)
{
}

void Acquisition::record(const char *step)
{
    history_.emplace_back(step);
}

void Acquisition::releaseSourcesUngrabFirst()
{
    record("unclaim-if01");
    if01_.unclaimSource();
    record("close-if01");
    if01_.closeSource();
    ledger_.clearPhysical(SourceTag::If01);
    record("unclaim-if00");
    if00_.unclaimSource();
    record("close-if00");
    if00_.closeSource();
    ledger_.clearPhysical(SourceTag::If00);
}

void Acquisition::emitSyntheticDisarm()
{
    record("synthetic-disarm");
    const std::vector<LedgerEmit> emitted = ledger_.disarmSynthetic();
    if (events_ == nullptr) {
        return;
    }
    // Cleanup writes are best-effort. Ledger is already cleared.
    for (const LedgerEmit &item : emitted) {
        (void)events_->writeEvent(EV_KEY, item.code, item.value);
    }
    if (!emitted.empty()) {
        (void)events_->flushSyn();
    }
}

void Acquisition::rollbackFrom(bool if00Opened, bool if00Claimed, bool if01Opened, bool if01Claimed, bool virtualCreated)
{
    if (if01Claimed) {
        record("unclaim-if01");
        if01_.unclaimSource();
    }
    if (if01Opened) {
        record("close-if01");
        if01_.closeSource();
        ledger_.clearPhysical(SourceTag::If01);
    }
    if (if00Claimed) {
        record("unclaim-if00");
        if00_.unclaimSource();
    }
    if (if00Opened) {
        record("close-if00");
        if00_.closeSource();
        ledger_.clearPhysical(SourceTag::If00);
    }
    emitSyntheticDisarm();
    if (virtualCreated) {
        record("destroy-virtual");
        sink_.destroyVirtual();
    }
    armed_ = false;
    logger_.state("disarmed");
}

bool Acquisition::arm()
{
    if (armed_) {
        return true;
    }
    logger_.state("arming");

    record("open-if00");
    if (!if00_.openSource()) {
        logger_.error("source-open-failed");
        logger_.state("disarmed");
        return false;
    }
    record("open-if01");
    if (!if01_.openSource()) {
        logger_.error("source-open-failed");
        rollbackFrom(true, false, false, false, false);
        return false;
    }

    record("measure-capabilities");
    const SinkCapabilities caps = unionSourceCapabilities(if00_.measuredCapabilities(), if01_.measuredCapabilities());
    if (!sink_.applyMeasuredCapabilities(caps)) {
        logger_.error("capability-invalid");
        rollbackFrom(true, false, true, false, false);
        return false;
    }

    record("create-virtual");
    if (!sink_.createVirtual()) {
        logger_.error("virtual-create-failed");
        rollbackFrom(true, false, true, false, true);
        return false;
    }
    record("prepare-virtual");
    if (!sink_.prepareVirtual()) {
        logger_.error("led-setup-failed");
        rollbackFrom(true, false, true, false, true);
        return false;
    }

    record("claim-if00");
    if (!if00_.claimSource()) {
        logger_.error("source-claim-failed");
        rollbackFrom(true, false, true, false, true);
        return false;
    }
    record("claim-if01");
    if (!if01_.claimSource()) {
        logger_.error("source-claim-failed");
        rollbackFrom(true, true, true, false, true);
        return false;
    }

    armed_ = true;
    logger_.state("armed");
    return true;
}

void Acquisition::disarm()
{
    if (!armed_) {
        return;
    }
    logger_.state("disarming");
    // Plan contract: ungrab physical first, then balanced synthetic releases, then destroy uinput.
    releaseSourcesUngrabFirst();
    emitSyntheticDisarm();
    record("destroy-virtual");
    sink_.destroyVirtual();
    armed_ = false;
    logger_.state("disarmed");
}

} // namespace contextdeck::broker
