#include "instrument/instrument_registry.hpp"
#include <magic_enum/magic_enum.hpp>
#include <stdexcept>
#include <type_traits>

void InstrumentRegistry::registerInstrument(const Instrument& instrument) {
    const std::string id = std::visit(
    [](const auto& inst) {
        return inst.instrumentID();
    },
    instrument);
    auto [it, inserted] = instruments_.emplace(id, instrument);

    if (!inserted) {
        throw std::runtime_error(
            "Instrument with ID " + id + " is already registered."
        );
    }
}

const Instrument& InstrumentRegistry::getInstrument(const std::string& instrument_id) const {
    return instruments_.at(instrument_id);
}

bool InstrumentRegistry::contains(const std::string& instrument_id) const {
    return instruments_.find(instrument_id) != instruments_.end();
}
