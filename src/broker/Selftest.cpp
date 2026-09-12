#include "broker/Selftest.h"

#include "broker/Acquisition.h"
#include "broker/FakeGrabber.h"
#include "broker/FakeSink.h"
#include "broker/FakeSource.h"
#include "broker/ForwardingEngine.h"
#include "broker/GrabbingSource.h"
#include "broker/IdentityMatcher.h"
#include "broker/KeyLedger.h"
#include "broker/Logger.h"

#include <cstdio>
#include <linux/input.h>
#include <set>
#include <string>
#include <vector>

namespace contextdeck::broker {
namespace {

class QuietSink final : public ILifecycleSink {
public:
    bool createVirtual() override { return true; }
    void destroyVirtual() override { destroyed = true; }
    bool destroyed = false;
};

DeviceCandidate sampleIf00()
{
    DeviceCandidate c;
    c.vendorId = "046d";
    c.modelId = "c336";
    c.interfaceNum = "00";
    c.bustype = BUS_USB;
    c.name = "Logitech Gaming Keyboard G213";
    return c;
}

} // namespace

int runSelftest()
{
    const DeviceCandidate ok = sampleIf00();
    if (evaluateIdentity(ok) != IdentityVerdict::AcceptIf00) {
        std::fprintf(stderr, "contextdeck-broker: error=selftest-identity\n");
        return 1;
    }
    DeviceCandidate virt = ok;
    virt.bustype = BUS_VIRTUAL;
    if (evaluateIdentity(virt) != IdentityVerdict::RejectVirtualBus) {
        std::fprintf(stderr, "contextdeck-broker: error=selftest-virtual-reject\n");
        return 1;
    }
    DeviceCandidate named = ok;
    named.name = "ContextDeck G213 passthrough";
    if (evaluateIdentity(named) != IdentityVerdict::RejectNamePrefix) {
        std::fprintf(stderr, "contextdeck-broker: error=selftest-name-reject\n");
        return 1;
    }

    Logger logger;
    KeyLedger ledger;
    FakeSink sink;
    FakeSource if00Events(SourceTag::If00);
    ForwardingEngine engine(sink, ledger, logger);

    constexpr uint16_t kCode = 59;
    if00Events.enqueue(InputEvent{InputEvent::Kind::Key, kCode, 1});
    if00Events.enqueue(InputEvent{InputEvent::Kind::SynReport, 0, 0});
    if00Events.enqueue(InputEvent{InputEvent::Kind::Key, kCode, 2});
    if00Events.enqueue(InputEvent{InputEvent::Kind::SynReport, 0, 0});
    if (!engine.ingest(if00Events) || ledger.counters().keysDownSynthetic != 1) {
        std::fprintf(stderr, "contextdeck-broker: error=selftest-repeat-ledger\n");
        return 1;
    }

    if00Events.setPhysicalKeys(std::set<uint16_t>{});
    if00Events.enqueue(InputEvent{InputEvent::Kind::SynDropped, 0, 0});
    if (!engine.ingest(if00Events) || ledger.counters().droppedSync != 1 || ledger.counters().keysDownSynthetic != 0) {
        std::fprintf(stderr, "contextdeck-broker: error=selftest-sync-dropped\n");
        return 1;
    }

    if00Events.enqueue(InputEvent{InputEvent::Kind::Key, kCode, 1});
    if00Events.enqueue(InputEvent{InputEvent::Kind::SynReport, 0, 0});
    if (!engine.ingest(if00Events)) {
        std::fprintf(stderr, "contextdeck-broker: error=selftest-ingest\n");
        return 1;
    }

    QuietSink lifecycle;
    std::vector<std::string> grabLog;
    FakeGrabber grab00("if00", grabLog);
    FakeGrabber grab01("if01", grabLog);
    GrabbingSource if00(SourceTag::If00, grab00);
    GrabbingSource if01(SourceTag::If01, grab01);
    Acquisition acq(lifecycle, if00, if01, ledger, logger, &sink);
    if (!acq.arm()) {
        std::fprintf(stderr, "contextdeck-broker: error=selftest-arm\n");
        return 1;
    }
    acq.disarm();
    if (acq.armed() || grab00.grabbed() || grab01.grabbed() || !lifecycle.destroyed) {
        std::fprintf(stderr, "contextdeck-broker: error=selftest-disarm-order\n");
        return 1;
    }
    const auto history = acq.history();
    bool sawUnclaimBeforeSynthetic = false;
    bool sawSynthetic = false;
    bool sawDestroyAfterSynthetic = false;
    for (const std::string &step : history) {
        if (step == "unclaim-if00" || step == "unclaim-if01") {
            if (!sawSynthetic) {
                sawUnclaimBeforeSynthetic = true;
            }
        }
        if (step == "synthetic-disarm") {
            sawSynthetic = true;
        }
        if (step == "destroy-virtual" && sawSynthetic) {
            sawDestroyAfterSynthetic = true;
        }
    }
    if (!sawUnclaimBeforeSynthetic || !sawDestroyAfterSynthetic) {
        std::fprintf(stderr, "contextdeck-broker: error=selftest-ungrab-first\n");
        return 1;
    }

    const BrokerCounters counters = ledger.counters();
    logger.counters(counters);
    std::fprintf(stdout, "selftest: ok\n");
    std::fprintf(stdout, "droppedSync=%llu\n", static_cast<unsigned long long>(counters.droppedSync));
    std::fprintf(stdout, "keysDownPhysical=%llu\n", static_cast<unsigned long long>(counters.keysDownPhysical));
    std::fprintf(stdout, "keysDownSynthetic=%llu\n", static_cast<unsigned long long>(counters.keysDownSynthetic));
    if (counters.keysDownSynthetic != 0 || counters.keysDownPhysical != 0) {
        std::fprintf(stderr, "contextdeck-broker: error=selftest-ledger-not-idle\n");
        return 1;
    }
    return 0;
}

} // namespace contextdeck::broker
