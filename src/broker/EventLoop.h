#pragma once

#include "broker/Watchdog.h"

namespace contextdeck::broker {

enum class WaitOutcome { Progress, Stop };

class ILoopWait {
public:
  virtual ~ILoopWait() = default;
  virtual WaitOutcome wait(int timeoutMs) = 0;
};

class ILoopWork {
public:
  virtual ~ILoopWork() = default;
  virtual void afterWait() = 0;
};

// Single-thread event loop. WATCHDOG=1 is emitted only after wait() returns
// Progress and afterWait() returns. A hung wait or hung ingest cannot feed
// the watchdog. There is no helper thread or detached timer.
class EventLoop {
public:
  EventLoop(ILoopWait &wait, IWatchdog &watchdog, int timeoutMs, ILoopWork *work = nullptr);

  void run();
  WaitOutcome step();
  int progressCount() const { return progressCount_; }
  bool readySent() const { return readySent_; }

private:
  void ensureReady();

  ILoopWait &wait_;
  IWatchdog &watchdog_;
  ILoopWork *work_ = nullptr;
  int timeoutMs_ = 1000;
  int progressCount_ = 0;
  bool readySent_ = false;
};

} // namespace contextdeck::broker
