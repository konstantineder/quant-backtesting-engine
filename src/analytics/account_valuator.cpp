#include "analytics/account_valuator.hpp"

#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace {

// Folds one held position into the running valuation. `unit_price` is a
// price per contract unit; the multiplier is applied here.
void accumulatePosition(
    AccountValuation& valuation,
    const PositionState& position,
    double unit_price,
    double contract_multiplier
) {
    if (!std::isfinite(unit_price) || unit_price < 0.0) {
        throw std::invalid_argument(
            "Instrument price must be finite and non-negative"
        );
    }

    const double scaled_quantity =
        position.quantity() * contract_multiplier;

    const double market_value = scaled_quantity * unit_price;

    const double unrealized_pnl =
        scaled_quantity * (unit_price - position.averagePrice());

    if (!std::isfinite(market_value) ||
        !std::isfinite(unrealized_pnl)) {
        throw std::overflow_error("Non-finite position valuation");
    }

    valuation.position_market_value += market_value;
    valuation.unrealized_pnl += unrealized_pnl;

    if (!std::isfinite(valuation.position_market_value) ||
        !std::isfinite(valuation.unrealized_pnl)) {
        throw std::overflow_error("Non-finite valuation aggregate");
    }
}

void finalize(AccountValuation& valuation) {
    valuation.equity =
        valuation.cash + valuation.position_market_value;

    valuation.net_pnl =
        valuation.realized_pnl +
        valuation.unrealized_pnl -
        valuation.total_fees;

    if (!std::isfinite(valuation.equity) ||
        !std::isfinite(valuation.net_pnl)) {
        throw std::overflow_error("Non-finite account valuation");
    }
}

AccountValuation startValuation(const Account& account) {
    AccountValuation valuation{};
    valuation.cash = account.cash();
    valuation.realized_pnl = account.realizedPnL();
    valuation.total_fees = account.totalFees();
    return valuation;
}

} // namespace

AccountValuation AccountValuator::evaluate(
    const Account& account,
    const InstrumentUniverse& universe,
    const MarketContext& market,
    const PricingEngine& pricer
) const {
    if (account.size() != universe.size()) {
        throw std::invalid_argument(
            "Account and instrument universe differ in size"
        );
    }

    AccountValuation valuation = startValuation(account);

    for (std::size_t offset = 0; offset < account.size(); ++offset) {
        const auto index = InstrumentIndex{offset};
        const PositionState& position = account.position(index);

        // Flat slots need no price: an untouched instrument must not make
        // the whole valuation fail on missing market data.
        if (position.isFlat()) {
            continue;
        }

        accumulatePosition(
            valuation,
            position,
            pricer.price(
                universe.instrument(index),
                universe.registry(),
                market
            ),
            account.spec(index).contractMultiplier()
        );
    }

    finalize(valuation);

    return valuation;
}

AccountValuation AccountValuator::evaluateWithPrices(
    const Account& account,
    std::span<const double> unit_prices
) const {
    if (account.size() != unit_prices.size()) {
        throw std::invalid_argument(
            "Account and price vector differ in size"
        );
    }

    AccountValuation valuation = startValuation(account);

    for (std::size_t offset = 0; offset < account.size(); ++offset) {
        const auto index = InstrumentIndex{offset};
        const PositionState& position = account.position(index);

        if (position.isFlat()) {
            continue;
        }

        accumulatePosition(
            valuation,
            position,
            unit_prices[offset],
            account.spec(index).contractMultiplier()
        );
    }

    finalize(valuation);

    return valuation;
}

void AccountValuator::priceUniverse(
    const InstrumentUniverse& universe,
    const MarketContext& market,
    const PricingEngine& pricer,
    std::vector<double>& out
) const {
    out.resize(universe.size());

    for (std::size_t offset = 0; offset < universe.size(); ++offset) {
        out[offset] = pricer.price(
            universe.instrument(InstrumentIndex{offset}),
            universe.registry(),
            market
        );
    }
}
