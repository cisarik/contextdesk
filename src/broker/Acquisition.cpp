#include "broker/Acquisition.h"

namespace contextdeck::broker {

Acquisition::Acquisition(ILifecycleSink &sink, ILifecycleSource &if00, ILifecycleSource &if01, KeyLedger &ledger,
                         Logger &logger)
    : sink_(sink)
    , if00_(if00)
    , if01_(if01)
    , ledger_(ledger)
    , logger_(logger)
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
    record("unclaim-if00");
    if00_.unclaimSource();
    record("close-if00");
    if00_.closeSource();
}

void Acquisition::rollbackFrom(int openedSources, bool if00Claimed, bool if01Opened, bool if01Claimed)
{
    if (if01Claimed) {
        record("unclaim-if01");
        if01_.unclaimSource();
    }
    if (if01Opened) {
        record("close-if01");
        if01_.closeSource();
    }
    if (if00Claimed) {
        record("unclaim-if00");
        if00_.unclaimSource();
    }
    if (openedSources >= 1) {
        record("close-if00");
        if00_.closeSource();
    }
    record("synthetic-disarm");
    (void)ledger_.disarmSynthetic();
    record("destroy-virtual");
    sink_.destroyVirtual();
    armed_ = false;
    logger_.state("disarmed");
}

bool Acquisition::arm()
{
    if (armed_) {
        return true;
    }
    logger_.state("arming");
    record("create-virtual");
    if (!sink_.createVirtual()) {
        logger_.error("virtual-create-failed");
        logger_.state("disarmed");
        return false;
    }

    record("open-if00");
    if (!if00_.openSource()) {
        logger_.error("source-open-failed");
        rollbackFrom(0, false, false, false);
        return false;
    }
    record("claim-if00");
    if (!if00_.claimSource()) {
        logger_.error("source-claim-failed");
        rollbackFrom(1, false, false, false);
        return false;
    }
    record("open-if01");
    if (!if01_.openSource()) {
        logger_.error("source-open-failed");
        rollbackFrom(1, true, false, false);
        return false;
    }
    record("claim-if01");
    if (!if01_.claimSource()) {
        logger_.error("source-claim-failed");
        rollbackFrom(1, true, true, false);
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
    record("synthetic-disarm");
    (void)ledger_.disarmSynthetic();
    record("destroy-virtual");
    sink_.destroyVirtual();
    armed_ = false;
    logger_.state("disarmed");
}

} // namespace contextdeck::broker
