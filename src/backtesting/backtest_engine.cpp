#include "backtesting/backtest_engine.hpp"

#include "account/account.hpp"
#include "strategy/strategy_context.hpp"
#include "execution/execution_model.hpp"
#include "execution/execution_log.hpp"
#include "execution/fill.hpp"
#include "order/order_generator.hpp"
#include "order/order_instruction.hpp"
#include "order/pending_order_book.hpp"
#include "analytics/account_valuation.hpp"
#include "analytics/account_valuator.hpp"

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

namespace {

void validateConfig(const BacktestConfig& config) {
    if (!std::isfinite(config.initial_cash) || config.initial_cash <= 0.0) {
        throw std::invalid_argument(
            "Initial cash must be finite and strictly positive"
        );
    }

    // computePerformanceMetrics rejects a bad factor too, but only once a
    // whole run has been simulated. Rejecting it here costs nothing and
    // fails at the point the mistake was actually made.
    if (!std::isfinite(config.bars_per_year) ||
        config.bars_per_year <= 0.0) {
        throw std::invalid_argument(
            "Bars per year must be finite and strictly positive"
        );
    }
}

// Close prices for every slot of the bar, in InstrumentIndex order --
// the layout AccountValuator and OrderGenerator both expect.
void priceUniverseFromCloses(
    const HistoricalMarketData& market_data,
    std::size_t bar,
    std::vector<double>& unit_prices
) {
    for (std::size_t slot = 0; slot < unit_prices.size(); ++slot) {
        unit_prices[slot] = market_data.close(bar, slot);
    }
}

} // namespace

BacktestEngine::BacktestEngine(BacktestConfig config)
    : config_(config) {
    validateConfig(config_);
}

BacktestResult BacktestEngine::run(
    Strategy& strategy,
    const InstrumentUniverse& universe,
    const HistoricalMarketData& market_data
) const {
    // FR-01. Slot order IS column order everywhere downstream -- an
    // InstrumentIndex indexes the market data directly -- so a mismatch
    // would silently price one instrument with another's quotes. Checked
    // once, here, rather than guessed at per bar.
    universe.validateAlignment(market_data.instrumentIds());

    // At least one bar is guaranteed: HistoricalMarketData rejects an
    // empty date vector at construction. That matters here because the
    // valuation returned below is the one the last bar produced -- with no
    // bars at all it would be a default-constructed zero, which would
    // claim an empty account that in fact holds initial_cash.
    const std::size_t num_bars = market_data.numDates();
    const std::size_t slots = universe.size();

    // FR-02, FR-03: everything below is per-run state.
    Account account = Account::forUniverse(config_.initial_cash, universe);
    PendingOrderBook pending_orders{universe};
    OrderGenerator order_generator;
    const ExecutionModel execution_model;
    const AccountValuator valuator;

    ExecutionLog execution_log;
    execution_log.reserve(num_bars);

    std::vector<Date> dates;
    std::vector<double> equity_curve;
    dates.reserve(num_bars);
    equity_curve.reserve(num_bars);

    // Reuse the price buffer across bars to avoid reallocating a
    // universe-wide vector every bar. The loop as a whole is NOT
    // allocation-free: the fills and the plan are returned by value, and
    // the book, the log and the strategy may each allocate.
    std::vector<double> unit_prices(slots, 0.0);
    TargetWeights target_weights;

    AccountValuation valuation{};

    for (std::size_t bar = 0; bar < num_bars; ++bar) {

        // ------------------------------------------------- at the open --

        // Phase 1 (FR-04). Orders submitted on an earlier bar execute at
        // this bar's open. The model decides what fills; it changes
        // nothing, so the fills can be inspected before they are applied.
        const std::vector<Fill> fills =
            execution_model.execute(pending_orders, market_data, bar);

        // Phase 2 (FR-05, FR-06). The book first: it re-validates the fill
        // against the order it belongs to -- direction, size, no overfill
        // -- so a fill the book rejects never reaches the Account's cash.
        // The log last, because it is meant to record what actually
        // happened; replaying it into a fresh Account must reproduce this
        // one exactly, and an entry for a rejected fill would break that.
        for (const Fill& fill : fills) {
            pending_orders.applyFill(fill);
            account.applyFill(fill);
            execution_log.record(fill);
        }

        // Phase 3 (FR-07). Filled and cancelled orders are done with; the
        // book keeps only what is still working, so the generator in phase
        // 7 reconciles against live exposure alone. This invalidates every
        // reference into the book, which is why it happens between the
        // phases and never inside one.
        pending_orders.removeTerminal();

        // ------------------------------------------------ at the close --

        // Phase 4. Once per bar, shared by the valuation and the
        // generator, so a bar has exactly one set of prices.
        priceUniverseFromCloses(market_data, bar, unit_prices);

        // Phase 5 (FR-08).
        valuation = valuator.evaluateWithPrices(account, unit_prices);

        // Phase 6 (FR-09). StrategyContext exposes this bar and history
        // behind it, never a later one. onBar returns false when the
        // strategy has nothing new to say, and leaves the target alone --
        // which is not the same as targeting zero, so there is nothing to
        // reconcile and the existing orders stay as they are.
        if (strategy.onBar(StrategyContext{market_data, bar}, target_weights)) {

            // Phase 7 (FR-10). The generator reconciles the target against
            // the position AND what is still working, and returns every
            // cancellation before any submission -- the book rejects an
            // order that opposes live exposure, so applying the plan in
            // the order given is what makes a reversal possible at all.
            std::vector<OrderInstruction> plan =
                order_generator.generate(
                    target_weights, account, unit_prices, pending_orders
                );

            // The plan is dead once applied, so each order is moved into
            // the book rather than copied.
            for (OrderInstruction& instruction : plan) {
                if (const auto* cancel =
                        std::get_if<CancelOrder>(&instruction)) {
                    pending_orders.cancel(cancel->id);
                } else {
                    // Stamped with THIS bar, which is what stops the
                    // ExecutionModel from filling it before the next open.
                    pending_orders.submit(
                        std::move(std::get<SubmitOrder>(instruction).order),
                        bar
                    );
                }
            }
        }

        // Phase 8 (FR-11). One point per bar, traded or not, so the curve
        // and the dates stay index-aligned with the market data.
        dates.push_back(market_data.date(bar));
        equity_curve.push_back(valuation.equity);
    }

    // FR-12.
    return BacktestResult{
        std::move(dates),
        std::move(equity_curve),
        std::move(execution_log),
        valuation,
        config_.bars_per_year
    };
}
