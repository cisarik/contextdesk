#include "broker/Selftest.h"

#include <cstdio>
#include <string_view>

#ifndef CONTEXTDECK_VERSION
#define CONTEXTDECK_VERSION "0.1.0"
#endif

int main(int argc, char **argv)
{
    if (argc > 1 && std::string_view(argv[1]) == "selftest") {
        return contextdeck::broker::runSelftest();
    }
    std::fprintf(stdout, "contextdeck-broker %s\n", CONTEXTDECK_VERSION);
    return 0;
}
