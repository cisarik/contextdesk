#pragma once

namespace contextdeck::broker {

// In-process fake-source/fake-sink check. Does not open devices.
int runSelftest();

} // namespace contextdeck::broker
