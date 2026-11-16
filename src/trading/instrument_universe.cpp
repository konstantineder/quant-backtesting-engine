#include "trading/instrument_universe.hpp"

#include <stdexcept>
#include <utility>

InstrumentUniverse::InstrumentUniverse(
    std::vector<InstrumentId> ids,
    const InstrumentRegistry& registry
): ids_(std::move(ids)), registry_(&registry) {
    if (ids_.empty()) {
        throw std::invalid_argument(
            "InstrumentUniverse requires at least one instrument"
        );
    }

    instruments_.reserve(ids_.size());
    index_by_id_.reserve(ids_.size());

    for (std::size_t offset = 0; offset < ids_.size(); ++offset) {
        const InstrumentId& id = ids_[offset];

        if (!registry_->contains(id.str())) {
            throw std::out_of_range(
                "Instrument is not registered: " + id.str()
            );
        }

        const auto [it, inserted] =
            index_by_id_.emplace(id, InstrumentIndex{offset});

        if (!inserted) {
            throw std::invalid_argument(
                "Duplicate instrument in universe: " + id.str()
            );
        }

        // Registry stores instruments in a node-based container, so the
        // address stays valid for the lifetime of the registry.
        instruments_.push_back(
            &registry_->getInstrument(id.str())
        );
    }
}

InstrumentUniverse InstrumentUniverse::fromInstrumentIds(
    std::span<const std::string> ordered_ids,
    const InstrumentRegistry& registry
) {
    std::vector<InstrumentId> ids;
    ids.reserve(ordered_ids.size());

    for (const std::string& id : ordered_ids) {
        ids.emplace_back(id);
    }

    return InstrumentUniverse{std::move(ids), registry};
}

void InstrumentUniverse::validateAlignment(
    std::span<const std::string> ordered_ids
) const {
    if (ordered_ids.size() != ids_.size()) {
        throw std::invalid_argument(
            "Instrument universe and market data differ in size"
        );
    }

    for (std::size_t offset = 0; offset < ids_.size(); ++offset) {
        if (ordered_ids[offset] != ids_[offset].str()) {
            throw std::invalid_argument(
                "Instrument universe is misaligned with market data at "
                "index " + std::to_string(offset) + ": expected " +
                ids_[offset].str() + ", got " + ordered_ids[offset]
            );
        }
    }
}

std::size_t InstrumentUniverse::checkedOffset(
    InstrumentIndex index
) const {
    const std::size_t offset = index.value();

    if (offset >= ids_.size()) {
        throw std::out_of_range(
            "InstrumentIndex out of range: " + std::to_string(offset)
        );
    }

    return offset;
}

InstrumentIndex InstrumentUniverse::index(
    const InstrumentId& id
) const {
    const auto it = index_by_id_.find(id);

    if (it == index_by_id_.end()) {
        throw std::out_of_range(
            "Instrument is not part of the universe: " + id.str()
        );
    }

    return it->second;
}

const InstrumentId& InstrumentUniverse::id(
    InstrumentIndex index
) const {
    return ids_[checkedOffset(index)];
}

const Instrument& InstrumentUniverse::instrument(
    InstrumentIndex index
) const {
    return *instruments_[checkedOffset(index)];
}
