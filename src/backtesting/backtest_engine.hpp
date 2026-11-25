#pragma once

#include "backtesting/backtest_config.hpp"
#include "backtesting/backtest_result.hpp"
#include "market_data/historical_market_data.hpp"
#include "strategy/strategy.hpp"
#include "instrument/instrument_universe.hpp"

// Drives one backtest: walks the bars, lets the strategy decide, turns its
// decisions into orders and executes them.
//
// WHAT ONE BAR LOOKS LIKE, and why in this order:
//
//   at the open    1. execute the orders that are already working
//                  2. settle the fills into the Account and the book, and
//                     record them in the ExecutionLog
//                  3. drop the orders that are now terminal
//   at the close   4. price the universe
//                  5. mark the Account to market
//                  6. ask the strategy for a target
//                  7. reconcile that target into cancellations and new
//                     orders, and apply them to the book
//                  8. record the bar's date and equity
//
// The split is the whole point. A strategy sees a bar's close in step 6,
// and the orders it asks for in step 7 are submitted stamped with that bar
// -- so the ExecutionModel cannot fill them before the NEXT bar's open.
// Decision and execution are therefore separated by a price the strategy
// had not seen when it decided, which is what keeps the run free of
// look-ahead bias. Collapsing any of this into one step would silently
// reintroduce it.
//
// Equity is recorded from step 5, before the bar's new orders exist:
// submitting an order moves no cash, so the figure is unaffected, and
// taking it there keeps one equity value per bar regardless of whether the
// strategy traded.
//
// STATELESSNESS: every run builds its own Account, PendingOrderBook,
// OrderGenerator and ExecutionLog, so the same engine can serve one run
// after another and two runs over the same inputs produce the same result.
// The Strategy is the exception -- it is passed by reference and carries
// its own signal state, so a fresh instance (or clone()) belongs to each
// run. The engine cannot enforce that.
//
// PRICES: unit prices come from the market data's close column. That is
// exactly right for cash instruments and NOT right for anything whose
// value has to be modelled -- an option's close in a data file is not what
// the PricingEngine would say. Valuing those needs the pricer path that
// AccountValuator::priceUniverse already provides, which is a later
// extension and the reason prices are computed in one place below.
//
// LIMITATION, worth knowing before reading a result: PerformanceMetrics
// requires a strictly positive equity curve, and BacktestResult computes
// them in its constructor. A run that wipes the account out therefore
// throws instead of handing back a result that could be inspected -- the
// one case where a completed simulation yields nothing. Separating "the
// simulation ran" from "the metrics are defined" is a risk-management
// concern and deliberately not solved here.
//
// Exception safety: anything thrown mid-run propagates and the partial run
// is discarded. Every piece of state it touches is local to run(), so
// nothing inconsistent escapes; a strategy that mutated itself before the
// throw is not rolled back.
class BacktestEngine {
public:
    explicit BacktestEngine(BacktestConfig config);

    [[nodiscard]]
    BacktestResult run(
        Strategy& strategy,
        const InstrumentUniverse& universe,
        const HistoricalMarketData& market_data
    ) const;

    const BacktestConfig& config() const noexcept { return config_; }

private:
    BacktestConfig config_;
};
