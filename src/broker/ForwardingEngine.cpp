#include "broker/ForwardingEngine.h"

#include <linux/input.h>

namespace contextdeck::broker {

ForwardingEngine::ForwardingEngine(ISink &sink, KeyLedger &ledger, Logger &logger)
    : sink_(sink)
    , ledger_(ledger)
    , logger_(logger)
{
}

void ForwardingEngine::handleDropped(ISource &source)
{
    logger_.state("sync-dropped");
    const std::vector<LedgerEmit> emitted = ledger_.reconcileAfterSync(source.tag(), source.keysDown());
    for (const LedgerEmit &item : emitted) {
        sink_.writeEvent(EV_KEY, item.code, item.value);
    }
    sink_.flushSyn();
}

void ForwardingEngine::ingest(ISource &source)
{
    while (const std::optional<InputEvent> event = source.read()) {
        switch (event->kind) {
        case InputEvent::Kind::Key:
            ledger_.onKey(source.tag(), event->code, event->value);
            sink_.writeEvent(EV_KEY, event->code, event->value);
            break;
        case InputEvent::Kind::Led:
            sink_.writeEvent(EV_LED, event->code, event->value);
            break;
        case InputEvent::Kind::Msc:
            sink_.writeEvent(EV_MSC, event->code, event->value);
            break;
        case InputEvent::Kind::SynReport:
            sink_.flushSyn();
            break;
        case InputEvent::Kind::SynDropped:
            handleDropped(source);
            break;
        }
    }
}

} // namespace contextdeck::broker
