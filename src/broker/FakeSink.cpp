#include "broker/FakeSink.h"

#include <linux/input.h>

namespace contextdeck::broker {

void FakeSink::writeEvent(uint16_t type, uint16_t code, int32_t value)
{
    events_.push_back(RecordedEvent{type, code, value});
}

void FakeSink::flushSyn()
{
    events_.push_back(RecordedEvent{EV_SYN, SYN_REPORT, 0});
}

} // namespace contextdeck::broker
