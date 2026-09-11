#include "broker/Watchdog.h"

#include <cstdint>
#include <systemd/sd-daemon.h>

namespace contextdeck::broker {

SystemdWatchdog::SystemdWatchdog(SystemdNotifyFn send)
    : send_(send != nullptr ? send : sd_notify)
{
}

void SystemdWatchdog::notifyReady()
{
  (void)send_(0, "READY=1");
}

void SystemdWatchdog::notifyWatchdog()
{
  (void)send_(0, "WATCHDOG=1");
}

void SystemdWatchdog::notifyStopping()
{
  (void)send_(0, "STOPPING=1");
}

int watchdogFeedTimeoutMs()
{
  uint64_t usec = 0;
  const int enabled = sd_watchdog_enabled(0, &usec);
  if (enabled <= 0 || usec < 2000ULL) {
    return 1000;
  }
  const uint64_t halfMs = usec / 2000ULL;
  if (halfMs > 86400000ULL) {
    return 1000;
  }
  return static_cast<int>(halfMs);
}

} // namespace contextdeck::broker
