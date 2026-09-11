#pragma once

#include "broker/IdentityMatcher.h"

#include <string>
#include <vector>

namespace contextdeck::broker {

enum class EnumerateError {
    Ok,
    Empty,
    MissingIf00,
    MissingIf01,
    DuplicateIf00,
    DuplicateIf01,
};

struct EnumeratedNode {
    DeviceCandidate candidate;
    std::string devnode;
    bool usbAncestry = false;
};

struct G213Selection {
    EnumeratedNode if00;
    EnumeratedNode if01;
};

const char *enumerateErrorClass(EnumerateError error);

// Identity is USB ancestry + interface number. devnode is only the later open
// handle and is never treated as a durable id (eventN is not identity).
EnumerateError selectG213Pair(const std::vector<EnumeratedNode> &nodes, G213Selection &out);

class IDeviceEnumerator {
public:
    virtual ~IDeviceEnumerator() = default;
    virtual EnumerateError resolve(G213Selection &out) const = 0;
};

// libudev scan of input event nodes. Reads sysfs/uevent only; does not open
// /dev/input or /dev/uinput. Production ARM calls this; unit tests must not.
class UdevDeviceEnumerator final : public IDeviceEnumerator {
public:
    std::vector<EnumeratedNode> scan() const;
    EnumerateError resolve(G213Selection &out) const override;
};

} // namespace contextdeck::broker
