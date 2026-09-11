#include "broker/IdentityMatcher.h"

#include <cctype>
#include <libudev.h>
#include <linux/input.h>
#include <string>

namespace contextdeck::broker {
namespace {

bool isUsbOrBluetooth(uint16_t bustype)
{
    return bustype == BUS_USB || bustype == BUS_BLUETOOTH;
}

std::optional<uint16_t> parseHexId(std::string_view text)
{
    if (text.size() >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        text.remove_prefix(2);
    }
    if (text.empty() || text.size() > 4) {
        return std::nullopt;
    }
    unsigned value = 0;
    for (char c : text) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (!std::isxdigit(uc)) {
            return std::nullopt;
        }
        value *= 16;
        if (uc >= '0' && uc <= '9') {
            value += static_cast<unsigned>(uc - '0');
        } else if (uc >= 'a' && uc <= 'f') {
            value += static_cast<unsigned>(uc - 'a' + 10);
        } else {
            value += static_cast<unsigned>(uc - 'A' + 10);
        }
        if (value > 0xffffu) {
            return std::nullopt;
        }
    }
    return static_cast<uint16_t>(value);
}

std::optional<int> parseInterfaceNumber(std::string_view text)
{
    if (text.empty() || text.size() > 2) {
        return std::nullopt;
    }
    int value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        value = value * 10 + (c - '0');
    }
    return value;
}

std::optional<std::string> readProperty(udev_device *device, const char *key)
{
    if (device == nullptr || key == nullptr) {
        return std::nullopt;
    }
    const char *value = udev_device_get_property_value(device, key);
    if (value != nullptr && value[0] != '\0') {
        return std::string(value);
    }
    udev_device *iface = udev_device_get_parent_with_subsystem_devtype(device, "usb", "usb_interface");
    if (iface != nullptr) {
        value = udev_device_get_property_value(iface, key);
        if (value != nullptr && value[0] != '\0') {
            return std::string(value);
        }
    }
    udev_device *usb = udev_device_get_parent_with_subsystem_devtype(device, "usb", "usb_device");
    if (usb != nullptr) {
        value = udev_device_get_property_value(usb, key);
        if (value != nullptr && value[0] != '\0') {
            return std::string(value);
        }
    }
    return std::nullopt;
}

} // namespace

IdentityVerdict evaluateIdentity(const DeviceCandidate &candidate)
{
    if (!candidate.vendorId.has_value() || !candidate.modelId.has_value()
        || !candidate.interfaceNum.has_value() || !candidate.bustype.has_value()
        || !candidate.name.has_value() || candidate.name->empty()) {
        return IdentityVerdict::RejectUnresolved;
    }

    const uint16_t bus = *candidate.bustype;
    if (bus == BUS_VIRTUAL) {
        return IdentityVerdict::RejectVirtualBus;
    }
    if (!isUsbOrBluetooth(bus)) {
        return IdentityVerdict::RejectIdentityMismatch;
    }

    const std::string_view name = *candidate.name;
    if (name.size() >= kForbiddenNamePrefix.size()
        && name.substr(0, kForbiddenNamePrefix.size()) == kForbiddenNamePrefix) {
        return IdentityVerdict::RejectNamePrefix;
    }

    const std::optional<uint16_t> vendor = parseHexId(*candidate.vendorId);
    const std::optional<uint16_t> product = parseHexId(*candidate.modelId);
    const std::optional<int> iface = parseInterfaceNumber(*candidate.interfaceNum);
    if (!vendor.has_value() || !product.has_value() || !iface.has_value()) {
        return IdentityVerdict::RejectUnresolved;
    }
    if (*vendor != kG213VendorId || *product != kG213ProductId) {
        return IdentityVerdict::RejectIdentityMismatch;
    }
    if (*iface == 0) {
        return IdentityVerdict::AcceptIf00;
    }
    if (*iface == 1) {
        return IdentityVerdict::AcceptIf01;
    }
    return IdentityVerdict::RejectIdentityMismatch;
}

std::optional<SourceTag> acceptedSource(IdentityVerdict verdict)
{
    if (verdict == IdentityVerdict::AcceptIf00) {
        return SourceTag::If00;
    }
    if (verdict == IdentityVerdict::AcceptIf01) {
        return SourceTag::If01;
    }
    return std::nullopt;
}

const char *identityVerdictClass(IdentityVerdict verdict)
{
    switch (verdict) {
    case IdentityVerdict::AcceptIf00:
        return "accept-if00";
    case IdentityVerdict::AcceptIf01:
        return "accept-if01";
    case IdentityVerdict::RejectVirtualBus:
        return "reject-virtual-bus";
    case IdentityVerdict::RejectNamePrefix:
        return "reject-name-prefix";
    case IdentityVerdict::RejectUnresolved:
        return "reject-unresolved";
    case IdentityVerdict::RejectIdentityMismatch:
        return "reject-identity-mismatch";
    }
    return "reject-unresolved";
}

DeviceCandidate candidateFromUdev(udev_device *device, uint16_t bustype, std::string_view evdevName)
{
    DeviceCandidate candidate;
    candidate.bustype = bustype;
    if (!evdevName.empty()) {
        candidate.name = std::string(evdevName);
    }
    candidate.vendorId = readProperty(device, "ID_VENDOR_ID");
    candidate.modelId = readProperty(device, "ID_MODEL_ID");
    candidate.interfaceNum = readProperty(device, "ID_USB_INTERFACE_NUM");
    return candidate;
}

} // namespace contextdeck::broker
