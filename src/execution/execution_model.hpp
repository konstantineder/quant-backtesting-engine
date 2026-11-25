#pragma once

#include "market_data/historical_market_data.hpp"
#include "execution/fill.hpp"
#include "order/order.hpp"
#include "order/pending_order_book.hpp"

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <variant>
#include <vector>

// Deterministic market-order execution model.
//
// Orders submitted at bar t are eligible from bar t+1.
// Executes the entire remaining quantity at the current
// bar's open, without slippage, fees or liquidity limits.
//
// Produces fills without mutating the order book or account.
//
// PRECONDITION: instrument indices and market data
// columns are aligned.
class ExecutionModel {
public:
    std::vector<Fill> execute(
        const PendingOrderBook& pending_orders,
        const HistoricalMarketData& market_data,
        std::size_t bar_index
    ) const {
        const auto& execution_date = market_data.date(bar_index);

        std::vector<Fill> fills;

        fills.reserve(pending_orders.size());

        for (const auto& state : pending_orders.orders()) {

            if (state.isTerminal()) {
                continue;
            }

            // Workable from the bar after submission onwards. The equality
            // in the comparison is what rules out same-bar execution, and
            // it also makes bar 0 fill nothing at all: no order can have
            // been submitted before the first bar.
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

            const double execution_price =
                market_data.bar(bar_index, state.instrument().value()).open;

            // HistoricalMarketData already rejects non-finite and
            // non-positive prices when it is built, so this is redundant
            // today. It stays because a price is what turns an order into
            // cash leaving the account: should that guarantee ever move,
            // this must fail rather than mint a Fill from a NaN.
            if (!std::isfinite(execution_price) || execution_price <= 0.0) {
                throw std::invalid_argument(
                    "Execution requires a finite, positive open price"
                );
            }

            // The remaining quantity, not the ordered one: a partially
            // filled order executes only what is still outstanding.
            // fromOrder re-derives the id and the slot from the order
            // itself and checks the quantity against it, so a fill and the
            // order it belongs to cannot drift apart here.
            fills.push_back(Fill::fromOrder(
                state.order(),
                state.remainingQuantity(),
                execution_price,
                0.0,  // Fees are not this model's concern.
                execution_date,
                bar_index
            ));
        }

        return fills;
    }

};
