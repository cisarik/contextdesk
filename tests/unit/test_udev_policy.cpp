#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifndef CONTEXTDECK_SOURCE_DIR
#error CONTEXTDECK_SOURCE_DIR must be defined
#endif

namespace {

int g_failures = 0;

void expect(bool ok, const char *expr, int line)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL tests/unit/test_udev_policy.cpp:%d %s\n", line, expr);
        ++g_failures;
    }
}

#define EXPECT(expr) expect(static_cast<bool>(expr), #expr, __LINE__)

std::string readFile(const std::filesystem::path &path)
{
    std::ifstream in(path);
    EXPECT(in.good());
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

std::string trim(std::string_view line)
{
    std::size_t begin = 0;
    while (begin < line.size() && std::isspace(static_cast<unsigned char>(line[begin]))) {
        ++begin;
    }
    std::size_t end = line.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(line[end - 1]))) {
        --end;
    }
    return std::string(line.substr(begin, end - begin));
}

std::vector<std::string> ruleLines(const std::string &text)
{
    std::vector<std::string> lines;
    std::istringstream in(text);
    std::string raw;
    while (std::getline(in, raw)) {
        if (!raw.empty() && raw.back() == '\r') {
            raw.pop_back();
        }
        const std::string line = trim(raw);
        if (line.empty() || line.front() == '#') {
            continue;
        }
        lines.push_back(line);
    }
    return lines;
}

bool contains(std::string_view haystack, std::string_view needle)
{
    return haystack.find(needle) != std::string_view::npos;
}

bool anyLineContains(const std::vector<std::string> &lines, std::string_view needle)
{
    return std::any_of(lines.begin(), lines.end(), [&](const std::string &line) {
        return contains(line, needle);
    });
}

} // namespace

int main()
{
    const std::filesystem::path root{CONTEXTDECK_SOURCE_DIR};
    const std::filesystem::path udevDir = root / "packaging" / "udev";
    const std::filesystem::path guardPath = udevDir / "61-contextdeck-input-guard.rules";
    const std::filesystem::path grantPath = udevDir / "62-contextdeck-broker.rules";
    const std::filesystem::path uinputPath = udevDir / "99-contextdeck-broker-uinput.rules";
    const std::filesystem::path aclGuardPath = udevDir / "99-contextdeck-input-acl-guard.rules";

    EXPECT(std::filesystem::is_regular_file(guardPath));
    EXPECT(std::filesystem::is_regular_file(grantPath));
    EXPECT(std::filesystem::is_regular_file(uinputPath));
    EXPECT(std::filesystem::is_regular_file(aclGuardPath));
    EXPECT(uinputPath.filename().string() > std::string("73-seat-late.rules"));
    EXPECT(aclGuardPath.filename().string() > std::string("73-seat-late.rules"));

    const std::string guardText = readFile(guardPath);
    const std::string grantText = readFile(grantPath);
    const std::string uinputText = readFile(uinputPath);
    const std::string aclGuardText = readFile(aclGuardPath);
    const auto guard = ruleLines(guardText);
    const auto grant = ruleLines(grantText);
    const auto uinput = ruleLines(uinputText);
    const auto aclGuard = ruleLines(aclGuardText);

    EXPECT(guard.size() == 3);
    EXPECT(grant.size() == 1);
    EXPECT(uinput.size() == 1);
    EXPECT(aclGuard.size() == 3);

    EXPECT(guard[0] == R"(SUBSYSTEM=="input", SUBSYSTEMS=="usb", ATTRS{idVendor}=="046d", ATTRS{idProduct}=="c336", TAG-="uaccess")");
    EXPECT(guard[1] == R"(KERNEL=="port", TAG-="uaccess")");
    EXPECT(guard[2] == R"(KERNEL=="i2c-[0-9]*", TAG-="uaccess")");
    EXPECT(!anyLineContains(guard, "hidraw"));
    EXPECT(!anyLineContains(guard, "OWNER"));
    EXPECT(!anyLineContains(guard, R"(TAG+="uaccess")"));
    EXPECT(!anyLineContains(guard, "uinput"));

    EXPECT(grant[0] == R"(SUBSYSTEM=="input", SUBSYSTEMS=="usb", ATTRS{idVendor}=="046d", ATTRS{idProduct}=="c336", KERNEL=="event*", GROUP="contextdeck-broker", MODE="0660")");
    EXPECT(!anyLineContains(grant, "uinput"));
    EXPECT(!anyLineContains(grant, "setfacl"));
    EXPECT(!anyLineContains(grant, "OWNER"));
    EXPECT(!anyLineContains(grant, R"(TAG+="uaccess")"));
    EXPECT(!anyLineContains(grant, "hidraw"));
    EXPECT(!anyLineContains(grant, R"(KERNEL=="port")"));
    EXPECT(!anyLineContains(grant, "i2c"));

    EXPECT(uinput[0] == R"(ACTION=="add|change", KERNEL=="uinput", SUBSYSTEM=="misc", RUN+="/usr/bin/setfacl -m u:contextdeck-broker:rw /dev/uinput")");
    EXPECT(!anyLineContains(uinput, "OWNER"));
    EXPECT(!anyLineContains(uinput, "GROUP="));
    EXPECT(!anyLineContains(uinput, "MODE="));
    EXPECT(!anyLineContains(uinput, "TAG"));
    EXPECT(!anyLineContains(uinput, "hidraw"));
    EXPECT(!anyLineContains(uinput, R"(KERNEL=="event)"));
    EXPECT(!anyLineContains(uinput, R"(KERNEL=="port")"));
    EXPECT(!anyLineContains(uinput, "i2c"));
    EXPECT(!contains(uinput[0], "uaccess"));

    EXPECT(aclGuard[0] == R"(ACTION=="add|change", SUBSYSTEM=="input", SUBSYSTEMS=="usb", ATTRS{idVendor}=="046d", ATTRS{idProduct}=="c336", KERNEL=="event*", RUN+="/usr/bin/setfacl -b %N")");
    EXPECT(aclGuard[1] == R"(ACTION=="add|change", KERNEL=="port", RUN+="/usr/bin/setfacl -b %N")");
    EXPECT(aclGuard[2] == R"(ACTION=="add|change", KERNEL=="i2c-[0-9]*", RUN+="/usr/bin/setfacl -b %N")");
    EXPECT(!anyLineContains(aclGuard, "hidraw"));
    EXPECT(!anyLineContains(aclGuard, "uinput"));
    EXPECT(!anyLineContains(aclGuard, "/dev/uinput"));
    EXPECT(!anyLineContains(aclGuard, "OWNER"));
    EXPECT(!anyLineContains(aclGuard, "GROUP="));
    EXPECT(!anyLineContains(aclGuard, "MODE="));
    EXPECT(!anyLineContains(aclGuard, R"(TAG+="uaccess")"));
    EXPECT(!anyLineContains(aclGuard, R"(TAG-="uaccess")"));
    EXPECT(!anyLineContains(aclGuard, "contextdeck-broker"));
    EXPECT(!anyLineContains(aclGuard, "setfacl -m"));
    EXPECT(!anyLineContains(aclGuard, "/dev/%k"));
    EXPECT(anyLineContains(aclGuard, R"(KERNEL=="event*)"));
    EXPECT(anyLineContains(aclGuard, R"(KERNEL=="port")"));
    EXPECT(anyLineContains(aclGuard, R"(KERNEL=="i2c-[0-9]*")"));
    EXPECT(!anyLineContains(uinput, "setfacl -b"));
    EXPECT(!anyLineContains(guard, "setfacl"));
    EXPECT(!anyLineContains(grant, "setfacl"));

    if (g_failures != 0) {
        std::fprintf(stderr, "test_udev_policy: %d failure(s)\n", g_failures);
        return 1;
    }
    return 0;
}
