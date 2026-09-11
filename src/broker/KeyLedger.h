#pragma once

#include "broker/Types.h"

#include <array>
#include <cstdint>
#include <set>
#include <vector>

namespace contextdeck::broker {

class KeyLedger {
public:
    void onKey(SourceTag source, uint16_t code, int32_t value);

    // Balanced synthetic releases, newest press first. Clears synthetic state only.
    std::vector<LedgerEmit> disarmSynthetic();

    // Update physical keys for one source after SYN_DROPPED. Emits the minimum
    // sink-reconciling press/release pair; never treats those as commands.
    std::vector<LedgerEmit> reconcileAfterSync(SourceTag source, const std::set<uint16_t> &physicalNow);

    bool physicalDown(SourceTag source, uint16_t code) const;
    bool syntheticDown(uint16_t code) const;
    BrokerCounters counters() const;

private:
    void recordSyntheticPress(uint16_t code);
    void clearSynthetic(uint16_t code);
    bool anyPhysicalDown(uint16_t code) const;

    std::array<std::set<uint16_t>, 2> physical_{};
    std::set<uint16_t> synthetic_{};
    std::vector<uint16_t> syntheticOrder_{};
    uint64_t droppedSync_{0};
};

} // namespace contextdeck::broker
