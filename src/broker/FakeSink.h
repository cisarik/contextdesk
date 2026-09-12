#pragma once

#include "broker/ISink.h"
#include "broker/Types.h"

#include <vector>

namespace contextdeck::broker {

class FakeSink final : public ISink {
public:
    bool writeEvent(uint16_t type, uint16_t code, int32_t value) override;
    bool flushSyn() override;
    const std::vector<RecordedEvent> &events() const { return events_; }
    void clear() { events_.clear(); }

    bool failWrites = false;

private:
    std::vector<RecordedEvent> events_;
};

} // namespace contextdeck::broker
