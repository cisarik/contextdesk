#include "broker/IdentityMatcher.h"

#include <cstdio>
#include <linux/input.h>
#include <string>

using contextdeck::broker::DeviceCandidate;
using contextdeck::broker::IdentityVerdict;
using contextdeck::broker::acceptedSource;
using contextdeck::broker::evaluateIdentity;
using contextdeck::broker::SourceTag;

namespace {

int g_failures = 0;

void expect(bool ok, const char *expr, int line)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL tests/unit/test_broker_identity.cpp:%d %s\n", line, expr);
        ++g_failures;
    }
}

#define EXPECT(expr) expect(static_cast<bool>(expr), #expr, __LINE__)

DeviceCandidate validIf00()
{
    DeviceCandidate c;
    c.vendorId = "046d";
    c.modelId = "c336";
    c.interfaceNum = "00";
    c.bustype = BUS_USB;
    c.name = "Logitech Gaming Keyboard G213";
    return c;
}

} // namespace

int main()
{
    const DeviceCandidate if00 = validIf00();
    EXPECT(evaluateIdentity(if00) == IdentityVerdict::AcceptIf00);
    EXPECT(acceptedSource(evaluateIdentity(if00)) == SourceTag::If00);

    DeviceCandidate if01 = if00;
    if01.interfaceNum = "01";
    EXPECT(evaluateIdentity(if01) == IdentityVerdict::AcceptIf01);
    EXPECT(acceptedSource(evaluateIdentity(if01)) == SourceTag::If01);

    DeviceCandidate ifaceBare = if00;
    ifaceBare.interfaceNum = "0";
    EXPECT(evaluateIdentity(ifaceBare) == IdentityVerdict::AcceptIf00);

    DeviceCandidate hexPrefix = if00;
    hexPrefix.vendorId = "0x046D";
    hexPrefix.modelId = "C336";
    EXPECT(evaluateIdentity(hexPrefix) == IdentityVerdict::AcceptIf00);

    DeviceCandidate virt = if00;
    virt.bustype = BUS_VIRTUAL;
    EXPECT(evaluateIdentity(virt) == IdentityVerdict::RejectVirtualBus);
    EXPECT(!acceptedSource(evaluateIdentity(virt)).has_value());

    DeviceCandidate clone = if00;
    clone.bustype = BUS_VIRTUAL;
    clone.vendorId = "046d";
    clone.modelId = "c336";
    clone.name = "Logitech Gaming Keyboard G213";
    EXPECT(evaluateIdentity(clone) == IdentityVerdict::RejectVirtualBus);

    DeviceCandidate named = if00;
    named.name = "ContextDeck G213 passthrough";
    EXPECT(evaluateIdentity(named) == IdentityVerdict::RejectNamePrefix);

    DeviceCandidate prefix = if00;
    prefix.name = "ContextDeck";
    EXPECT(evaluateIdentity(prefix) == IdentityVerdict::RejectNamePrefix);

    DeviceCandidate unresolved;
    EXPECT(evaluateIdentity(unresolved) == IdentityVerdict::RejectUnresolved);

    DeviceCandidate missingName = if00;
    missingName.name.reset();
    EXPECT(evaluateIdentity(missingName) == IdentityVerdict::RejectUnresolved);

    DeviceCandidate emptyName = if00;
    emptyName.name = "";
    EXPECT(evaluateIdentity(emptyName) == IdentityVerdict::RejectUnresolved);

    DeviceCandidate missingIface = if00;
    missingIface.interfaceNum.reset();
    EXPECT(evaluateIdentity(missingIface) == IdentityVerdict::RejectUnresolved);

    DeviceCandidate wrongProduct = if00;
    wrongProduct.modelId = "c337";
    EXPECT(evaluateIdentity(wrongProduct) == IdentityVerdict::RejectIdentityMismatch);

    DeviceCandidate wrongVendor = if00;
    wrongVendor.vendorId = "046e";
    EXPECT(evaluateIdentity(wrongVendor) == IdentityVerdict::RejectIdentityMismatch);

    DeviceCandidate iface02 = if00;
    iface02.interfaceNum = "02";
    EXPECT(evaluateIdentity(iface02) == IdentityVerdict::RejectIdentityMismatch);

    DeviceCandidate bluetooth = if00;
    bluetooth.bustype = BUS_BLUETOOTH;
    EXPECT(evaluateIdentity(bluetooth) == IdentityVerdict::AcceptIf00);

    if (g_failures != 0) {
        std::fprintf(stderr, "test_broker_identity: %d failure(s)\n", g_failures);
        return 1;
    }
    return 0;
}
