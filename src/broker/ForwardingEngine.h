#pragma once

#include "broker/ISink.h"
#include "broker/ISource.h"
#include "broker/KeyLedger.h"
#include "broker/Logger.h"

namespace contextdeck::broker {

class ForwardingEngine {
public:
    ForwardingEngine(ISink &sink, KeyLedger &ledger, Logger &logger);

    // Returns false on a bounded sink-write-failed condition. The caller must
    // disarm. Physical EV_LED is not forwarded (return path is virtual→if00).
    bool ingest(ISource &source);

private:
    bool handleDropped(ISource &source);
    bool failWrite();

    ISink &sink_;
    KeyLedger &ledger_;
    Logger &logger_;
};

} // namespace contextdeck::broker
