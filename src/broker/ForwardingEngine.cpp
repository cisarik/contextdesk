#include "broker/ForwardingEngine.h"

#include <linux/input.h>

namespace contextdeck::broker {

ForwardingEngine::ForwardingEngine(ISink &sink, KeyLedger &ledger, Logger &logger)
    : sink_(sink)
    , ledger_(ledger)
    , logger_(logger)
{
}

bool ForwardingEngine::failWrite()
{
    logger_.error("sink-write-failed");
    return false;
}

bool ForwardingEngine::handleDropped(ISource &source)
{
    logger_.state("sync-dropped");
    const std::vector<LedgerEmit> emitted = ledger_.reconcileAfterSync(source.tag(), source.keysDown());
    for (const LedgerEmit &item : emitted) {
        if (!sink_.writeEvent(EV_KEY, item.code, item.value)) {
            return failWrite();
        }
    }
    if (!sink_.flushSyn()) {
        return failWrite();
    }
    return true;
}

bool ForwardingEngine::ingest(ISource &source)
{
    while (const std::optional<InputEvent> event = source.read()) {
        switch (event->kind) {
        case InputEvent::Kind::Key:
            ledger_.onKey(source.tag(), event->code, event->value);
            if (!sink_.writeEvent(EV_KEY, event->code, event->value)) {
                return failWrite();
            }
            break;
        case InputEvent::Kind::Led:
            // Physical LED reports are not forwarded. Compositor LED state
            // returns through the virtual uinput fd to if00 only.
            break;
        case InputEvent::Kind::Msc:
            if (!sink_.writeEvent(EV_MSC, event->code, event->value)) {
                return failWrite();
            }
            break;
        case InputEvent::Kind::SynReport:
            if (!sink_.flushSyn()) {
                return failWrite();
            }
            break;
        case InputEvent::Kind::SynDropped:
            if (!handleDropped(source)) {
                return false;
            }
            break;
        }
    }
    return true;
}

} // namespace contextdeck::broker
