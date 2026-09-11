#pragma once

namespace contextdeck::broker {

// systemd notify payload sender. Matches sd_notify(3).
using SystemdNotifyFn = int (*)(int unsetEnvironment, const char *state);

class IWatchdog {
public:
  virtual ~IWatchdog() = default;
  virtual void notifyReady() = 0;
  virtual void notifyWatchdog() = 0;
  virtual void notifyStopping() {}
};

class SystemdWatchdog final : public IWatchdog {
public:
  explicit SystemdWatchdog(SystemdNotifyFn send = nullptr);

  void notifyReady() override;
  void notifyWatchdog() override;
  void notifyStopping() override;

private:
  SystemdNotifyFn send_;
};

// Half of systemd WatchdogSec when $WATCHDOG_USEC/$WATCHDOG_PID are set;
// otherwise 1000 ms so an unsupervised idle loop still uses a 1 s wait.
int watchdogFeedTimeoutMs();

} // namespace contextdeck::broker
