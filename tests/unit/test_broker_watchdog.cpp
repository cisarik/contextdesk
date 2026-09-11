#include "broker/EventLoop.h"
#include "broker/FakeSink.h"
#include "broker/FakeSource.h"
#include "broker/ForwardingEngine.h"
#include "broker/IdleWait.h"
#include "broker/KeyLedger.h"
#include "broker/Logger.h"
#include "broker/Watchdog.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <poll.h>
#include <string>
#include <sys/eventfd.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

using contextdeck::broker::EventLoop;
using contextdeck::broker::FakeSink;
using contextdeck::broker::FakeSource;
using contextdeck::broker::ForwardingEngine;
using contextdeck::broker::ILoopWait;
using contextdeck::broker::ILoopWork;
using contextdeck::broker::IWatchdog;
using contextdeck::broker::InputEvent;
using contextdeck::broker::KeyLedger;
using contextdeck::broker::Logger;
using contextdeck::broker::SignalEpollWait;
using contextdeck::broker::SourceTag;
using contextdeck::broker::SystemdNotifyFn;
using contextdeck::broker::SystemdWatchdog;
using contextdeck::broker::WaitOutcome;
using contextdeck::broker::watchdogFeedTimeoutMs;

namespace {

int g_failures = 0;

void expect(bool ok, const char *expr, int line)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL tests/unit/test_broker_watchdog.cpp:%d %s\n", line, expr);
        ++g_failures;
    }
}

#define EXPECT(expr) expect(static_cast<bool>(expr), #expr, __LINE__)

class RecordingWatchdog final : public IWatchdog {
public:
    std::vector<std::string> order;

    void notifyReady() override { order.emplace_back("ready"); }
    void notifyWatchdog() override { order.emplace_back("watchdog"); }
    void notifyStopping() override { order.emplace_back("stopping"); }

    int count(const char *label) const
    {
        int n = 0;
        for (const std::string &item : order) {
            if (item == label) {
                ++n;
            }
        }
        return n;
    }
};

class ScriptedWait final : public ILoopWait {
public:
    explicit ScriptedWait(std::vector<WaitOutcome> outcomes)
        : outcomes_(std::move(outcomes))
    {
    }

    WaitOutcome wait(int timeoutMs) override
    {
        lastTimeoutMs = timeoutMs;
        ++waitCalls;
        if (index_ >= outcomes_.size()) {
            return WaitOutcome::Stop;
        }
        return outcomes_[index_++];
    }

    int lastTimeoutMs = -1;
    int waitCalls = 0;

private:
    std::vector<WaitOutcome> outcomes_;
    std::size_t index_ = 0;
};

class ProbeWork final : public ILoopWork {
public:
    RecordingWatchdog *watchdog = nullptr;
    int watchdogDuringWork = -1;
    int runs = 0;

    void afterWait() override
    {
        ++runs;
        if (watchdog != nullptr) {
            watchdogDuringWork = watchdog->count("watchdog");
        }
    }
};

std::vector<std::string> g_notifyPayloads;

int recordingSend(int unsetEnvironment, const char *state)
{
    if (unsetEnvironment != 0 || state == nullptr) {
        return -EINVAL;
    }
    g_notifyPayloads.emplace_back(state);
    return 1;
}

void testSystemdPayloads()
{
    g_notifyPayloads.clear();
    const SystemdNotifyFn send = recordingSend;
    SystemdWatchdog wd(send);
    wd.notifyReady();
    wd.notifyWatchdog();
    wd.notifyStopping();
    EXPECT(g_notifyPayloads.size() == 3);
    EXPECT(g_notifyPayloads[0] == "READY=1");
    EXPECT(g_notifyPayloads[1] == "WATCHDOG=1");
    EXPECT(g_notifyPayloads[2] == "STOPPING=1");
}

void testWatchdogIntervalFromEnv()
{
    const char *oldUsec = ::getenv("WATCHDOG_USEC");
    const char *oldPid = ::getenv("WATCHDOG_PID");
    std::string savedUsec = oldUsec != nullptr ? oldUsec : "";
    std::string savedPid = oldPid != nullptr ? oldPid : "";
    const bool hadUsec = oldUsec != nullptr;
    const bool hadPid = oldPid != nullptr;

    char pidbuf[32];
    std::snprintf(pidbuf, sizeof(pidbuf), "%ld", static_cast<long>(::getpid()));
    EXPECT(::setenv("WATCHDOG_USEC", "2000000", 1) == 0);
    EXPECT(::setenv("WATCHDOG_PID", pidbuf, 1) == 0);
    EXPECT(watchdogFeedTimeoutMs() == 1000);

    EXPECT(::setenv("WATCHDOG_USEC", "4000000", 1) == 0);
    EXPECT(watchdogFeedTimeoutMs() == 2000);

    if (hadUsec) {
        (void)::setenv("WATCHDOG_USEC", savedUsec.c_str(), 1);
    } else {
        (void)::unsetenv("WATCHDOG_USEC");
    }
    if (hadPid) {
        (void)::setenv("WATCHDOG_PID", savedPid.c_str(), 1);
    } else {
        (void)::unsetenv("WATCHDOG_PID");
    }
}

void testProgressFeedsWatchdogAfterWork()
{
    RecordingWatchdog wd;
    ProbeWork work;
    work.watchdog = &wd;
    ScriptedWait wait({WaitOutcome::Progress, WaitOutcome::Progress, WaitOutcome::Stop});
    EventLoop loop(wait, wd, 1000, &work);
    loop.run();
    EXPECT(wd.count("ready") == 1);
    EXPECT(wd.count("watchdog") == 2);
    EXPECT(work.runs == 2);
    EXPECT(work.watchdogDuringWork == 1);
    EXPECT(loop.progressCount() == 2);
    EXPECT(wd.order.size() >= 3);
    EXPECT(wd.order[0] == "ready");
    EXPECT(wd.order[1] == "watchdog");
    EXPECT(wd.order[2] == "watchdog");
    EXPECT(wait.lastTimeoutMs == 1000);
}

void testStopDoesNotFeedWatchdog()
{
    RecordingWatchdog wd;
    ScriptedWait wait({WaitOutcome::Stop});
    EventLoop loop(wait, wd, 1000, nullptr);
    EXPECT(loop.step() == WaitOutcome::Stop);
    EXPECT(wd.count("ready") == 1);
    EXPECT(wd.count("watchdog") == 0);
    EXPECT(loop.progressCount() == 0);
}

void testNoBackgroundFeed()
{
    RecordingWatchdog wd;
    ScriptedWait wait({WaitOutcome::Progress, WaitOutcome::Stop});
    EventLoop loop(wait, wd, 1000, nullptr);
    EXPECT(loop.step() == WaitOutcome::Progress);
    EXPECT(wd.count("watchdog") == 1);
    pollfd idle{};
    (void)::poll(&idle, 0, 50);
    EXPECT(wd.count("watchdog") == 1);
    EXPECT(loop.step() == WaitOutcome::Stop);
    EXPECT(wd.count("watchdog") == 1);
}

void testSignalEpollIdleTimeout()
{
    RecordingWatchdog wd;
    SignalEpollWait wait;
    EXPECT(wait.valid());
    EventLoop loop(wait, wd, 20, nullptr);
    EXPECT(loop.step() == WaitOutcome::Progress);
    EXPECT(wd.count("ready") == 1);
    EXPECT(wd.count("watchdog") == 1);
}

void testIngestProgressPreservesForwarding()
{
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
        IngestWork(ForwardingEngine &engine, FakeSource &source, RecordingWatchdog &wd)
            : engine_(engine)
            , source_(source)
            , wd_(wd)
        {
        }

        int watchdogDuringIngest = -1;

        void afterWait() override
        {
            watchdogDuringIngest = wd_.count("watchdog");
            engine_.ingest(source_);
        }

    private:
        ForwardingEngine &engine_;
        FakeSource &source_;
        RecordingWatchdog &wd_;
    };

    RecordingWatchdog wd;
    IngestWork work(engine, source, wd);
    ScriptedWait wait({WaitOutcome::Progress, WaitOutcome::Stop});
    EventLoop loop(wait, wd, 1000, &work);
    loop.run();
    EXPECT(work.watchdogDuringIngest == 0);
    EXPECT(wd.count("watchdog") == 1);
    EXPECT(sink.events().size() == 4);
    EXPECT(ledger.counters().keysDownSynthetic == 0);
    EXPECT(ledger.counters().keysDownPhysical == 0);
}

ssize_t readAll(int fd, char *buf, size_t cap)
{
    size_t used = 0;
    while (used + 1 < cap) {
        pollfd pfd{};
        pfd.fd = fd;
        pfd.events = POLLIN;
        const int rc = ::poll(&pfd, 1, 200);
        if (rc <= 0) {
            break;
        }
        const ssize_t n = ::read(fd, buf + used, cap - 1 - used);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (n == 0) {
            break;
        }
        used += static_cast<size_t>(n);
    }
    buf[used] = '\0';
    return static_cast<ssize_t>(used);
}

bool reap(pid_t pid, int *status)
{
    for (int i = 0; i < 50; ++i) {
        const pid_t rc = ::waitpid(pid, status, WNOHANG);
        if (rc == pid) {
            return true;
        }
        if (rc < 0 && errno != EINTR) {
            return false;
        }
        (void)::poll(nullptr, 0, 20);
    }
    (void)::kill(pid, SIGKILL);
    return ::waitpid(pid, status, 0) == pid;
}

void hangChild()
{
    class PipeWatchdog final : public IWatchdog {
    public:
        void notifyReady() override
        {
            std::fprintf(stdout, "harness=ready\n");
            std::fflush(stdout);
        }
        void notifyWatchdog() override
        {
            std::fprintf(stdout, "harness=watchdog\n");
            std::fflush(stdout);
        }
    };

    class HangWait final : public ILoopWait {
    public:
        WaitOutcome wait(int timeoutMs) override
        {
            (void)timeoutMs;
            const int fd = ::eventfd(0, EFD_CLOEXEC);
            if (fd < 0) {
                _exit(2);
            }
            pollfd pfd{};
            pfd.fd = fd;
            pfd.events = POLLIN;
            for (;;) {
                (void)::poll(&pfd, 1, -1);
            }
        }
    };

    PipeWatchdog wd;
    HangWait wait;
    EventLoop loop(wait, wd, 1000, nullptr);
    loop.run();
    _exit(3);
}

void workHangChild()
{
    class PipeWatchdog final : public IWatchdog {
    public:
        void notifyReady() override
        {
            std::fprintf(stdout, "harness=ready\n");
            std::fflush(stdout);
        }
        void notifyWatchdog() override
        {
            std::fprintf(stdout, "harness=watchdog\n");
            std::fflush(stdout);
        }
    };

    class InstantWait final : public ILoopWait {
    public:
        WaitOutcome wait(int timeoutMs) override
        {
            (void)timeoutMs;
            return WaitOutcome::Progress;
        }
    };

    class HangWork final : public ILoopWork {
    public:
        void afterWait() override
        {
            const int fd = ::eventfd(0, EFD_CLOEXEC);
            if (fd < 0) {
                _exit(2);
            }
            pollfd pfd{};
            pfd.fd = fd;
            pfd.events = POLLIN;
            for (;;) {
                (void)::poll(&pfd, 1, -1);
            }
        }
    };

    PipeWatchdog wd;
    InstantWait wait;
    HangWork work;
    EventLoop loop(wait, wd, 1000, &work);
    loop.run();
    _exit(3);
}

void disableCoreDumps()
{
    struct rlimit lim {};
    lim.rlim_cur = 0;
    lim.rlim_max = 0;
    (void)::setrlimit(RLIMIT_CORE, &lim);
}

void crashChild()
{
    class PipeWatchdog final : public IWatchdog {
    public:
        void notifyReady() override
        {
            std::fprintf(stdout, "harness=ready\n");
            std::fflush(stdout);
        }
        void notifyWatchdog() override
        {
            std::fprintf(stdout, "harness=watchdog\n");
            std::fflush(stdout);
        }
    };

    class InstantWait final : public ILoopWait {
    public:
        WaitOutcome wait(int timeoutMs) override
        {
            (void)timeoutMs;
            return WaitOutcome::Progress;
        }
    };

    class CrashWork final : public ILoopWork {
    public:
        void afterWait() override
        {
            disableCoreDumps();
            ::abort();
        }
    };

    PipeWatchdog wd;
    InstantWait wait;
    CrashWork work;
    EventLoop loop(wait, wd, 1000, &work);
    loop.run();
    _exit(3);
}

void testHangHarness(void (*childMain)(), const char *label)
{
    int fds[2];
    if (::pipe(fds) != 0) {
        EXPECT(false);
        return;
    }
    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(fds[0]);
        ::close(fds[1]);
        EXPECT(false);
        return;
    }
    if (pid == 0) {
        ::close(fds[0]);
        if (::dup2(fds[1], STDOUT_FILENO) < 0) {
            _exit(2);
        }
        ::close(fds[1]);
        childMain();
        _exit(3);
    }
    ::close(fds[1]);
    char buf[256];
    (void)readAll(fds[0], buf, sizeof(buf));
    ::close(fds[0]);
    const bool sawReady = std::strstr(buf, "harness=ready") != nullptr;
    const bool sawWatchdog = std::strstr(buf, "harness=watchdog") != nullptr;
    EXPECT(sawReady);
    EXPECT(!sawWatchdog);
    if (!sawReady || sawWatchdog) {
        std::fprintf(stderr, "FAIL tests/unit/test_broker_watchdog.cpp harness %s output=%s\n", label, buf);
    }
    (void)::kill(pid, SIGKILL);
    int status = 0;
    EXPECT(reap(pid, &status));
}

void testCrashHarness()
{
    int fds[2];
    if (::pipe(fds) != 0) {
        EXPECT(false);
        return;
    }
    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(fds[0]);
        ::close(fds[1]);
        EXPECT(false);
        return;
    }
    if (pid == 0) {
        ::close(fds[0]);
        if (::dup2(fds[1], STDOUT_FILENO) < 0) {
            _exit(2);
        }
        ::close(fds[1]);
        crashChild();
        _exit(3);
    }
    ::close(fds[1]);
    char buf[256];
    (void)readAll(fds[0], buf, sizeof(buf));
    ::close(fds[0]);
    EXPECT(std::strstr(buf, "harness=ready") != nullptr);
    EXPECT(std::strstr(buf, "harness=watchdog") == nullptr);
    int status = 0;
    EXPECT(reap(pid, &status));
    EXPECT(WIFSIGNALED(status));
    if (WIFSIGNALED(status)) {
        EXPECT(WTERMSIG(status) == SIGABRT);
    }
}

} // namespace

int main()
{
    testSystemdPayloads();
    testWatchdogIntervalFromEnv();
    testProgressFeedsWatchdogAfterWork();
    testStopDoesNotFeedWatchdog();
    testNoBackgroundFeed();
    testIngestProgressPreservesForwarding();
    testSignalEpollIdleTimeout();
    testHangHarness(hangChild, "wait-hang");
    testHangHarness(workHangChild, "work-hang");
    testCrashHarness();

    if (g_failures != 0) {
        std::fprintf(stderr, "test_broker_watchdog: %d failure(s)\n", g_failures);
        return 1;
    }
    return 0;
}
