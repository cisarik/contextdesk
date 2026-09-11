#include <cstdio>
#include <string_view>

#ifndef CONTEXTDECK_VERSION
#define CONTEXTDECK_VERSION "0.1.0"
#endif

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    std::fprintf(stdout, "contextdeck-broker %s\n", CONTEXTDECK_VERSION);
    return 0;
}
