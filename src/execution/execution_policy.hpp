#pragma once

#include "execution/fill.hpp"
#include "instrument/instrument_universe.hpp"
#include "market_data/historical_market_data.hpp"
#include "order/pending_order_book.hpp"

#include <concepts>
#include <cstddef>
#include <vector>

// What the BacktestEngine needs of an execution model, and nothing more.
//
// The engine knows this contract alone: not slippage, not commission, not
// how the fills were arrived at. Any model satisfying it can be handed to
// run(), including one composed of policies that did not exist when the
// engine was written.
//
// The universe is part of the signature because a model may need an
// instrument's properties -- a contract multiplier, for a fee charged on
// notional -- which are in neither the order nor the market data.
//
// Returning the fills by value, and const on the model, is what makes a
// bar's execution inspectable before it is applied: a conforming model
// cannot have mutated the book or the account to produce them.
template<typename T>
concept ExecutionPolicy = requires(
    const T& model,
    const PendingOrderBook& pending_orders,
    const HistoricalMarketData& market_data,
    const InstrumentUniverse& universe,
    std::size_t bar_index
) {
    {
        model.execute(
            pending_orders,
            market_data,
            universe,
            bar_index
        )
    } -> std::same_as<std::vector<Fill>>;
};
