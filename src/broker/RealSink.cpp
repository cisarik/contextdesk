#include "broker/RealSink.h"
#include "broker/Types.h"

#include <algorithm>
#include <libevdev/libevdev-uinput.h>
#include <linux/input.h>
#include <string>

namespace contextdeck::broker {
namespace {

void enableCodes(::libevdev *dev, uint16_t type, const std::vector<uint16_t> &codes)
{
    if (codes.empty()) {
        return;
    }
    libevdev_enable_event_type(dev, type);
    for (uint16_t code : codes) {
        libevdev_enable_event_code(dev, type, code, nullptr);
    }
}

} // namespace

SinkCapabilities unionSourceCapabilities(const SinkCapabilities &if00, const SinkCapabilities &if01)
{
    SinkCapabilities out;
    out.keyCodes = if00.keyCodes;
    out.keyCodes.insert(out.keyCodes.end(), if01.keyCodes.begin(), if01.keyCodes.end());
    std::sort(out.keyCodes.begin(), out.keyCodes.end());
    out.keyCodes.erase(std::unique(out.keyCodes.begin(), out.keyCodes.end()), out.keyCodes.end());
    out.ledCodes = if00.ledCodes;
    out.mscCodes = if00.mscCodes;
    out.mscCodes.insert(out.mscCodes.end(), if01.mscCodes.begin(), if01.mscCodes.end());
    std::sort(out.mscCodes.begin(), out.mscCodes.end());
    out.mscCodes.erase(std::unique(out.mscCodes.begin(), out.mscCodes.end()), out.mscCodes.end());
    return out;
}

SinkCapabilities passthroughCapabilities()
{
    SinkCapabilities out;
    out.keyCodes.reserve(KEY_CNT);
    for (unsigned code = 0; code < KEY_CNT; ++code) {
        out.keyCodes.push_back(static_cast<uint16_t>(code));
    }
    out.ledCodes.reserve(LED_CNT);
    for (unsigned code = 0; code < LED_CNT; ++code) {
        out.ledCodes.push_back(static_cast<uint16_t>(code));
    }
    out.mscCodes.reserve(MSC_CNT);
    for (unsigned code = 0; code < MSC_CNT; ++code) {
        out.mscCodes.push_back(static_cast<uint16_t>(code));
    }
    return out;
}

RealSink::RealSink(::libevdev *templateDevice, ::libevdev_uinput *uinputDevice)
    : templateDevice_(templateDevice)
    , uinputDevice_(uinputDevice)
{
}

std::unique_ptr<RealSink> RealSink::create(const SinkCapabilities &capabilities)
{
    ::libevdev *dev = libevdev_new();
    if (dev == nullptr) {
        return nullptr;
    }
    const std::string name(kVirtualDeviceName);
    libevdev_set_name(dev, name.c_str());
    libevdev_set_id_bustype(dev, BUS_VIRTUAL);
    libevdev_set_id_vendor(dev, kVirtualVendorId);
    libevdev_set_id_product(dev, kVirtualProductId);
    libevdev_set_id_version(dev, kVirtualVersion);
    enableCodes(dev, EV_KEY, capabilities.keyCodes);
    enableCodes(dev, EV_LED, capabilities.ledCodes);
    enableCodes(dev, EV_MSC, capabilities.mscCodes);
    // EV_REP is intentionally not enabled.

    ::libevdev_uinput *uidev = nullptr;
    const int err = libevdev_uinput_create_from_device(dev, LIBEVDEV_UINPUT_OPEN_MANAGED, &uidev);
    if (err != 0) {
        libevdev_free(dev);
        return nullptr;
    }
    return std::unique_ptr<RealSink>(new RealSink(dev, uidev));
}

RealSink::~RealSink()
{
    if (uinputDevice_ != nullptr) {
        libevdev_uinput_destroy(uinputDevice_);
        uinputDevice_ = nullptr;
    }
    if (templateDevice_ != nullptr) {
        libevdev_free(templateDevice_);
        templateDevice_ = nullptr;
    }
}

void RealSink::writeEvent(uint16_t type, uint16_t code, int32_t value)
{
    if (uinputDevice_ == nullptr) {
        return;
    }
    (void)libevdev_uinput_write_event(uinputDevice_, type, code, value);
}

void RealSink::flushSyn()
{
    writeEvent(EV_SYN, SYN_REPORT, 0);
}

} // namespace contextdeck::broker
