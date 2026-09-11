#pragma once

#include "broker/ISource.h"

#include <deque>
#include <set>

namespace contextdeck::broker {

class FakeSource final : public ISource {
public:
    explicit FakeSource(SourceTag tag);

    SourceTag tag() const override { return tag_; }
    std::optional<InputEvent> read() override;
    std::set<uint16_t> keysDown() const override { return physical_; }

    void enqueue(InputEvent event);
    void setPhysicalKeys(std::set<uint16_t> physical);

private:
    SourceTag tag_;
    std::deque<InputEvent> queue_;
    std::set<uint16_t> physical_;
};

} // namespace contextdeck::broker
