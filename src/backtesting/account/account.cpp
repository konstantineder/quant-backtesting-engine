#include "backtesting/account/account.hpp"

#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

Account::Account(
    double initial_cash,
    std::vector<InstrumentTradingSpec> specs
): initial_cash_(initial_cash),
   cash_(initial_cash),
   specs_(std::move(specs)) {
    if (!std::isfinite(initial_cash_) || initial_cash_ < 0.0) {
        throw std::invalid_argument(
            "Initial cash must be finite and non-negative"
        );
    }

    if (specs_.empty()) {
        throw std::invalid_argument(
            "Account requires at least one tradable instrument"
        );
    }

    // Dense and pre-sized: no allocation happens per fill afterwards.
    positions_.assign(specs_.size(), PositionState{});
}

Account Account::forUniverse(
    double initial_cash,
    const InstrumentUniverse& universe
) {
    return Account{initial_cash, makeTradingSpecs(universe)};
}

std::size_t Account::checkedOffset(InstrumentIndex index) const {
    const std::size_t offset = index.value();

    if (offset >= positions_.size()) {
        throw std::out_of_range(
            "InstrumentIndex is not part of this account: " +
            std::to_string(offset)
        );
    }

    return offset;
}

const PositionState& Account::position(InstrumentIndex index) const {
    return positions_[checkedOffset(index)];
}

const InstrumentTradingSpec& Account::spec(
    InstrumentIndex index
) const {
    return specs_[checkedOffset(index)];
}

void Account::applyFill(const Fill& fill) {
    const std::size_t offset = checkedOffset(fill.instrument());
    const InstrumentTradingSpec& spec = specs_[offset];

    // 1. Settlement, before anything is mutated.
    const double cash_flow = settlementCashFlow(
        spec,
        fill.quantity(),
        fill.price(),
        fill.fee()
    );

    const double new_cash = cash_ + cash_flow;
    const double new_total_fees = total_fees_ + fill.fee();

    if (!std::isfinite(new_cash) || !std::isfinite(new_total_fees)) {
        throw std::overflow_error(
            "Account cash cannot be represented"
        );
    }

    // 2. Position update on a copy: three doubles, no allocation.
    PositionState updated = positions_[offset];
    const double realized_delta = updated.applyFill(
        fill.quantity(),
        fill.price(),
        spec.contractMultiplier()
    );

    const double new_realized_pnl = realized_pnl_ + realized_delta;

    if (!std::isfinite(new_realized_pnl)) {
        throw std::overflow_error(
            "Account realized PnL cannot be represented"
        );
    }

    // 3. Commit -- nothing below can throw.
    positions_[offset] = updated;
    cash_ = new_cash;
    total_fees_ = new_total_fees;
    realized_pnl_ = new_realized_pnl;
}
