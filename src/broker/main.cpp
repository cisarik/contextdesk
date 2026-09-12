#include "broker/EventLoop.h"
#include "broker/IdleWait.h"
#include "broker/Logger.h"
#include "broker/ProductionRuntime.h"
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

int runBroker()
{
    contextdeck::broker::Logger logger;
    std::fprintf(stderr, "contextdeck-broker %s\n", CONTEXTDECK_VERSION);
    logger.state("idle");

    contextdeck::broker::SignalEpollWait wait;
    if (!wait.valid()) {
        logger.error("event-loop-setup-failed");
        return 1;
    }

    contextdeck::broker::KeyLedger ledger;
    contextdeck::broker::RealLifecycleSink sink;
    sink.setWait(&wait);
    contextdeck::broker::EvdevSource if00(contextdeck::broker::SourceTag::If00);
    contextdeck::broker::EvdevSource if01(contextdeck::broker::SourceTag::If01);
    contextdeck::broker::Acquisition acquisition(sink, if00, if01, ledger, logger, &sink);
    contextdeck::broker::UdevDeviceEnumerator enumerator;
    contextdeck::broker::ProductionArmControl control(enumerator, if00, if01, acquisition, logger, &wait);
    contextdeck::broker::ForwardingEngine engine(sink, ledger, logger);
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

    contextdeck::broker::BrokerLoopWork work(ipc, engine, if00, if01, control, logger, &sink);
    contextdeck::broker::SystemdWatchdog watchdog;
    contextdeck::broker::EventLoop loop(wait, watchdog, contextdeck::broker::watchdogFeedTimeoutMs(), &work);
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
    return runBroker();
}
