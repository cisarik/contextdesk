#include "broker/Acquisition.h"
#include "broker/DeviceEnumerator.h"
#include "broker/EvdevGrabber.h"
#include "broker/EvdevSource.h"
#include "broker/ProductionRuntime.h"
#include "broker/RealSink.h"

#include <algorithm>
#include <cstdio>
#include <libevdev/libevdev.h>
#include <linux/input.h>
#include <string>
#include <vector>

using contextdeck::broker::Acquisition;
using contextdeck::broker::DeviceCandidate;
using contextdeck::broker::EnumerateError;
using contextdeck::broker::EnumeratedNode;
using contextdeck::broker::EvdevGrabber;
using contextdeck::broker::EvdevSource;
using contextdeck::broker::G213Selection;
using contextdeck::broker::IDeviceEnumerator;
using contextdeck::broker::ILifecycleSink;
using contextdeck::broker::KeyLedger;
using contextdeck::broker::Logger;
using contextdeck::broker::ProductionArmControl;
using contextdeck::broker::RealLifecycleSink;
using contextdeck::broker::SinkCapabilities;
using contextdeck::broker::SourceTag;
using contextdeck::broker::passthroughCapabilities;
using contextdeck::broker::selectG213Pair;

namespace {

int g_failures = 0;

void expect(bool ok, const char *expr, int line)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL tests/unit/test_broker_production.cpp:%d %s\n", line, expr);
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

DeviceCandidate validIf01()
{
    DeviceCandidate c = validIf00();
    c.interfaceNum = "01";
    return c;
}

EnumeratedNode nodeFor(const DeviceCandidate &candidate, const char *devnode)
{
    EnumeratedNode node;
    node.candidate = candidate;
    node.devnode = devnode;
    node.usbAncestry = true;
    return node;
}

class CountingEnumerator final : public IDeviceEnumerator {
public:
    EnumerateError error = EnumerateError::Empty;
    G213Selection selection;
    mutable int resolves = 0;

    EnumerateError resolve(G213Selection &out) const override
    {
        ++resolves;
        if (error == EnumerateError::Ok) {
            out = selection;
        }
        return error;
    }
};

class RecordingSink final : public ILifecycleSink {
public:
    bool createVirtual() override
    {
        ++creates;
        created = true;
        destroyed = false;
        return createOk;
    }
    void destroyVirtual() override
    {
        ++destroys;
        destroyed = true;
        created = false;
    }

    bool createOk = true;
    bool created = false;
    bool destroyed = false;
    int creates = 0;
    int destroys = 0;
};

bool containsCode(const std::vector<uint16_t> &codes, uint16_t code)
{
    return std::find(codes.begin(), codes.end(), code) != codes.end();
}

} // namespace

int main()
{
    {
        G213Selection selected;
        std::vector<EnumeratedNode> nodes;
        nodes.push_back(nodeFor(validIf00(), "/dev/input/event3"));
        nodes.push_back(nodeFor(validIf01(), "/dev/input/event9"));
        EXPECT(selectG213Pair(nodes, selected) == EnumerateError::Ok);
        EXPECT(selected.if00.candidate.interfaceNum == std::string("00"));
        EXPECT(selected.if01.candidate.interfaceNum == std::string("01"));
        EXPECT(selected.if00.devnode == "/dev/input/event3");
        EXPECT(selected.if01.devnode == "/dev/input/event9");
    }

    {
        G213Selection selected;
        std::vector<EnumeratedNode> nodes;
        nodes.push_back(nodeFor(validIf00(), "/dev/input/event3"));
        EXPECT(selectG213Pair(nodes, selected) == EnumerateError::MissingIf01);
    }

    {
        G213Selection selected;
        std::vector<EnumeratedNode> nodes;
        nodes.push_back(nodeFor(validIf01(), "/dev/input/event9"));
        EXPECT(selectG213Pair(nodes, selected) == EnumerateError::MissingIf00);
    }

    {
        G213Selection selected;
        std::vector<EnumeratedNode> nodes;
        nodes.push_back(nodeFor(validIf00(), "/dev/input/event3"));
        nodes.push_back(nodeFor(validIf00(), "/dev/input/event99"));
        nodes.push_back(nodeFor(validIf01(), "/dev/input/event4"));
        EXPECT(selectG213Pair(nodes, selected) == EnumerateError::DuplicateIf00);
    }

    {
        G213Selection selected;
        std::vector<EnumeratedNode> nodes;
        nodes.push_back(nodeFor(validIf00(), "/dev/input/event3"));
        nodes.push_back(nodeFor(validIf01(), "/dev/input/event4"));
        nodes.push_back(nodeFor(validIf01(), "/dev/input/event8"));
        EXPECT(selectG213Pair(nodes, selected) == EnumerateError::DuplicateIf01);
    }

    {
        G213Selection selected;
        EXPECT(selectG213Pair({}, selected) == EnumerateError::Empty);
    }

    {
        G213Selection selected;
        EnumeratedNode virt = nodeFor(validIf00(), "/dev/input/event3");
        virt.candidate.bustype = BUS_VIRTUAL;
        EnumeratedNode if01 = nodeFor(validIf01(), "/dev/input/event4");
        EXPECT(selectG213Pair({virt, if01}, selected) == EnumerateError::MissingIf00);
    }

    {
        G213Selection selected;
        EnumeratedNode named = nodeFor(validIf00(), "/dev/input/event3");
        named.candidate.name = "ContextDeck G213 passthrough";
        EnumeratedNode if01 = nodeFor(validIf01(), "/dev/input/event4");
        EXPECT(selectG213Pair({named, if01}, selected) == EnumerateError::MissingIf00);
    }

    {
        G213Selection selected;
        EnumeratedNode wrong = nodeFor(validIf00(), "/dev/input/event3");
        wrong.candidate.modelId = "c337";
        EnumeratedNode if01 = nodeFor(validIf01(), "/dev/input/event4");
        EXPECT(selectG213Pair({wrong, if01}, selected) == EnumerateError::MissingIf00);
    }

    {
        G213Selection selected;
        DeviceCandidate iface02 = validIf00();
        iface02.interfaceNum = "02";
        EnumeratedNode extra = nodeFor(iface02, "/dev/input/event5");
        std::vector<EnumeratedNode> nodes{nodeFor(validIf00(), "/dev/input/event3"),
                                         nodeFor(validIf01(), "/dev/input/event4"), extra};
        EXPECT(selectG213Pair(nodes, selected) == EnumerateError::Ok);
    }

    {
        G213Selection selected;
        EnumeratedNode noUsb = nodeFor(validIf00(), "/dev/input/event3");
        noUsb.usbAncestry = false;
        EXPECT(selectG213Pair({noUsb, nodeFor(validIf01(), "/dev/input/event4")}, selected)
               == EnumerateError::MissingIf00);
    }

    {
        const SinkCapabilities caps = passthroughCapabilities();
        EXPECT(containsCode(caps.keyCodes, 59));
        EXPECT(containsCode(caps.keyCodes, 88));
        EXPECT(containsCode(caps.keyCodes, 113));
        EXPECT(containsCode(caps.keyCodes, 164));
        EXPECT(!caps.ledCodes.empty());
        EXPECT(!caps.mscCodes.empty());
        EXPECT(EvdevGrabber::create(nullptr) == nullptr);
        ::libevdev *empty = libevdev_new();
        EXPECT(empty != nullptr);
        EXPECT(EvdevGrabber::create(empty) == nullptr);
        libevdev_free(empty);
    }

    {
        Logger logger;
        KeyLedger ledger;
        RecordingSink sink;
        EvdevSource if00(SourceTag::If00);
        EvdevSource if01(SourceTag::If01);
        Acquisition acq(sink, if00, if01, ledger, logger);
        CountingEnumerator enumerator;
        ProductionArmControl control(enumerator, if00, if01, acq, logger, nullptr);
        EXPECT(!control.armed());
        EXPECT(!if00.opened());
        EXPECT(!if01.opened());
        EXPECT(if00.fd() < 0);
        EXPECT(if01.fd() < 0);
        EXPECT(enumerator.resolves == 0);
        EXPECT(sink.creates == 0);
        EXPECT(!control.arm());
        EXPECT(!control.armed());
        EXPECT(enumerator.resolves == 1);
        EXPECT(!if00.opened());
        EXPECT(!if01.opened());
        EXPECT(sink.creates == 0);
    }

    {
        Logger logger;
        KeyLedger ledger;
        RecordingSink sink;
        EvdevSource if00(SourceTag::If00);
        EvdevSource if01(SourceTag::If01);
        Acquisition acq(sink, if00, if01, ledger, logger);
        CountingEnumerator enumerator;
        enumerator.error = EnumerateError::Ok;
        enumerator.selection.if00 = nodeFor(validIf00(), "/tmp/contextdeck-no-such-if00");
        enumerator.selection.if01 = nodeFor(validIf01(), "/tmp/contextdeck-no-such-if01");
        ProductionArmControl control(enumerator, if00, if01, acq, logger, nullptr);
        EXPECT(!control.arm());
        EXPECT(!control.armed());
        EXPECT(!if00.opened());
        EXPECT(!if01.opened());
        EXPECT(sink.creates == 1);
        EXPECT(sink.destroys == 1);
        EXPECT(!sink.created);
        control.disarm();
        EXPECT(!control.armed());
        EXPECT(!if00.opened());
        EXPECT(!if01.opened());
    }

    {
        Logger logger;
        KeyLedger ledger;
        RecordingSink sink;
        EvdevSource if00(SourceTag::If00);
        EvdevSource if01(SourceTag::If01);
        Acquisition acq(sink, if00, if01, ledger, logger);
        CountingEnumerator enumerator;
        enumerator.error = EnumerateError::Ok;
        enumerator.selection.if00 = nodeFor(validIf00(), "/dev/null");
        enumerator.selection.if01 = nodeFor(validIf01(), "/dev/null");
        ProductionArmControl control(enumerator, if00, if01, acq, logger, nullptr);
        EXPECT(!control.arm());
        EXPECT(!control.armed());
        EXPECT(!if00.opened());
        EXPECT(!if01.opened());
        EXPECT(if00.fd() < 0);
        EXPECT(if01.fd() < 0);
        EXPECT(sink.destroys == 1);
    }

    {
        EvdevSource source(SourceTag::If00);
        EXPECT(!source.opened());
        EXPECT(source.fd() < 0);
        source.bind("/tmp/contextdeck-no-such-evdev", validIf00());
        EXPECT(!source.opened());
        EXPECT(!source.openSource());
        EXPECT(!source.opened());
        EXPECT(source.fd() < 0);
        EXPECT(!source.claimSource());
        source.unclaimSource();
        source.closeSource();
        EXPECT(!source.opened());
        EXPECT(!source.hadError());
    }

    {
        RealLifecycleSink sink(passthroughCapabilities());
        EXPECT(!sink.created());
        sink.destroyVirtual();
        EXPECT(!sink.created());
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "test_broker_production: %d failure(s)\n", g_failures);
        return 1;
    }
    return 0;
}
