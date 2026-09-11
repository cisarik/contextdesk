#pragma once

#include "broker/EventLoop.h"

#include <vector>

namespace contextdeck::broker {

// Production wait: epoll on signalfd plus optional extra fds (IPC).
// Idle timeout is event-loop progress. SIGTERM/SIGINT stop the loop.
class SignalEpollWait final : public ILoopWait {
public:
  SignalEpollWait();
  ~SignalEpollWait() override;

  SignalEpollWait(const SignalEpollWait &) = delete;
  SignalEpollWait &operator=(const SignalEpollWait &) = delete;

  bool valid() const { return epfd_ >= 0 && sigfd_ >= 0; }
  bool addFd(int fd);
  void removeFd(int fd);
  const std::vector<int> &readyFds() const { return readyFds_; }
  WaitOutcome wait(int timeoutMs) override;

private:
  int epfd_ = -1;
  int sigfd_ = -1;
  std::vector<int> readyFds_;
};

} // namespace contextdeck::broker
