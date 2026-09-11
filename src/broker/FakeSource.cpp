#include "broker/FakeSource.h"

namespace contextdeck::broker {

FakeSource::FakeSource(SourceTag tag)
    : tag_(tag)
{
}

void FakeSource::enqueue(InputEvent event)
{
    queue_.push_back(event);
}

void FakeSource::setPhysicalKeys(std::set<uint16_t> physical)
{
    physical_ = std::move(physical);
}

std::optional<InputEvent> FakeSource::read()
{
    if (queue_.empty()) {
        return std::nullopt;
    }
    const InputEvent event = queue_.front();
    queue_.pop_front();
    if (event.kind == InputEvent::Kind::Key) {
        if (event.value == 1) {
            physical_.insert(event.code);
        } else if (event.value == 0) {
            physical_.erase(event.code);
        }
    }
    return event;
}

} // namespace contextdeck::broker
