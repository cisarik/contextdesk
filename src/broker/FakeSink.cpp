#include "broker/FakeSink.h"

#include <linux/input.h>

namespace contextdeck::broker {

bool FakeSink::writeEvent(uint16_t type, uint16_t code, int32_t value)
{
    if (failWrites) {
        return false;
    }
    events_.push_back(RecordedEvent{type, code, value});
    return true;
}

bool FakeSink::flushSyn()
{
    return writeEvent(EV_SYN, SYN_REPORT, 0);
}

} // namespace contextdeck::broker
