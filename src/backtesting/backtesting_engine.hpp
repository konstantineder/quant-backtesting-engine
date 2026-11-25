#pragma once

#include "backtesting/backtest_config.hpp"
#include "backtesting/backtest_result.hpp"
#include "instrument/instrument_universe.hpp"
#include "strategy/strategy.hpp"
#include "market_data/historical_market_data.hpp"
#include "account/account.hpp"
#include "instrument/instrument_trading_spec.hpp"
#include "order/pending_order_book.hpp"
#include "order/order_generator.hpp"

#include <stdexcept>

class BacktestEngine {
public:
    explicit BacktestEngine(BacktestConfig config): config_(config) {
        if (config.initial_cash < 0) {
            std::invalid_argument("Initial cash needs to be non-negative.");
        }

        if (config.bars_per_year <= 0) {
            std::invalid_argument("Num bars per year need to be strictly positive.");
        }
    }

    BacktestResult run(
        Strategy& strategy,
        const InstrumentUniverse& universe,
        const HistoricalMarketData& market_data
    ) const {
        auto account = Account(config_.initial_cash, makeTradingSpecs(universe));
        

        for (std::size_t bar = 0; bar < market_data.numDates(); ++bar) {


            // Phase 1: OPEN
            // Execute pending orders.
            for (auto& order : )

            // Phase 2: OPEN
            // Apply fills to Account and PendingOrderBook.
            // Record fills in ExecutionLog.

            // Phase 3: OPEN
            // Remove terminal orders.

            // Phase 4: CLOSE
            // Compute instrument prices.

            // Phase 5: CLOSE
            // Evaluate Account.

            // Phase 6: CLOSE
            // Evaluate Strategy.

            // Phase 7: CLOSE
            // Generate and apply order instructions.

            // Phase 8: CLOSE
            // Record date and equity.
        }
    }

private:
    BacktestConfig config_;
};