#pragma once

#include <string>
#include <unordered_map>
#include "instrument/instrument.hpp"

class InstrumentRegistry {
public:
    void registerInstrument(const Instrument& instrument);

    const Instrument& getInstrument(
        const std::string& instrument_id
    ) const;

    bool contains(
        const std::string& instrument_id
    ) const;

    size_t size() const {
        return instruments_.size();
    }

private:
    std::unordered_map<std::string, Instrument> instruments_;
};