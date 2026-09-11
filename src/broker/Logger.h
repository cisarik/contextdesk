#pragma once

#include "broker/Types.h"

#include <string_view>

namespace contextdeck::broker {

// Bounded diagnostics: state names, error classes, counters. Never key content.
class Logger {
public:
    void state(std::string_view transition) const;
    void error(std::string_view errorClass) const;
    void counters(const BrokerCounters &values) const;
};

} // namespace contextdeck::broker
