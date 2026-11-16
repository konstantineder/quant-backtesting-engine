#pragma once

#include "instrument/instrument.hpp"
#include "instrument/instrument_registry.hpp"
#include "trading/instrument_id.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

// Immutable mapping between registry instrument ids and dense,
// universe-local indices.
//
// Built once per backtest and then shared read-only between workers. The
// instrument pointers are resolved at construction, so hot loops reach an
// Instrument by index without touching a string or a hash table. The
// referenced registry must outlive the universe and must not have
// instruments removed (InstrumentRegistry offers no removal).
class InstrumentUniverse {
public:
    InstrumentUniverse(
        std::vector<InstrumentId> ids,
        const InstrumentRegistry& registry
    );

    // Builds a universe whose indices match the given order by
    // construction -- pass HistoricalMarketData::instrumentIds() to align
    // universe slots with market data columns.
    static InstrumentUniverse fromInstrumentIds(
        std::span<const std::string> ordered_ids,
        const InstrumentRegistry& registry
    );

    // Immutable by contract: copyable and movable so a universe can be
    // handed to workers, but never reseated after construction.
    InstrumentUniverse(const InstrumentUniverse&) = default;
    InstrumentUniverse(InstrumentUniverse&&) = default;
    InstrumentUniverse& operator=(const InstrumentUniverse&) = delete;
    InstrumentUniverse& operator=(InstrumentUniverse&&) = delete;
    ~InstrumentUniverse() = default;

    // Throws unless `ordered_ids` matches this universe slot for slot.
    void validateAlignment(
        std::span<const std::string> ordered_ids
    ) const;

    std::size_t size() const noexcept { return ids_.size(); }

    bool contains(const InstrumentId& id) const noexcept {
        return index_by_id_.find(id) != index_by_id_.end();
    }

    // Hash lookup -- for setup code, not for per-bar loops.
    InstrumentIndex index(const InstrumentId& id) const;

    const InstrumentId& id(InstrumentIndex index) const;

    // O(1), allocation-free, no string comparison: the hot path.
    const Instrument& instrument(InstrumentIndex index) const;

    const std::vector<InstrumentId>& ids() const noexcept {
        return ids_;
    }

    const InstrumentRegistry& registry() const noexcept {
        return *registry_;
    }

private:
    std::size_t checkedOffset(InstrumentIndex index) const;

    std::vector<InstrumentId> ids_;
    std::vector<const Instrument*> instruments_;
    std::unordered_map<InstrumentId, InstrumentIndex> index_by_id_;
    const InstrumentRegistry* registry_;
};
