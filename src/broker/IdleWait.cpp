#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "broker/IdleWait.h"

#include <cerrno>
#include <csignal>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <unistd.h>

namespace contextdeck::broker {

SignalEpollWait::SignalEpollWait()
{
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGTERM);
  sigaddset(&mask, SIGINT);
  if (sigprocmask(SIG_BLOCK, &mask, nullptr) < 0) {
    return;
  }

  sigfd_ = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
  if (sigfd_ < 0) {
    return;
  }

  epfd_ = epoll_create1(EPOLL_CLOEXEC);
  if (epfd_ < 0) {
    ::close(sigfd_);
    sigfd_ = -1;
    return;
  }

  epoll_event ev{};
  ev.events = EPOLLIN;
  ev.data.fd = sigfd_;
  if (epoll_ctl(epfd_, EPOLL_CTL_ADD, sigfd_, &ev) < 0) {
    ::close(epfd_);
    ::close(sigfd_);
    epfd_ = -1;
    sigfd_ = -1;
  }
}

SignalEpollWait::~SignalEpollWait()
{
  if (sigfd_ >= 0) {
    ::close(sigfd_);
  }
  if (epfd_ >= 0) {
    ::close(epfd_);
  }
}

WaitOutcome SignalEpollWait::wait(int timeoutMs)
{
  if (!valid()) {
    return WaitOutcome::Stop;
  }

  epoll_event ev{};
  const int rc = epoll_wait(epfd_, &ev, 1, timeoutMs);
  if (rc < 0) {
    if (errno == EINTR) {
      return WaitOutcome::Progress;
    }
    return WaitOutcome::Stop;
  }
  if (rc == 0) {
    return WaitOutcome::Progress;
  }
  if ((ev.events & EPOLLIN) != 0 && ev.data.fd == sigfd_) {
    signalfd_siginfo info{};
    const ssize_t n = ::read(sigfd_, &info, sizeof(info));
    (void)n;
    return WaitOutcome::Stop;
  }
  return WaitOutcome::Progress;
}

} // namespace contextdeck::broker
