#include "broker/Acquisition.h"
#include "broker/KeyLedger.h"
#include "broker/Logger.h"

#include <algorithm>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

using contextdeck::broker::Acquisition;
using contextdeck::broker::ILifecycleSink;
using contextdeck::broker::ILifecycleSource;
using contextdeck::broker::KeyLedger;
using contextdeck::broker::LedgerEmit;
using contextdeck::broker::Logger;
using contextdeck::broker::SourceTag;

namespace {

int g_failures = 0;

void expect(bool ok, const char *expr, int line)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL tests/unit/test_broker_ledger.cpp:%d %s\n", line, expr);
        ++g_failures;
    }
}

#define EXPECT(expr) expect(static_cast<bool>(expr), #expr, __LINE__)

constexpr uint16_t kCodeA = 59;
constexpr uint16_t kCodeB = 60;
constexpr uint16_t kCodeC = 61;

class RecordingSink final : public ILifecycleSink {
public:
    bool createVirtual() override
    {
        created = true;
        return createOk;
    }
    void destroyVirtual() override { destroyed = true; }

    bool createOk = true;
    bool created = false;
    bool destroyed = false;
};

class RecordingSource final : public ILifecycleSource {
public:
    explicit RecordingSource(SourceTag tag, bool *claimOk)
        : tag_(tag)
        , claimOk_(claimOk)
    {
    }

    SourceTag tag() const override { return tag_; }
    bool openSource() override
    {
        opened = true;
        return openOk;
    }
    bool claimSource() override
    {
        claimed = true;
        return claimOk_ == nullptr ? claimOk : *claimOk_;
    }
    void unclaimSource() override { unclaimed = true; }
    void closeSource() override { closed = true; }

    bool openOk = true;
    bool claimOk = true;
    bool opened = false;
    bool claimed = false;
    bool unclaimed = false;
    bool closed = false;

private:
    SourceTag tag_;
    bool *claimOk_ = nullptr;
};

std::ptrdiff_t indexOf(const std::vector<std::string> &history, const std::string &step)
{
    const auto it = std::find(history.begin(), history.end(), step);
    if (it == history.end()) {
        return -1;
    }
    return it - history.begin();
}

} // namespace

int main()
{
    {
        KeyLedger ledger;
        ledger.onKey(SourceTag::If00, kCodeA, 1);
        ledger.onKey(SourceTag::If00, kCodeB, 1);
        EXPECT(ledger.counters().keysDownPhysical == 2);
        EXPECT(ledger.counters().keysDownSynthetic == 2);
        ledger.onKey(SourceTag::If00, kCodeB, 0);
        ledger.onKey(SourceTag::If00, kCodeA, 0);
        EXPECT(ledger.counters().keysDownPhysical == 0);
        EXPECT(ledger.counters().keysDownSynthetic == 0);
        EXPECT(!ledger.syntheticDown(kCodeA));
        EXPECT(!ledger.syntheticDown(kCodeB));
    }

    {
        KeyLedger ledger;
        ledger.onKey(SourceTag::If00, kCodeA, 1);
        ledger.onKey(SourceTag::If01, kCodeB, 1);
        const std::vector<LedgerEmit> released = ledger.disarmSynthetic();
        EXPECT(released.size() == 2);
        EXPECT(released[0].code == kCodeB && released[0].value == 0);
        EXPECT(released[1].code == kCodeA && released[1].value == 0);
        EXPECT(ledger.counters().keysDownSynthetic == 0);
        const std::vector<LedgerEmit> again = ledger.disarmSynthetic();
        EXPECT(again.empty());
    }

    {
        KeyLedger ledger;
        ledger.onKey(SourceTag::If00, kCodeA, 1);
        const auto before = ledger.counters();
        ledger.onKey(SourceTag::If00, kCodeA, 2);
        const auto after = ledger.counters();
        EXPECT(before.keysDownPhysical == after.keysDownPhysical);
        EXPECT(before.keysDownSynthetic == after.keysDownSynthetic);
        EXPECT(ledger.physicalDown(SourceTag::If00, kCodeA));
        EXPECT(ledger.syntheticDown(kCodeA));
    }

    {
        KeyLedger ledger;
        ledger.onKey(SourceTag::If00, kCodeA, 1);
        ledger.onKey(SourceTag::If01, kCodeC, 1);
        const std::vector<LedgerEmit> emitted = ledger.reconcileAfterSync(SourceTag::If00, std::set<uint16_t>{});
        EXPECT(ledger.counters().droppedSync == 1);
        EXPECT(emitted.size() == 1);
        EXPECT(emitted[0].code == kCodeA && emitted[0].value == 0);
        EXPECT(!ledger.syntheticDown(kCodeA));
        EXPECT(ledger.syntheticDown(kCodeC));
        EXPECT(!ledger.physicalDown(SourceTag::If00, kCodeA));
        EXPECT(ledger.physicalDown(SourceTag::If01, kCodeC));
    }

    {
        KeyLedger ledger;
        const std::vector<LedgerEmit> emitted = ledger.reconcileAfterSync(SourceTag::If00, std::set<uint16_t>{kCodeA});
        EXPECT(ledger.counters().droppedSync == 1);
        EXPECT(emitted.size() == 1);
        EXPECT(emitted[0].code == kCodeA && emitted[0].value == 1);
        EXPECT(ledger.syntheticDown(kCodeA));
        EXPECT(ledger.physicalDown(SourceTag::If00, kCodeA));
    }

    {
        Logger logger;
        KeyLedger ledger;
        ledger.onKey(SourceTag::If00, kCodeA, 1);
        RecordingSink sink;
        RecordingSource if00(SourceTag::If00, nullptr);
        RecordingSource if01(SourceTag::If01, nullptr);
        Acquisition acq(sink, if00, if01, ledger, logger);
        EXPECT(acq.arm());
        acq.disarm();
        const std::vector<std::string> &h = acq.history();
        const auto unclaimIf01 = indexOf(h, "unclaim-if01");
        const auto unclaimIf00 = indexOf(h, "unclaim-if00");
        const auto synthetic = indexOf(h, "synthetic-disarm");
        const auto destroy = indexOf(h, "destroy-virtual");
        EXPECT(unclaimIf01 >= 0 && unclaimIf00 >= 0 && synthetic >= 0 && destroy >= 0);
        EXPECT(unclaimIf01 < synthetic);
        EXPECT(unclaimIf00 < synthetic);
        EXPECT(synthetic < destroy);
        EXPECT(if00.unclaimed);
        EXPECT(if01.unclaimed);
        EXPECT(sink.destroyed);
        EXPECT(!acq.armed());
        EXPECT(ledger.counters().keysDownSynthetic == 0);
    }

    {
        Logger logger;
        KeyLedger ledger;
        RecordingSink sink;
        RecordingSource if00(SourceTag::If00, nullptr);
        RecordingSource if01(SourceTag::If01, nullptr);
        if01.claimOk = false;
        Acquisition acq(sink, if00, if01, ledger, logger);
        EXPECT(!acq.arm());
        EXPECT(!acq.armed());
        EXPECT(if00.unclaimed);
        EXPECT(sink.destroyed);
        const auto unclaimIf00 = indexOf(acq.history(), "unclaim-if00");
        const auto destroy = indexOf(acq.history(), "destroy-virtual");
        EXPECT(unclaimIf00 >= 0 && destroy >= 0);
        EXPECT(unclaimIf00 < destroy);
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "test_broker_ledger: %d failure(s)\n", g_failures);
        return 1;
    }
    return 0;
}
