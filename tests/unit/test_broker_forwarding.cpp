#include "broker/FakeSink.h"
#include "broker/FakeSource.h"
#include "broker/ForwardingEngine.h"
#include "broker/KeyLedger.h"
#include "broker/Logger.h"

#include <cstdio>
#include <linux/input.h>
#include <type_traits>

using contextdeck::broker::FakeSink;
using contextdeck::broker::FakeSource;
using contextdeck::broker::ForwardingEngine;
using contextdeck::broker::InputEvent;
using contextdeck::broker::KeyLedger;
using contextdeck::broker::Logger;
using contextdeck::broker::SourceTag;

namespace {

int g_failures = 0;

void expect(bool ok, const char *expr, int line)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL tests/unit/test_broker_forwarding.cpp:%d %s\n", line, expr);
        ++g_failures;
    }
}

#define EXPECT(expr) expect(static_cast<bool>(expr), #expr, __LINE__)

constexpr uint16_t kCodeA = 59;
constexpr uint16_t kCodeMedia = 164;
constexpr uint16_t kLedNuml = 0;

} // namespace

int main()
{
    Logger logger;
    KeyLedger ledger;
    FakeSink sink;
    FakeSource source(SourceTag::If00);
    ForwardingEngine engine(sink, ledger, logger);

    source.enqueue(InputEvent{InputEvent::Kind::Key, kCodeA, 1});
    source.enqueue(InputEvent{InputEvent::Kind::SynReport, 0, 0});
    EXPECT(engine.ingest(source));
    EXPECT(sink.events().size() == 2);
    EXPECT(sink.events()[0].type == EV_KEY && sink.events()[0].code == kCodeA && sink.events()[0].value == 1);
    EXPECT(sink.events()[1].type == EV_SYN && sink.events()[1].code == SYN_REPORT && sink.events()[1].value == 0);
    EXPECT(ledger.syntheticDown(kCodeA));

    source.enqueue(InputEvent{InputEvent::Kind::Key, kCodeA, 2});
    source.enqueue(InputEvent{InputEvent::Kind::SynReport, 0, 0});
    EXPECT(engine.ingest(source));
    EXPECT(sink.events().size() == 4);
    EXPECT(sink.events()[2].type == EV_KEY && sink.events()[2].code == kCodeA && sink.events()[2].value == 2);
    EXPECT(sink.events()[3].type == EV_SYN && sink.events()[3].code == SYN_REPORT);
    EXPECT(ledger.counters().keysDownSynthetic == 1);
    EXPECT(ledger.counters().keysDownPhysical == 1);

    source.enqueue(InputEvent{InputEvent::Kind::Led, kLedNuml, 1});
    source.enqueue(InputEvent{InputEvent::Kind::SynReport, 0, 0});
    EXPECT(engine.ingest(source));
    EXPECT(sink.events().size() == 5);
    EXPECT(sink.events()[4].type == EV_SYN && sink.events()[4].code == SYN_REPORT);

    FakeSource media(SourceTag::If01);
    media.enqueue(InputEvent{InputEvent::Kind::Key, kCodeMedia, 1});
    media.enqueue(InputEvent{InputEvent::Kind::SynReport, 0, 0});
    media.enqueue(InputEvent{InputEvent::Kind::Key, kCodeMedia, 0});
    media.enqueue(InputEvent{InputEvent::Kind::SynReport, 0, 0});
    EXPECT(engine.ingest(media));
    EXPECT(sink.events().size() == 9);
    EXPECT(sink.events()[5].type == EV_KEY && sink.events()[5].value == 1);
    EXPECT(sink.events()[6].type == EV_SYN);
    EXPECT(sink.events()[7].type == EV_KEY && sink.events()[7].value == 0);
    EXPECT(sink.events()[8].type == EV_SYN);

    source.enqueue(InputEvent{InputEvent::Kind::Key, kCodeA, 0});
    source.enqueue(InputEvent{InputEvent::Kind::SynReport, 0, 0});
    EXPECT(engine.ingest(source));
    EXPECT(ledger.counters().keysDownSynthetic == 0);

    source.enqueue(InputEvent{InputEvent::Kind::Key, kCodeA, 1});
    source.enqueue(InputEvent{InputEvent::Kind::SynReport, 0, 0});
    EXPECT(engine.ingest(source));
    sink.clear();
    source.setPhysicalKeys({});
    source.enqueue(InputEvent{InputEvent::Kind::SynDropped, 0, 0});
    EXPECT(engine.ingest(source));
    EXPECT(sink.events().size() == 2);
    EXPECT(sink.events()[0].type == EV_KEY && sink.events()[0].code == kCodeA && sink.events()[0].value == 0);
    EXPECT(sink.events()[1].type == EV_SYN && sink.events()[1].code == SYN_REPORT);
    EXPECT(ledger.counters().droppedSync >= 1);
    EXPECT(!ledger.syntheticDown(kCodeA));

    sink.clear();
    source.setPhysicalKeys({kCodeA});
    source.enqueue(InputEvent{InputEvent::Kind::SynDropped, 0, 0});
    EXPECT(engine.ingest(source));
    EXPECT(sink.events().size() == 2);
    EXPECT(sink.events()[0].type == EV_KEY && sink.events()[0].value == 1);
    EXPECT(sink.events()[1].type == EV_SYN);
    EXPECT(ledger.syntheticDown(kCodeA));

    {
        Logger failLogger;
        KeyLedger failLedger;
        FakeSink failSink;
        failSink.failWrites = true;
        FakeSource failSource(SourceTag::If00);
        ForwardingEngine failEngine(failSink, failLedger, failLogger);
        failSource.enqueue(InputEvent{InputEvent::Kind::Key, kCodeA, 1});
        failSource.enqueue(InputEvent{InputEvent::Kind::SynReport, 0, 0});
        EXPECT(!failEngine.ingest(failSource));
        EXPECT(failSink.events().empty());
        EXPECT(failLedger.syntheticDown(kCodeA));
    }

    static_assert(std::is_same_v<decltype(engine.ingest(source)), bool>,
                  "ingest reports sink-write-failed; there is no remap/command catalog");

    if (g_failures != 0) {
        std::fprintf(stderr, "test_broker_forwarding: %d failure(s)\n", g_failures);
        return 1;
    }
    return 0;
}
