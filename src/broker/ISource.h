#pragma once

#include "broker/Types.h"

#include <optional>
#include <set>

namespace contextdeck::broker {

class ISource {
public:
    virtual ~ISource() = default;
    virtual SourceTag tag() const = 0;
    virtual std::optional<InputEvent> read() = 0;
    virtual std::set<uint16_t> keysDown() const = 0;
};

} // namespace contextdeck::broker
