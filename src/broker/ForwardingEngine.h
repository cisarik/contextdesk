#pragma once

#include "broker/ISink.h"
#include "broker/ISource.h"
#include "broker/KeyLedger.h"
#include "broker/Logger.h"

namespace contextdeck::broker {

class ForwardingEngine {
public:
    ForwardingEngine(ISink &sink, KeyLedger &ledger, Logger &logger);

    void ingest(ISource &source);

private:
    void handleDropped(ISource &source);

    ISink &sink_;
    KeyLedger &ledger_;
    Logger &logger_;
};

} // namespace contextdeck::broker
