#pragma once

#include "instrument/instrument_registry.hpp"
#include "market_data/market_context.hpp"
#include "market_data/market_data.hpp"
#include "instrument/instrument.hpp"

class PricingEngine {
public:
    double price(
        const Instrument& instrument,
        const InstrumentRegistry& registry,
        const MarketContext& market
    ) const;

private:
    double priceEquity(
        const Equity& equity,
        const MarketContext& market
    ) const;

    double priceEuropeanOption(
        const EuropeanOption& option,
        const InstrumentRegistry& registry,
        const MarketContext& market
    ) const;
};
