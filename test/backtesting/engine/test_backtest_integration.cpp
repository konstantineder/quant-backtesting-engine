// End-to-end wiring of the migrated architecture: the existing
// SmaCrossoverStrategy drives orders through the new trading domain into
// an Account, with every fill recorded in an ExecutionLog and the equity
// curve marked to market by the AccountValuator.
//
// The glue lives here rather than in src/: the engine loop is a separate
// ticket. What this test pins down is that the new pieces compose, and
// that the legacy Strategy interface still fits without changes.

#include "account/account.hpp"
#include "backtesting/backtest_result.hpp"
#include "market_data/historical_market_data.hpp"
#include "strategy/sma_crossover_strategy.hpp"
#include "execution/execution_log.hpp"
#include "instrument/instrument_universe.hpp"
#include "analytics/account_valuator.hpp"
#include "volatility/flat_volatility_surface.hpp"
#include "yield_curve/flat_yield_curve.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <vector>

namespace {

constexpr double kThreshold = 1e-7;
constexpr double kInitialCash = 100'000.0;
constexpr double kFeePerFill = 1.0;
constexpr std::size_t kShortWindow = 2;
constexpr std::size_t kLongWindow = 4;

// Rises, then falls, then rises sharply: the strategy goes long at bar 3,
// flat at bar 6 and long again at bar 11.
const std::vector<double> kCloses{
    100.0, 101.0, 102.0, 103.0, 104.0, 103.0,
    101.0, 98.0, 94.0, 90.0, 100.0, 110.0
};

std::vector<Date> makeDates(std::size_t n) {
    std::vector<Date> dates;
    dates.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        dates.push_back(
            std::chrono::sys_days{std::chrono::year{2024} / 1 / 1} +
            std::chrono::days{static_cast<int>(i)}
        );
    }
    return dates;
}

HistoricalMarketData makeMarketData() {
    const auto rows = static_cast<Eigen::Index>(kCloses.size());

    OHLCVData data;
    data.close = MarketMatrix::Zero(rows, 1);
    for (Eigen::Index t = 0; t < rows; ++t) {
        data.close(t, 0) = kCloses[static_cast<std::size_t>(t)];
    }

    data.open = data.close;
    data.high = data.close * 1.01;
    data.low = data.close * 0.99;
    data.volume = MarketMatrix::Constant(rows, 1, 1'000.0);

    return HistoricalMarketData{
        makeDates(kCloses.size()), {"AAPL"}, data
    };
}

InstrumentRegistry makeRegistry() {
    InstrumentRegistry registry;
    // Instrument id and ticker coincide, so market data columns can be
    // published into the snapshot under the id.
    registry.registerInstrument(Equity{"AAPL", "AAPL"});
    return registry;
}

MarketContext makeMarketContext(
    const HistoricalMarketData& data,
    std::size_t bar
) {
    MarketSnapshot snapshot;
    for (std::size_t slot = 0; slot < data.numInstruments(); ++slot) {
        snapshot.updatePrice(
            data.instrumentIds()[slot], data.close(bar, slot)
        );
    }

    return MarketContext{
        data.date(bar),
        snapshot,
        {},
        std::make_shared<FlatYieldCurve>(0.0),
        {}
    };
}

// Minimal engine loop: rebalance to the strategy's target weights at the
// close of every bar on which it emits a signal.
BacktestResult runBacktest(
    const HistoricalMarketData& data,
    const InstrumentRegistry& registry,
    Strategy& strategy
) {
    const InstrumentUniverse universe =
        InstrumentUniverse::fromInstrumentIds(
            data.instrumentIds(), registry
        );
    universe.validateAlignment(data.instrumentIds());

    Account account = Account::forUniverse(kInitialCash, universe);
    ExecutionLog log;
    log.reserve(data.numDates());

    const PricingEngine pricer;
    const AccountValuator valuator;

    std::vector<Date> dates;
    std::vector<double> equity_curve;
    dates.reserve(data.numDates());
    equity_curve.reserve(data.numDates());

    TargetWeights weights;
    AccountValuation valuation{};
    std::uint64_t next_order_id = 1;

    for (std::size_t bar = 0; bar < data.numDates(); ++bar) {
        const StrategyContext context{data, bar};
        const MarketContext market = makeMarketContext(data, bar);

        // Mark to market first: orders are sized off current equity.
        valuation =
            valuator.evaluate(account, universe, market, pricer);

        if (strategy.onBar(context, weights)) {
            for (std::size_t slot = 0; slot < universe.size(); ++slot) {
                const auto index = InstrumentIndex{slot};
                const double price = data.close(bar, slot);
                const double multiplier =
                    account.spec(index).contractMultiplier();

                const double target_quantity =
                    weights[slot] * valuation.equity /
                    (price * multiplier);

                const double delta =
                    target_quantity -
                    account.position(index).quantity();

                if (std::abs(delta) < 1e-12) {
                    continue;
                }

                const Order order =
                    MarketOrder{OrderId{next_order_id++}, index, delta};
                const Fill fill = Fill::fromOrder(
                    order,
                    delta,
                    price,
                    kFeePerFill,
                    data.date(bar),
                    bar
                );

                account.applyFill(fill);
                log.record(fill);
            }

            valuation =
                valuator.evaluate(account, universe, market, pricer);
        }

        dates.push_back(data.date(bar));
        equity_curve.push_back(valuation.equity);
    }

    return BacktestResult{
        std::move(dates),
        std::move(equity_curve),
        std::move(log),
        valuation
    };
}

} // namespace

class BacktestIntegrationTest : public ::testing::Test {
protected:
    InstrumentRegistry registry = makeRegistry();
    HistoricalMarketData data = makeMarketData();

    BacktestResult run() {
        SmaCrossoverStrategy strategy{0, kShortWindow, kLongWindow};
        return runBacktest(data, registry, strategy);
    }
};

TEST_F(BacktestIntegrationTest, ProducesOneEquityPointPerBar) {
    const BacktestResult result = run();

    ASSERT_EQ(result.numBars(), kCloses.size());
    EXPECT_EQ(result.dates().size(), kCloses.size());
    EXPECT_EQ(result.dates().front(), data.date(0));
    EXPECT_EQ(result.dates().back(), data.date(kCloses.size() - 1));

    // Nothing is held during warmup, so equity is still untouched cash.
    EXPECT_NEAR(result.equityCurve().front(), kInitialCash, kThreshold);
}

TEST_F(BacktestIntegrationTest, TradesOnlyOnSignalChanges) {
    const BacktestResult result = run();

    ASSERT_EQ(result.fills().size(), 3u);

    EXPECT_EQ(result.fills()[0].barIndex(), 3u);
    EXPECT_EQ(result.fills()[0].side(), OrderSide::BUY);

    EXPECT_EQ(result.fills()[1].barIndex(), 6u);
    EXPECT_EQ(result.fills()[1].side(), OrderSide::SELL);

    EXPECT_EQ(result.fills()[2].barIndex(), 11u);
    EXPECT_EQ(result.fills()[2].side(), OrderSide::BUY);
}

TEST_F(BacktestIntegrationTest, GoingFlatClosesTheWholePosition) {
    const BacktestResult result = run();

    ASSERT_EQ(result.fills().size(), 3u);
    // The cash signal targets weight 0, so the sale is the exact inverse
    // of what was bought.
    EXPECT_NEAR(
        result.fills()[1].quantity(),
        -result.fills()[0].quantity(),
        kThreshold
    );
}

TEST_F(BacktestIntegrationTest, FillsAreStampedWithTheirBarDate) {
    const BacktestResult result = run();

    for (const Fill& fill : result.fills()) {
        EXPECT_EQ(fill.date(), data.date(fill.barIndex()));
        EXPECT_NEAR(
            fill.price(),
            data.close(fill.barIndex(), 0),
            kThreshold
        );
        EXPECT_NEAR(fill.fee(), kFeePerFill, kThreshold);
    }
}

TEST_F(BacktestIntegrationTest, FinalValuationSatisfiesTheEquityInvariant) {
    const BacktestResult result = run();
    const AccountValuation& valuation = result.finalValuation();

    EXPECT_NEAR(
        valuation.equity,
        valuation.cash + valuation.position_market_value,
        kThreshold
    );
    EXPECT_NEAR(
        valuation.equity - kInitialCash,
        valuation.realized_pnl +
            valuation.unrealized_pnl -
            valuation.total_fees,
        kThreshold
    );
    // One fee per fill, counted once.
    EXPECT_NEAR(
        valuation.total_fees,
        kFeePerFill * static_cast<double>(result.fills().size()),
        kThreshold
    );
    EXPECT_NEAR(
        valuation.equity, result.equityCurve().back(), kThreshold
    );
}

TEST_F(BacktestIntegrationTest, MetricsDescribeTheEquityCurve) {
    const BacktestResult result = run();
    const PerformanceMetrics& metrics = result.metrics();

    EXPECT_EQ(metrics.num_bars, kCloses.size());
    EXPECT_NEAR(
        metrics.total_return,
        result.equityCurve().back() / result.equityCurve().front() - 1.0,
        kThreshold
    );
    EXPECT_GE(metrics.max_drawdown, 0.0);
    EXPECT_LT(metrics.max_drawdown, 1.0);
    EXPECT_GE(metrics.volatility, 0.0);
}

TEST_F(BacktestIntegrationTest, ReplayingTheLogReproducesTheAccount) {
    const BacktestResult result = run();

    const InstrumentUniverse universe =
        InstrumentUniverse::fromInstrumentIds(
            data.instrumentIds(), registry
        );

    Account replayed = Account::forUniverse(kInitialCash, universe);
    for (const Fill& fill : result.fills()) {
        replayed.applyFill(fill);
    }

    EXPECT_NEAR(
        replayed.cash(), result.finalValuation().cash, kThreshold
    );
    EXPECT_NEAR(
        replayed.realizedPnL(),
        result.finalValuation().realized_pnl,
        kThreshold
    );
    EXPECT_NEAR(
        replayed.totalFees(),
        result.finalValuation().total_fees,
        kThreshold
    );
}

TEST_F(BacktestIntegrationTest, RunsAreDeterministic) {
    const BacktestResult first = run();
    const BacktestResult second = run();

    ASSERT_EQ(first.numBars(), second.numBars());
    for (std::size_t bar = 0; bar < first.numBars(); ++bar) {
        EXPECT_DOUBLE_EQ(
            first.equityCurve()[bar], second.equityCurve()[bar]
        );
    }

    ASSERT_EQ(first.fills().size(), second.fills().size());
    for (std::size_t i = 0; i < first.fills().size(); ++i) {
        EXPECT_DOUBLE_EQ(
            first.fills()[i].quantity(), second.fills()[i].quantity()
        );
        EXPECT_DOUBLE_EQ(
            first.fills()[i].price(), second.fills()[i].price()
        );
    }
}

TEST_F(BacktestIntegrationTest, ClonedStrategyRunsIndependently) {
    SmaCrossoverStrategy original{0, kShortWindow, kLongWindow};
    const std::unique_ptr<Strategy> worker = original.clone();

    // Each worker gets its own strategy state and its own account, so the
    // clone reproduces the original run rather than continuing it.
    const BacktestResult reference = runBacktest(data, registry, original);
    const BacktestResult cloned = runBacktest(data, registry, *worker);

    ASSERT_EQ(reference.fills().size(), cloned.fills().size());
    for (std::size_t i = 0; i < reference.fills().size(); ++i) {
        EXPECT_EQ(
            reference.fills()[i].barIndex(),
            cloned.fills()[i].barIndex()
        );
        EXPECT_DOUBLE_EQ(
            reference.fills()[i].quantity(),
            cloned.fills()[i].quantity()
        );
    }
    EXPECT_DOUBLE_EQ(
        reference.finalValuation().equity,
        cloned.finalValuation().equity
    );
}
