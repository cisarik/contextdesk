#pragma once

namespace contextdeck::broker {

// In-process fake-source/fake-sink check. Does not open devices.
int runSelftest();

// In-process event-loop watchdog check. Does not open devices or talk to systemd.
int runWatchdogSelftest();

} // namespace contextdeck::broker
