#include "broker/Acquisition.h"
#include "broker/EvdevGrabber.h"
#include "broker/FakeGrabber.h"
#include "broker/FakeSink.h"
#include "broker/GrabbingSource.h"
#include "broker/KeyLedger.h"
#include "broker/Logger.h"

#include <algorithm>
#include <cstdio>
#include <linux/input.h>
#include <string>
#include <vector>

using contextdeck::broker::Acquisition;
using contextdeck::broker::EvdevGrabber;
using contextdeck::broker::FakeGrabber;
using contextdeck::broker::FakeSink;
using contextdeck::broker::GrabbingSource;
using contextdeck::broker::ILifecycleSink;
using contextdeck::broker::KeyLedger;
using contextdeck::broker::Logger;
using contextdeck::broker::SourceTag;

namespace {

int g_failures = 0;

void expect(bool ok, const char *expr, int line)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL tests/unit/test_broker_acquisition.cpp:%d %s\n", line, expr);
        ++g_failures;
    }
}

#define EXPECT(expr) expect(static_cast<bool>(expr), #expr, __LINE__)

constexpr uint16_t kCodeA = 59;
constexpr uint16_t kCodeB = 60;

class RecordingSink final : public ILifecycleSink {
public:
    bool createVirtual() override
    {
        created = true;
        destroyed = false;
        return createOk;
    }
    void destroyVirtual() override { destroyed = true; }

    bool createOk = true;
    bool created = false;
    bool destroyed = false;
    bool prepareOk = true;
    bool prepareVirtual() override { return prepareOk; }
};

std::ptrdiff_t indexOf(const std::vector<std::string> &history, const std::string &step)
{
    const auto it = std::find(history.begin(), history.end(), step);
    if (it == history.end()) {
        return -1;
    }
    return it - history.begin();
}

int countStep(const std::vector<std::string> &history, const std::string &step)
{
    return static_cast<int>(std::count(history.begin(), history.end(), step));
}

} // namespace

int main()
{
    {
        Logger logger;
        KeyLedger ledger;
        RecordingSink sink;
        std::vector<std::string> grabLog;
        FakeGrabber grab00("if00", grabLog, true);
        FakeGrabber grab01("if01", grabLog, false);
        GrabbingSource if00(SourceTag::If00, grab00);
        GrabbingSource if01(SourceTag::If01, grab01);
        Acquisition acq(sink, if00, if01, ledger, logger);
        EXPECT(!acq.arm());
        EXPECT(!acq.armed());
        EXPECT(sink.created);
        EXPECT(sink.destroyed);
        EXPECT(!grab00.grabbed());
        EXPECT(!grab01.grabbed());
        EXPECT(countStep(grabLog, "grab-if00") == 1);
        EXPECT(countStep(grabLog, "grab-if01") == 1);
        EXPECT(countStep(grabLog, "ungrab-if00") == 1);
        EXPECT(countStep(grabLog, "ungrab-if01") == 0);
        const auto grabIf00 = indexOf(grabLog, "grab-if00");
        const auto grabIf01 = indexOf(grabLog, "grab-if01");
        const auto ungrabIf00 = indexOf(grabLog, "ungrab-if00");
        EXPECT(grabIf00 >= 0 && grabIf01 >= 0 && ungrabIf00 >= 0);
        EXPECT(grabIf00 < grabIf01);
        EXPECT(grabIf01 < ungrabIf00);
        const auto unclaimIf00 = indexOf(acq.history(), "unclaim-if00");
        const auto destroy = indexOf(acq.history(), "destroy-virtual");
        EXPECT(unclaimIf00 >= 0 && destroy >= 0);
        EXPECT(unclaimIf00 < destroy);
        EXPECT(indexOf(acq.history(), "unclaim-if01") < 0);
    }

    {
        Logger logger;
        KeyLedger ledger;
        ledger.onKey(SourceTag::If00, kCodeA, 1);
        ledger.onKey(SourceTag::If01, kCodeB, 1);
        RecordingSink lifecycle;
        FakeSink events;
        std::vector<std::string> grabLog;
        FakeGrabber grab00("if00", grabLog);
        FakeGrabber grab01("if01", grabLog);
        GrabbingSource if00(SourceTag::If00, grab00);
        GrabbingSource if01(SourceTag::If01, grab01);
        Acquisition acq(lifecycle, if00, if01, ledger, logger, &events);
        EXPECT(acq.arm());
        EXPECT(acq.armed());
        EXPECT(grab00.grabbed());
        EXPECT(grab01.grabbed());
        acq.disarm();
        EXPECT(!acq.armed());
        EXPECT(!grab00.grabbed());
        EXPECT(!grab01.grabbed());
        EXPECT(countStep(grabLog, "grab-if00") == 1);
        EXPECT(countStep(grabLog, "grab-if01") == 1);
        EXPECT(countStep(grabLog, "ungrab-if00") == 1);
        EXPECT(countStep(grabLog, "ungrab-if01") == 1);
        const auto grabIf00 = indexOf(grabLog, "grab-if00");
        const auto grabIf01 = indexOf(grabLog, "grab-if01");
        const auto ungrabIf01 = indexOf(grabLog, "ungrab-if01");
        const auto ungrabIf00 = indexOf(grabLog, "ungrab-if00");
        EXPECT(grabIf00 < grabIf01);
        EXPECT(ungrabIf01 < ungrabIf00);
        const auto unclaimIf01 = indexOf(acq.history(), "unclaim-if01");
        const auto unclaimIf00 = indexOf(acq.history(), "unclaim-if00");
        const auto synthetic = indexOf(acq.history(), "synthetic-disarm");
        const auto destroy = indexOf(acq.history(), "destroy-virtual");
        EXPECT(unclaimIf01 >= 0 && unclaimIf00 >= 0 && synthetic >= 0 && destroy >= 0);
        EXPECT(unclaimIf01 < synthetic);
        EXPECT(unclaimIf00 < synthetic);
        EXPECT(synthetic < destroy);
        EXPECT(events.events().size() == 3);
        EXPECT(events.events()[0].type == EV_KEY && events.events()[0].code == kCodeB && events.events()[0].value == 0);
        EXPECT(events.events()[1].type == EV_KEY && events.events()[1].code == kCodeA && events.events()[1].value == 0);
        EXPECT(events.events()[2].type == EV_SYN);
        EXPECT(ledger.counters().keysDownSynthetic == 0);
    }

    {
        Logger logger;
        KeyLedger ledger;
        RecordingSink sink;
        std::vector<std::string> grabLog;
        FakeGrabber grab00("if00", grabLog);
        FakeGrabber grab01("if01", grabLog);
        GrabbingSource if00(SourceTag::If00, grab00);
        GrabbingSource if01(SourceTag::If01, grab01);
        if01.openOk = false;
        Acquisition acq(sink, if00, if01, ledger, logger);
        EXPECT(!acq.arm());
        EXPECT(!acq.armed());
        EXPECT(!grab00.grabbed());
        EXPECT(!grab01.grabbed());
        EXPECT(countStep(grabLog, "grab-if00") == 0);
        EXPECT(countStep(grabLog, "grab-if01") == 0);
        EXPECT(countStep(grabLog, "ungrab-if00") == 0);
        EXPECT(countStep(grabLog, "ungrab-if01") == 0);
        EXPECT(!sink.created);
        EXPECT(!sink.destroyed);
    }

    {
        EXPECT(EvdevGrabber::create(nullptr) == nullptr);
    }

    {
        Logger logger;
        KeyLedger ledger;
        ledger.onKey(SourceTag::If00, kCodeA, 1);
        RecordingSink lifecycle;
        FakeSink events;
        events.failWrites = true;
        std::vector<std::string> grabLog;
        FakeGrabber grab00("if00", grabLog);
        FakeGrabber grab01("if01", grabLog);
        GrabbingSource if00(SourceTag::If00, grab00);
        GrabbingSource if01(SourceTag::If01, grab01);
        Acquisition acq(lifecycle, if00, if01, ledger, logger, &events);
        EXPECT(acq.arm());
        acq.disarm();
        EXPECT(!acq.armed());
        EXPECT(!grab00.grabbed());
        EXPECT(!grab01.grabbed());
        EXPECT(lifecycle.destroyed);
        EXPECT(events.events().empty());
        EXPECT(ledger.counters().keysDownSynthetic == 0);
        const auto unclaimIf01 = indexOf(acq.history(), "unclaim-if01");
        const auto unclaimIf00 = indexOf(acq.history(), "unclaim-if00");
        const auto synthetic = indexOf(acq.history(), "synthetic-disarm");
        const auto destroy = indexOf(acq.history(), "destroy-virtual");
        EXPECT(unclaimIf01 >= 0 && unclaimIf00 >= 0 && synthetic >= 0 && destroy >= 0);
        EXPECT(unclaimIf01 < synthetic);
        EXPECT(unclaimIf00 < synthetic);
        EXPECT(synthetic < destroy);
    }

    {
        Logger logger;
        KeyLedger ledger;
        RecordingSink sink;
        sink.prepareOk = false;
        std::vector<std::string> grabLog;
        FakeGrabber grab00("if00", grabLog);
        FakeGrabber grab01("if01", grabLog);
        GrabbingSource if00(SourceTag::If00, grab00);
        GrabbingSource if01(SourceTag::If01, grab01);
        Acquisition acq(sink, if00, if01, ledger, logger);
        EXPECT(!acq.arm());
        EXPECT(!acq.armed());
        EXPECT(sink.created);
        EXPECT(sink.destroyed);
        EXPECT(countStep(grabLog, "grab-if00") == 0);
        EXPECT(countStep(grabLog, "grab-if01") == 0);
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "test_broker_acquisition: %d failure(s)\n", g_failures);
        return 1;
    }
    return 0;
}
