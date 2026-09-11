#include "broker/EventLoop.h"
#include "broker/IdleWait.h"
#include "broker/Logger.h"
#include "broker/Selftest.h"
#include "broker/Watchdog.h"

#include <cstdio>
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

    contextdeck::broker::SystemdWatchdog watchdog;
    contextdeck::broker::EventLoop loop(wait, watchdog, contextdeck::broker::watchdogFeedTimeoutMs());
    loop.run();
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
