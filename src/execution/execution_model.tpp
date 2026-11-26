#pragma once

// Definition of ExecutionModel<S, C>::execute. Included at the end of
// execution_model.hpp -- never compiled on its own and never included
// directly.

#include <cmath>
#include <stdexcept>
#include <variant>

template<SlippagePolicy S, CommissionPolicy C>
std::vector<Fill> ExecutionModel<S, C>::execute(
    const PendingOrderBook& pending_orders,
    const HistoricalMarketData& market_data,
    const InstrumentUniverse& universe,
    std::size_t bar_index
) const {
    // Validated up front, before anything is iterated, so an invalid bar
    // is rejected even when there is nothing to execute. Deferring it
    // until some order needed a price would make the same call throw or
    // not depending on the contents of the book, hiding a caller's
    // off-by-one on exactly the bars where no order happens to rest.
    const Date execution_date = market_data.date(bar_index);

    std::vector<Fill> fills;

    fills.reserve(pending_orders.size());

    for (const OrderState& state : pending_orders.orders()) {

        if (state.isTerminal()) {
            continue;
        }

        // Workable from the bar after submission onwards. The equality in
        // the comparison is what rules out same-bar execution, and it
        // also makes bar 0 fill nothing at all: no order can have been
        // submitted before the first bar.
        if (state.submittedBarIndex() >= bar_index) {
            continue;
        }

        // Unreachable while Order holds MarketOrder alone, and kept for
        // the alternative that comes next: a resting limit order this
        // model silently passed over would never fill, while the
        // OrderGenerator would go on seeing its quantity as pending.
        // Failing loudly beats deadlocking a slot.
        if (!std::holds_alternative<MarketOrder>(state.order())) {
            throw std::invalid_argument(
                "ExecutionModel supports market orders only"
            );
        }

        const double reference_price =
            market_data.bar(bar_index, state.instrument().value()).open;

        // HistoricalMarketData already rejects non-finite and non-positive
        // prices when it is built, so this is redundant today. It stays
        // because a price is what turns an order into cash leaving the
        // account: should that guarantee ever move, this must fail rather
        // than hand a NaN to the policies.
        if (!std::isfinite(reference_price) || reference_price <= 0.0) {
            throw std::invalid_argument(
                "Execution requires a finite, positive open price"
            );
        }

        // The remaining quantity, not the ordered one: a partially filled
        // order executes only what is still outstanding. It is also what
        // both policies see, and neither may change it.
        const double signed_quantity = state.remainingQuantity();

        const double execution_price =
            slippage_.executionPrice(reference_price, signed_quantity);

        // The multiplier is a property of the instrument, not of the order
        // or the market data. Taken through InstrumentTradingSpec, which
        // is the same single definition the Account's cash settlement
        // uses -- so a fee and the cash it is charged against cannot
        // disagree about what one contract trades.
        const double contract_multiplier =
            InstrumentTradingSpec::forInstrument(
                universe.instrument(state.instrument())
            ).contractMultiplier();

        // Charged on the executed price, so slippage is inside the
        // notional the fee is computed from. Only this becomes the Fill's
        // fee; the slippage is already in execution_price.
        const double fee = commission_.calculate(
            execution_price,
            signed_quantity,
            contract_multiplier
        );

        // fromOrder re-derives the id and the slot from the order itself
        // and checks the quantity against it, so a fill and the order it
        // belongs to cannot drift apart here.
        fills.push_back(Fill::fromOrder(
            state.order(),
            signed_quantity,
            execution_price,
            fee,
            execution_date,
            bar_index
        ));
    }

    return fills;
}
