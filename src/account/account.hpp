#pragma once

#include "execution/fill.hpp"
#include "instrument/instrument_id.hpp"
#include "instrument/instrument_trading_spec.hpp"
#include "position/position_state.hpp"

#include <cstddef>
#include <span>
#include <vector>

// Cash and signed positions for one backtest worker.
//
// Deliberately knows nothing about pricing, market data or valuation: it
// turns fills into cash and position state, nothing more. Pricing a
// position is the valuator's job, which keeps mark-to-market out of the
// accounting path.
//
// Positions live in a dense vector indexed by InstrumentIndex: position
// access and fill processing are O(1) and allocation-free after
// construction.
//
// Mutable state, so one Account per worker -- never shared between
// parallel backtests.
//
// Accounting conventions (and how double counting is avoided):
//   * cash absorbs the full settlement notional and every fee.
//   * realizedPnL() is GROSS of fees and already includes the contract
//     multiplier, which is applied only inside the settlement and
//     PositionState calculations.
//   * totalFees() is the single place fees are aggregated.
//   * No margin or buying-power check: cash may go negative when shorting
//     or over-investing. Risk limits belong in a separate layer.
class Account {
public:
    Account(
        double initial_cash,
        std::vector<InstrumentTradingSpec> specs
    );

    // Convenience factory deriving the specs from the universe.
    static Account forUniverse(
        double initial_cash,
        const InstrumentUniverse& universe
    );

    // Settles the fill into cash and folds it into the position.
    //
    // Strong exception safety: every value is computed and validated before
    // anything is committed, so a rejected fill leaves the account exactly
    // as it was.
    void applyFill(const Fill& fill);

    double cash() const noexcept { return cash_; }
    double initialCash() const noexcept { return initial_cash_; }
    double totalFees() const noexcept { return total_fees_; }

    // Aggregate realized PnL across all positions, gross of fees.
    double realizedPnL() const noexcept { return realized_pnl_; }

    const PositionState& position(InstrumentIndex index) const;

    std::span<const PositionState> positions() const noexcept {
        return positions_;
    }

    const InstrumentTradingSpec& spec(InstrumentIndex index) const;

    std::size_t size() const noexcept { return positions_.size(); }

private:
    std::size_t checkedOffset(InstrumentIndex index) const;

    double initial_cash_;
    double cash_;
    double total_fees_ = 0.0;
    double realized_pnl_ = 0.0;

    std::vector<PositionState> positions_;
    std::vector<InstrumentTradingSpec> specs_;
};
