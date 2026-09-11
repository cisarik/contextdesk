#pragma once

#include "broker/Types.h"

#include <optional>
#include <string_view>

struct udev_device;

namespace contextdeck::broker {

IdentityVerdict evaluateIdentity(const DeviceCandidate &candidate);

std::optional<SourceTag> acceptedSource(IdentityVerdict verdict);

const char *identityVerdictClass(IdentityVerdict verdict);

// Reads ID_VENDOR_ID, ID_MODEL_ID, ID_USB_INTERFACE_NUM via libudev.
// Does not open /dev/input. Not invoked by this exchange's tests or binary.
DeviceCandidate candidateFromUdev(udev_device *device, uint16_t bustype, std::string_view evdevName);

} // namespace contextdeck::broker
