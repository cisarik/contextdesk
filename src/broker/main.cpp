#include "broker/EventLoop.h"
#include "broker/IdleWait.h"
#include "broker/Logger.h"
#include "broker/Selftest.h"
#include "broker/SessionIpc.h"
#include "broker/Watchdog.h"

#include <cstdio>
#include <cstdlib>
#include <string_view>

#ifndef CONTEXTDECK_VERSION
#define CONTEXTDECK_VERSION "0.1.0"
#endif

namespace {

int runIdleBroker()
{
    contextdeck::broker::Logger logger;
    std::fprintf(stderr, "contextdeck-broker %s\n", CONTEXTDECK_VERSION);
    logger.state("idle");

    contextdeck::broker::SignalEpollWait wait;
    if (!wait.valid()) {
        logger.error("event-loop-setup-failed");
        return 1;
    }

    class FailClosedSink final : public contextdeck::broker::ILifecycleSink {
    public:
        bool createVirtual() override { return false; }
        void destroyVirtual() override {}
    };
    class FailClosedSource final : public contextdeck::broker::ILifecycleSource {
    public:
        explicit FailClosedSource(contextdeck::broker::SourceTag tag)
            : tag_(tag)
        {
        }
        contextdeck::broker::SourceTag tag() const override { return tag_; }
        bool openSource() override { return false; }
        bool claimSource() override { return false; }
        void unclaimSource() override {}
        void closeSource() override {}

    private:
        contextdeck::broker::SourceTag tag_;
    };

    contextdeck::broker::KeyLedger ledger;
    FailClosedSink sink;
    FailClosedSource if00(contextdeck::broker::SourceTag::If00);
    FailClosedSource if01(contextdeck::broker::SourceTag::If01);
    contextdeck::broker::Acquisition acquisition(sink, if00, if01, ledger, logger);
    contextdeck::broker::AcquisitionArmControl control(acquisition);
    contextdeck::broker::KernelPeerCredentials creds;
    contextdeck::broker::LogindSeatAuthorizer auth;
    const char *socketPath = std::getenv("CONTEXTDECK_BROKER_SOCKET");
    if (socketPath == nullptr || socketPath[0] == '\0') {
        socketPath = contextdeck::broker::kDefaultBrokerSocket;
    }
    contextdeck::broker::SessionIpc ipc(wait, control, auth, creds, logger);
    if (!ipc.listen(socketPath)) {
        logger.error("ipc-listen-failed");
        return 1;
    }

    contextdeck::broker::SystemdWatchdog watchdog;
    contextdeck::broker::EventLoop loop(wait, watchdog, contextdeck::broker::watchdogFeedTimeoutMs(), &ipc);
    loop.run();
    ipc.shutdown();
    watchdog.notifyStopping();
    logger.state("stopped");
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc > 1) {
        const std::string_view command(argv[1]);
        if (command == "selftest") {
            return contextdeck::broker::runSelftest();
        }
        if (command == "watchdog-selftest") {
            return contextdeck::broker::runWatchdogSelftest();
        }
        std::fprintf(stderr, "contextdeck-broker: error=unknown-command\n");
        return 1;
    }
    return runIdleBroker();
}
