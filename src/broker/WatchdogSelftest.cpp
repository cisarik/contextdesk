#include "broker/Selftest.h"

#include "broker/EventLoop.h"
#include "broker/FakeSink.h"
#include "broker/FakeSource.h"
#include "broker/ForwardingEngine.h"
#include "broker/KeyLedger.h"
#include "broker/Logger.h"
#include "broker/Watchdog.h"

#include <cstdio>
#include <vector>

namespace contextdeck::broker {
namespace {

class RecordingWatchdog final : public IWatchdog {
public:
  int ready = 0;
  int watchdog = 0;
  int stopping = 0;

  void notifyReady() override { ++ready; }
  void notifyWatchdog() override { ++watchdog; }
  void notifyStopping() override { ++stopping; }
};

class ScriptedWait final : public ILoopWait {
public:
  explicit ScriptedWait(std::vector<WaitOutcome> outcomes)
      : outcomes_(std::move(outcomes))
  {
  }

  WaitOutcome wait(int timeoutMs) override
  {
    (void)timeoutMs;
    if (index_ >= outcomes_.size()) {
      return WaitOutcome::Stop;
    }
    return outcomes_[index_++];
  }

private:
  std::vector<WaitOutcome> outcomes_;
  std::size_t index_ = 0;
};

class CountingWork final : public ILoopWork {
public:
  int runs = 0;
  int watchdogSeen = -1;
  RecordingWatchdog *watchdog = nullptr;

  void afterWait() override
  {
    ++runs;
    if (watchdog != nullptr) {
      watchdogSeen = watchdog->watchdog;
    }
  }
};

} // namespace

int runWatchdogSelftest()
{
  RecordingWatchdog wd;
  CountingWork work;
  work.watchdog = &wd;
  ScriptedWait wait({WaitOutcome::Progress, WaitOutcome::Progress, WaitOutcome::Progress, WaitOutcome::Stop});
  EventLoop loop(wait, wd, 1000, &work);
  loop.run();
  if (wd.ready != 1 || wd.watchdog != 3 || work.runs != 3 || work.watchdogSeen != 2) {
    std::fprintf(stderr, "contextdeck-broker: error=selftest-watchdog-progress\n");
    return 1;
  }
  if (wd.watchdog != loop.progressCount()) {
    std::fprintf(stderr, "contextdeck-broker: error=selftest-watchdog-count\n");
    return 1;
  }

  RecordingWatchdog hungWd;
  ScriptedWait hungWait({WaitOutcome::Progress, WaitOutcome::Stop});
  EventLoop hungLoop(hungWait, hungWd, 1000, nullptr);
  if (hungLoop.step() != WaitOutcome::Progress || hungWd.watchdog != 1) {
    std::fprintf(stderr, "contextdeck-broker: error=selftest-watchdog-first-progress\n");
    return 1;
  }
  if (hungLoop.step() != WaitOutcome::Stop || hungWd.watchdog != 1 || hungWd.ready != 1) {
    std::fprintf(stderr, "contextdeck-broker: error=selftest-watchdog-hang-stop\n");
    return 1;
  }

  Logger logger;
  KeyLedger ledger;
  FakeSink sink;
  FakeSource source(SourceTag::If00);
  ForwardingEngine engine(sink, ledger, logger);
  source.enqueue(InputEvent{InputEvent::Kind::Key, 59, 1});
  source.enqueue(InputEvent{InputEvent::Kind::SynReport, 0, 0});
  source.enqueue(InputEvent{InputEvent::Kind::Key, 59, 0});
  source.enqueue(InputEvent{InputEvent::Kind::SynReport, 0, 0});

  class IngestWork final : public ILoopWork {
  public:
    IngestWork(ForwardingEngine &engine, FakeSource &source)
        : engine_(engine)
        , source_(source)
    {
    }
    void afterWait() override { (void)engine_.ingest(source_); }

  private:
    ForwardingEngine &engine_;
    FakeSource &source_;
  };

  RecordingWatchdog ingestWd;
  IngestWork ingest(engine, source);
  ScriptedWait ingestWait({WaitOutcome::Progress, WaitOutcome::Stop});
  EventLoop ingestLoop(ingestWait, ingestWd, 1000, &ingest);
  ingestLoop.run();
  if (ingestWd.ready != 1 || ingestWd.watchdog != 1 || ledger.counters().keysDownSynthetic != 0) {
    std::fprintf(stderr, "contextdeck-broker: error=selftest-watchdog-ingest\n");
    return 1;
  }

  std::fprintf(stdout, "watchdog-selftest: ok\n");
  std::fprintf(stdout, "ready=%d\n", wd.ready);
  std::fprintf(stdout, "watchdog=%d\n", wd.watchdog);
  return 0;
}

} // namespace contextdeck::broker
