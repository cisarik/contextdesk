#include "broker/DeviceEnumerator.h"

#include <cctype>
#include <cstring>
#include <libudev.h>
#include <optional>
#include <string_view>

namespace contextdeck::broker {
namespace {

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

bool isEventSysname(const char *sysname)
{
    if (sysname == nullptr || std::strncmp(sysname, "event", 5) != 0) {
        return false;
    }
    const char *rest = sysname + 5;
    if (*rest == '\0') {
        return false;
    }
    for (const char *p = rest; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9') {
            return false;
        }
    }
    return true;
}

bool isInputEventDevnode(const char *devnode)
{
    if (devnode == nullptr) {
        return false;
    }
    static constexpr char kPrefix[] = "/dev/input/event";
    if (std::strncmp(devnode, kPrefix, sizeof(kPrefix) - 1) != 0) {
        return false;
    }
    const char *rest = devnode + (sizeof(kPrefix) - 1);
    if (*rest == '\0') {
        return false;
    }
    for (const char *p = rest; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9') {
            return false;
        }
    }
    return true;
}

bool g213UsbAncestry(udev_device *device, std::optional<std::string> &vendor, std::optional<std::string> &product,
                     std::optional<std::string> &iface)
{
    if (device == nullptr) {
        return false;
    }
    udev_device *usb = udev_device_get_parent_with_subsystem_devtype(device, "usb", "usb_device");
    if (usb == nullptr) {
        return false;
    }
    const char *idVendor = udev_device_get_sysattr_value(usb, "idVendor");
    const char *idProduct = udev_device_get_sysattr_value(usb, "idProduct");
    const auto parsedVendor = idVendor != nullptr ? parseHexId(idVendor) : std::nullopt;
    const auto parsedProduct = idProduct != nullptr ? parseHexId(idProduct) : std::nullopt;
    if (!parsedVendor.has_value() || !parsedProduct.has_value()) {
        return false;
    }
    if (*parsedVendor != kG213VendorId || *parsedProduct != kG213ProductId) {
        return false;
    }
    if (idVendor != nullptr) {
        vendor = std::string(idVendor);
    }
    if (idProduct != nullptr) {
        product = std::string(idProduct);
    }
    udev_device *usbIface = udev_device_get_parent_with_subsystem_devtype(device, "usb", "usb_interface");
    if (usbIface != nullptr) {
        const char *num = udev_device_get_sysattr_value(usbIface, "bInterfaceNumber");
        if (num != nullptr && num[0] != '\0') {
            iface = std::string(num);
        }
    }
    return true;
}

} // namespace

const char *enumerateErrorClass(EnumerateError error)
{
    switch (error) {
    case EnumerateError::Ok:
        return "enumerate-ok";
    case EnumerateError::Empty:
        return "enumerate-empty";
    case EnumerateError::MissingIf00:
        return "enumerate-missing-if00";
    case EnumerateError::MissingIf01:
        return "enumerate-missing-if01";
    case EnumerateError::DuplicateIf00:
        return "enumerate-duplicate-if00";
    case EnumerateError::DuplicateIf01:
        return "enumerate-duplicate-if01";
    }
    return "enumerate-empty";
}

EnumerateError selectG213Pair(const std::vector<EnumeratedNode> &nodes, G213Selection &out)
{
    const EnumeratedNode *if00 = nullptr;
    const EnumeratedNode *if01 = nullptr;
    for (const EnumeratedNode &node : nodes) {
        if (!node.usbAncestry) {
            continue;
        }
        const std::optional<SourceTag> tag = acceptedSource(evaluateIdentity(node.candidate));
        if (!tag.has_value()) {
            continue;
        }
        if (*tag == SourceTag::If00) {
            if (if00 != nullptr) {
                return EnumerateError::DuplicateIf00;
            }
            if00 = &node;
            continue;
        }
        if (if01 != nullptr) {
            return EnumerateError::DuplicateIf01;
        }
        if01 = &node;
    }
    if (if00 == nullptr && if01 == nullptr) {
        return EnumerateError::Empty;
    }
    if (if00 == nullptr) {
        return EnumerateError::MissingIf00;
    }
    if (if01 == nullptr) {
        return EnumerateError::MissingIf01;
    }
    out.if00 = *if00;
    out.if01 = *if01;
    return EnumerateError::Ok;
}

std::vector<EnumeratedNode> UdevDeviceEnumerator::scan() const
{
    std::vector<EnumeratedNode> nodes;
    udev *udev = udev_new();
    if (udev == nullptr) {
        return nodes;
    }
    udev_enumerate *enumerate = udev_enumerate_new(udev);
    if (enumerate == nullptr) {
        udev_unref(udev);
        return nodes;
    }
    udev_enumerate_add_match_subsystem(enumerate, "input");
    udev_enumerate_scan_devices(enumerate);
    udev_list_entry *entry = udev_enumerate_get_list_entry(enumerate);
    udev_list_entry *item = nullptr;
    udev_list_entry_foreach(item, entry)
    {
        const char *syspath = udev_list_entry_get_name(item);
        udev_device *device = udev_device_new_from_syspath(udev, syspath);
        if (device == nullptr) {
            continue;
        }
        const char *sysname = udev_device_get_sysname(device);
        const char *devnode = udev_device_get_devnode(device);
        if (!isEventSysname(sysname) || !isInputEventDevnode(devnode)) {
            udev_device_unref(device);
            continue;
        }

        EnumeratedNode node;
        node.devnode = devnode;
        node.candidate = candidateFromUdevDevice(device);
        std::optional<std::string> usbVendor;
        std::optional<std::string> usbProduct;
        std::optional<std::string> usbIface;
        node.usbAncestry = g213UsbAncestry(device, usbVendor, usbProduct, usbIface);
        if (!node.candidate.vendorId.has_value() && usbVendor.has_value()) {
            node.candidate.vendorId = usbVendor;
        }
        if (!node.candidate.modelId.has_value() && usbProduct.has_value()) {
            node.candidate.modelId = usbProduct;
        }
        if (!node.candidate.interfaceNum.has_value() && usbIface.has_value()) {
            node.candidate.interfaceNum = usbIface;
        }
        nodes.push_back(std::move(node));
        udev_device_unref(device);
    }
    udev_enumerate_unref(enumerate);
    udev_unref(udev);
    return nodes;
}

EnumerateError UdevDeviceEnumerator::resolve(G213Selection &out) const
{
    return selectG213Pair(scan(), out);
}

} // namespace contextdeck::broker
