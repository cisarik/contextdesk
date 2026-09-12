#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "broker/EvdevSource.h"

#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <libevdev/libevdev.h>
#include <linux/input.h>
#include <unistd.h>

namespace contextdeck::broker {
namespace {

std::optional<InputEvent> translateEvent(const input_event &ev)
{
    InputEvent out;
    switch (ev.type) {
    case EV_KEY:
        out.kind = InputEvent::Kind::Key;
        out.code = ev.code;
        out.value = ev.value;
        return out;
    case EV_LED:
        out.kind = InputEvent::Kind::Led;
        out.code = ev.code;
        out.value = ev.value;
        return out;
    case EV_MSC:
        out.kind = InputEvent::Kind::Msc;
        out.code = ev.code;
        out.value = ev.value;
        return out;
    case EV_SYN:
        if (ev.code == SYN_DROPPED) {
            out.kind = InputEvent::Kind::SynDropped;
            return out;
        }
        if (ev.code == SYN_REPORT) {
            out.kind = InputEvent::Kind::SynReport;
            return out;
        }
        return std::nullopt;
    default:
        return std::nullopt;
    }
}

} // namespace

EvdevSource::EvdevSource(SourceTag tag)
    : tag_(tag)
{
}

EvdevSource::~EvdevSource()
{
    unclaimSource();
    releaseOpened();
}

void EvdevSource::bind(std::string devnode, DeviceCandidate identity)
{
    if (opened_) {
        return;
    }
    path_ = std::move(devnode);
    identity_ = std::move(identity);
    failed_ = false;
}

bool EvdevSource::matchesBoundIdentity() const
{
    if (dev_ == nullptr) {
        return false;
    }
    DeviceCandidate live = identity_;
    live.bustype = static_cast<uint16_t>(libevdev_get_id_bustype(dev_));
    const char *name = libevdev_get_name(dev_);
    if (name != nullptr && name[0] != '\0') {
        live.name = std::string(name);
    }
    char vendor[5];
    char product[5];
    std::snprintf(vendor, sizeof(vendor), "%04x", static_cast<unsigned>(libevdev_get_id_vendor(dev_)) & 0xffffu);
    std::snprintf(product, sizeof(product), "%04x", static_cast<unsigned>(libevdev_get_id_product(dev_)) & 0xffffu);
    live.vendorId = vendor;
    live.modelId = product;
    const std::optional<SourceTag> tag = acceptedSource(evaluateIdentity(live));
    return tag.has_value() && *tag == tag_;
}

void EvdevSource::releaseOpened()
{
    grabber_.reset();
    if (dev_ != nullptr) {
        libevdev_free(dev_);
        dev_ = nullptr;
    }
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    opened_ = false;
}

bool EvdevSource::openSource()
{
    failed_ = false;
    if (opened_) {
        return true;
    }
    if (path_.empty()) {
        return false;
    }
    fd_ = ::open(path_.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd_ < 0) {
        return false;
    }
    const int err = libevdev_new_from_fd(fd_, &dev_);
    if (err != 0 || dev_ == nullptr) {
        ::close(fd_);
        fd_ = -1;
        dev_ = nullptr;
        return false;
    }
    if (!matchesBoundIdentity()) {
        libevdev_free(dev_);
        dev_ = nullptr;
        ::close(fd_);
        fd_ = -1;
        return false;
    }
    opened_ = true;
    return true;
}

bool EvdevSource::claimSource()
{
    if (!opened_ || dev_ == nullptr) {
        return false;
    }
    if (!grabber_) {
        grabber_ = EvdevGrabber::create(dev_);
        if (!grabber_) {
            return false;
        }
    }
    return grabber_->grab();
}

void EvdevSource::unclaimSource()
{
    if (grabber_) {
        grabber_->ungrab();
    }
}

void EvdevSource::closeSource()
{
    unclaimSource();
    releaseOpened();
}

std::optional<InputEvent> EvdevSource::read()
{
    if (!opened_ || dev_ == nullptr) {
        return std::nullopt;
    }
    input_event ev{};
    int rc = libevdev_next_event(dev_, LIBEVDEV_READ_FLAG_NORMAL, &ev);
    if (rc == -EAGAIN) {
        return std::nullopt;
    }
    if (rc < 0) {
        failed_ = true;
        return std::nullopt;
    }
    if (rc == LIBEVDEV_READ_STATUS_SYNC) {
        do {
            rc = libevdev_next_event(dev_, LIBEVDEV_READ_FLAG_SYNC, &ev);
        } while (rc == LIBEVDEV_READ_STATUS_SYNC);
        if (rc < 0 && rc != -EAGAIN) {
            failed_ = true;
        }
        InputEvent dropped;
        dropped.kind = InputEvent::Kind::SynDropped;
        return dropped;
    }
    if (rc != LIBEVDEV_READ_STATUS_SUCCESS) {
        return std::nullopt;
    }
    return translateEvent(ev);
}

std::set<uint16_t> EvdevSource::keysDown() const
{
    std::set<uint16_t> down;
    if (dev_ == nullptr) {
        return down;
    }
    for (unsigned code = 0; code < KEY_CNT; ++code) {
        if (libevdev_has_event_code(dev_, EV_KEY, static_cast<int>(code)) != 0
            && libevdev_get_event_value(dev_, EV_KEY, static_cast<int>(code)) != 0) {
            down.insert(static_cast<uint16_t>(code));
        }
    }
    return down;
}

SinkCapabilities measureEvdevCapabilities(const ::libevdev *dev, bool includeLeds)
{
    SinkCapabilities out;
    if (dev == nullptr) {
        return out;
    }
    for (int code = 0; code < KEY_CNT; ++code) {
        if (libevdev_has_event_code(dev, EV_KEY, code) != 0) {
            out.keyCodes.push_back(static_cast<uint16_t>(code));
        }
    }
    if (includeLeds) {
        for (int code = 0; code < LED_CNT; ++code) {
            if (libevdev_has_event_code(dev, EV_LED, code) != 0) {
                out.ledCodes.push_back(static_cast<uint16_t>(code));
            }
        }
    }
    for (int code = 0; code < MSC_CNT; ++code) {
        if (libevdev_has_event_code(dev, EV_MSC, code) != 0) {
            out.mscCodes.push_back(static_cast<uint16_t>(code));
        }
    }
    return out;
}

SinkCapabilities EvdevSource::measuredCapabilities() const
{
    return measureEvdevCapabilities(dev_, tag_ == SourceTag::If00);
}

bool EvdevSource::writeLed(uint16_t code, int32_t value)
{
    if (!opened_ || dev_ == nullptr || tag_ != SourceTag::If00) {
        return false;
    }
    const enum libevdev_led_value led = value != 0 ? LIBEVDEV_LED_ON : LIBEVDEV_LED_OFF;
    return libevdev_kernel_set_led_value(dev_, code, led) == 0;
}

} // namespace contextdeck::broker
