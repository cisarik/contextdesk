#pragma once

#include <cstdint>

namespace contextdeck::broker {

class ISink {
public:
    virtual ~ISink() = default;
    virtual void writeEvent(uint16_t type, uint16_t code, int32_t value) = 0;
    virtual void flushSyn() = 0;
};

} // namespace contextdeck::broker
