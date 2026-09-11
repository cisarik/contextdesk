#include "broker/Logger.h"

#include <cstdio>

namespace contextdeck::broker {

void Logger::state(std::string_view transition) const
{
    std::fprintf(stderr, "contextdeck-broker: state=%.*s\n",
                 static_cast<int>(transition.size()), transition.data());
}

void Logger::error(std::string_view errorClass) const
{
    std::fprintf(stderr, "contextdeck-broker: error=%.*s\n",
                 static_cast<int>(errorClass.size()), errorClass.data());
}

void Logger::counters(const BrokerCounters &values) const
{
    std::fprintf(stderr,
                 "contextdeck-broker: droppedSync=%llu keysDownPhysical=%llu keysDownSynthetic=%llu\n",
                 static_cast<unsigned long long>(values.droppedSync),
                 static_cast<unsigned long long>(values.keysDownPhysical),
                 static_cast<unsigned long long>(values.keysDownSynthetic));
}

} // namespace contextdeck::broker
