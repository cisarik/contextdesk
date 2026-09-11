#include "broker/EventLoop.h"

namespace contextdeck::broker {

EventLoop::EventLoop(ILoopWait &wait, IWatchdog &watchdog, int timeoutMs, ILoopWork *work)
    : wait_(wait)
    , watchdog_(watchdog)
    , work_(work)
    , timeoutMs_(timeoutMs > 0 ? timeoutMs : 1000)
{
}

void EventLoop::ensureReady()
{
  if (readySent_) {
    return;
  }
  watchdog_.notifyReady();
  readySent_ = true;
}

WaitOutcome EventLoop::step()
{
  ensureReady();
  const WaitOutcome outcome = wait_.wait(timeoutMs_);
  if (outcome != WaitOutcome::Progress) {
    return outcome;
  }
  if (work_ != nullptr) {
    work_->afterWait();
  }
  watchdog_.notifyWatchdog();
  ++progressCount_;
  return WaitOutcome::Progress;
}

void EventLoop::run()
{
  for (;;) {
    if (step() == WaitOutcome::Stop) {
      return;
    }
  }
}

} // namespace contextdeck::broker
