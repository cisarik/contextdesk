#include "broker/KeyLedger.h"

#include <algorithm>

namespace contextdeck::broker {

void KeyLedger::recordSyntheticPress(uint16_t code)
{
    if (synthetic_.insert(code).second) {
        syntheticOrder_.push_back(code);
    }
}

void KeyLedger::clearSynthetic(uint16_t code)
{
    if (synthetic_.erase(code) == 0) {
        return;
    }
    auto it = std::find(syntheticOrder_.begin(), syntheticOrder_.end(), code);
    if (it != syntheticOrder_.end()) {
        syntheticOrder_.erase(it);
    }
}

bool KeyLedger::anyPhysicalDown(uint16_t code) const
{
    return physical_[0].count(code) != 0 || physical_[1].count(code) != 0;
}

void KeyLedger::onKey(SourceTag source, uint16_t code, int32_t value)
{
    auto &phys = physical_[static_cast<size_t>(sourceIndex(source))];
    if (value == 2) {
        return;
    }
    if (value == 1) {
        phys.insert(code);
        recordSyntheticPress(code);
        return;
    }
    if (value == 0) {
        phys.erase(code);
        if (!anyPhysicalDown(code)) {
            clearSynthetic(code);
        }
        return;
    }
}

std::vector<LedgerEmit> KeyLedger::disarmSynthetic()
{
    std::vector<LedgerEmit> emitted;
    emitted.reserve(syntheticOrder_.size());
    while (!syntheticOrder_.empty()) {
        const uint16_t code = syntheticOrder_.back();
        syntheticOrder_.pop_back();
        synthetic_.erase(code);
        emitted.push_back(LedgerEmit{code, 0});
    }
    return emitted;
}

std::vector<LedgerEmit> KeyLedger::reconcileAfterSync(SourceTag source, const std::set<uint16_t> &physicalNow)
{
    ++droppedSync_;
    auto &phys = physical_[static_cast<size_t>(sourceIndex(source))];
    const std::set<uint16_t> previous = phys;
    phys = physicalNow;

    std::vector<LedgerEmit> emitted;

    for (uint16_t code : previous) {
        if (physicalNow.count(code) != 0) {
            continue;
        }
        if (anyPhysicalDown(code)) {
            continue;
        }
        if (synthetic_.count(code) == 0) {
            continue;
        }
        clearSynthetic(code);
        emitted.push_back(LedgerEmit{code, 0});
    }

    for (uint16_t code : physicalNow) {
        if (synthetic_.count(code) != 0) {
            continue;
        }
        recordSyntheticPress(code);
        emitted.push_back(LedgerEmit{code, 1});
    }

    return emitted;
}

bool KeyLedger::physicalDown(SourceTag source, uint16_t code) const
{
    return physical_[static_cast<size_t>(sourceIndex(source))].count(code) != 0;
}

bool KeyLedger::syntheticDown(uint16_t code) const
{
    return synthetic_.count(code) != 0;
}

BrokerCounters KeyLedger::counters() const
{
    BrokerCounters values;
    values.droppedSync = droppedSync_;
    values.keysDownPhysical = physical_[0].size() + physical_[1].size();
    values.keysDownSynthetic = synthetic_.size();
    return values;
}

} // namespace contextdeck::broker
