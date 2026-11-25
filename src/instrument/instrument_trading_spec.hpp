#pragma once

#include "instrument/instrument.hpp"
#include "instrument/instrument_universe.hpp"

#include <vector>

// How an instrument settles when it trades.
enum class SettlementConvention {
    // Cash changes hands at execution for the full notional: equities and
    // European option premium.
    PREMIUM,

    // Daily variation margin against a margin account -- futures. Not
    // supported yet, and rejected explicitly rather than silently
    // mis-settled as premium.
    MARGIN
};

// Everything the account needs to turn a fill into cash, and nothing about
// pricing or market data. A lightweight value type: one per universe slot,
// copied freely.
class InstrumentTradingSpec {
public:
    InstrumentTradingSpec(
        double contract_multiplier,
        SettlementConvention settlement
    );

    // Derives the spec from a registered instrument. Throws
    // std::invalid_argument for instruments whose settlement this engine
    // does not support (futures).
    static InstrumentTradingSpec forInstrument(
        const Instrument& instrument
    );

    double contractMultiplier() const noexcept {
        return contract_multiplier_;
    }

    SettlementConvention settlement() const noexcept {
        return settlement_;
    }

private:
    double contract_multiplier_;
    SettlementConvention settlement_;
};

// Specs for every universe slot, aligned with InstrumentIndex. Built once
// per backtest and shared read-only.
std::vector<InstrumentTradingSpec> makeTradingSpecs(
    const InstrumentUniverse& universe
);

// Cash flow produced by a fill, as seen from the account:
// negative when buying (cash leaves), positive when selling.
//
// This is a pure calculation -- deliberately a free function, so that
// settlement arithmetic can be tested and reasoned about without any
// account state. The contract multiplier is applied here and NOWHERE else
// on the cash path.
double settlementCashFlow(
    const InstrumentTradingSpec& spec,
    double signed_quantity,
    double unit_price,
    double fee
);
