#pragma once

#include "broker/EventLoop.h"

namespace contextdeck::broker {

// Production wait: epoll on signalfd with a caller-supplied timeout.
// Idle timeout is event-loop progress. SIGTERM/SIGINT stop the loop.
class SignalEpollWait final : public ILoopWait {
public:
  SignalEpollWait();
  ~SignalEpollWait() override;

  SignalEpollWait(const SignalEpollWait &) = delete;
  SignalEpollWait &operator=(const SignalEpollWait &) = delete;

  bool valid() const { return epfd_ >= 0 && sigfd_ >= 0; }
  WaitOutcome wait(int timeoutMs) override;

private:
  int epfd_ = -1;
  int sigfd_ = -1;
};

} // namespace contextdeck::broker
