#include "backtesting/backtest_engine.hpp"

#include "common/constants.hpp"
#include "account/account.hpp"
#include "backtesting/backtest_config.hpp"
#include "market_data/historical_market_data.hpp"
#include "strategy/sma_crossover_strategy.hpp"
#include "strategy/strategy.hpp"
#include "instrument/instrument.hpp"
#include "instrument/instrument_registry.hpp"
#include "instrument/instrument_universe.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr double kThreshold = 1e-9;
constexpr double kInitialCash = 100'000.0;

// Opens differ from closes throughout, which is what makes the open-fill
// rule observable: a fill priced at the close would show up immediately.
constexpr double kOpenFactor = 0.98;

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

HistoricalMarketData makeMarketData(
    const std::vector<double>& closes = kCloses
) {
    const auto rows = static_cast<Eigen::Index>(closes.size());

    OHLCVData data;
    data.close = MarketMatrix::Zero(rows, 1);
    for (Eigen::Index t = 0; t < rows; ++t) {
        data.close(t, 0) = closes[static_cast<std::size_t>(t)];
    }

    data.open = data.close * kOpenFactor;
    data.high = data.close * 1.05;
    data.low = data.open * 0.95;
    data.volume = MarketMatrix::Constant(rows, 1, 1'000.0);

    return HistoricalMarketData{
        makeDates(closes.size()), {"AAPL"}, data
    };
}

const InstrumentRegistry& registry() {
    static const InstrumentRegistry built = [] {
        InstrumentRegistry reg;
        reg.registerInstrument(Equity{"AAPL", "AAPL"});
        reg.registerInstrument(Equity{"MSFT", "MSFT"});
        return reg;
    }();
    return built;
}

InstrumentUniverse makeUniverse(
    const std::vector<std::string>& ids = {"AAPL"}
) {
    return InstrumentUniverse::fromInstrumentIds(ids, registry());
}

BacktestEngine makeEngine(double cash = kInitialCash) {
    return BacktestEngine{BacktestConfig{cash, TRADING_DAYS_PER_YEAR}};
}

// Holds a constant weight from the first bar on, re-emitting it on every
// bar. Useful for pinning down the bar on which execution happens, which
// the SMA strategy's warmup would otherwise obscure.
class AlwaysTargetStrategy final : public Strategy {
public:
    explicit AlwaysTargetStrategy(double weight) : weight_(weight) {}

    bool onBar(const StrategyContext& context, TargetWeights& out) override {
        out.assign(context.numInstruments(), 0.0);
        out[0] = weight_;
        ++calls_;
        return true;
    }

    std::unique_ptr<Strategy> clone() const override {
        return std::make_unique<AlwaysTargetStrategy>(weight_);
    }

    std::size_t calls() const noexcept { return calls_; }

private:
    double weight_;
    std::size_t calls_ = 0;
};

// Never emits a target, so the account must stay exactly as it started.
class PassiveStrategy final : public Strategy {
public:
    bool onBar(const StrategyContext&, TargetWeights&) override {
        return false;
    }

    std::unique_ptr<Strategy> clone() const override {
        return std::make_unique<PassiveStrategy>();
    }
};

} // namespace

// ------------------------------------------------------- configuration --

TEST(BacktestEngineTest, RejectsANonPositiveOrNonFiniteConfig) {
    EXPECT_THROW(makeEngine(0.0), std::invalid_argument);
    EXPECT_THROW(makeEngine(-1.0), std::invalid_argument);
    EXPECT_THROW(
        makeEngine(std::numeric_limits<double>::quiet_NaN()),
        std::invalid_argument
    );

    EXPECT_THROW(
        BacktestEngine(BacktestConfig{kInitialCash, 0.0}),
        std::invalid_argument
    );
    EXPECT_THROW(
        BacktestEngine(BacktestConfig{
            kInitialCash, std::numeric_limits<double>::infinity()
        }),
        std::invalid_argument
    );
}

TEST(BacktestEngineTest, RejectsAUniverseMisalignedWithTheMarketData) {
    const BacktestEngine engine = makeEngine();
    const HistoricalMarketData data = makeMarketData();
    SmaCrossoverStrategy strategy{0, 2, 4};

    // run() is [[nodiscard]], and EXPECT_THROW evaluates its argument as a
    // statement -- so the result has to be discarded explicitly here. The
    // call is expected to throw, which means there is no result to read.

    // Right size, wrong instrument.
    EXPECT_THROW(
        static_cast<void>(engine.run(strategy, makeUniverse({"MSFT"}), data)),
        std::invalid_argument
    );

    // Wrong size.
    EXPECT_THROW(
        static_cast<void>(
            engine.run(strategy, makeUniverse({"AAPL", "MSFT"}), data)
        ),
        std::invalid_argument
    );
}

// --------------------------------------------------------- the equity curve --

TEST(BacktestEngineTest, ProducesOneDateAndEquityPointPerBar) {
    const BacktestEngine engine = makeEngine();
    const HistoricalMarketData data = makeMarketData();
    SmaCrossoverStrategy strategy{0, 2, 4};

    const BacktestResult result = engine.run(strategy, makeUniverse(), data);

    ASSERT_EQ(result.numBars(), kCloses.size());
    ASSERT_EQ(result.dates().size(), kCloses.size());
    EXPECT_EQ(result.dates().front(), data.date(0));
    EXPECT_EQ(result.dates().back(), data.date(kCloses.size() - 1));

    // Nothing is held on bar 0, so equity is still untouched cash.
    EXPECT_NEAR(result.equityCurve().front(), kInitialCash, kThreshold);
    EXPECT_EQ(result.metrics().num_bars, kCloses.size());
}

TEST(BacktestEngineTest, APassiveStrategyLeavesTheAccountUntouched) {
    const BacktestEngine engine = makeEngine();
    PassiveStrategy strategy;

    const BacktestResult result =
        engine.run(strategy, makeUniverse(), makeMarketData());

    EXPECT_TRUE(result.fills().empty());
    EXPECT_NEAR(result.finalValuation().cash, kInitialCash, kThreshold);
    EXPECT_NEAR(result.finalValuation().equity, kInitialCash, kThreshold);
    EXPECT_DOUBLE_EQ(result.metrics().total_return, 0.0);

    for (const double equity : result.equityCurve()) {
        EXPECT_NEAR(equity, kInitialCash, kThreshold);
    }
}

// ------------------------------------------------- the open/close split --

// The heart of the engine: a target decided at the close of bar b may not
// execute before the open of bar b + 1.
TEST(BacktestEngineTest, OrdersDecidedAtACloseFillAtTheNextOpen) {
    const BacktestEngine engine = makeEngine();
    const HistoricalMarketData data = makeMarketData();
    AlwaysTargetStrategy strategy{1.0};

    const BacktestResult result = engine.run(strategy, makeUniverse(), data);

    ASSERT_FALSE(result.fills().empty());
    const Fill& first = result.fills().front();

    // Decided on bar 0, executed on bar 1 ...
    EXPECT_EQ(first.barIndex(), 1u);
    EXPECT_EQ(first.date(), data.date(1));
    // ... at bar 1's OPEN, not at the close the decision was based on.
    EXPECT_NEAR(first.price(), kCloses[1] * kOpenFactor, kThreshold);
    EXPECT_GT(first.quantity(), 0.0);
}

// A corollary of the same rule: the first bar can never trade, so its
// equity is always the initial cash whatever the strategy wants.
TEST(BacktestEngineTest, TheFirstBarNeverTrades) {
    const BacktestEngine engine = makeEngine();
    AlwaysTargetStrategy strategy{1.0};

    const BacktestResult result =
        engine.run(strategy, makeUniverse(), makeMarketData());

    EXPECT_NEAR(result.equityCurve().front(), kInitialCash, kThreshold);

    for (const Fill& fill : result.fills()) {
        EXPECT_GT(fill.barIndex(), 0u);
    }
}

TEST(BacktestEngineTest, EveryFillIsStampedWithItsOwnBarDate) {
    const BacktestEngine engine = makeEngine();
    const HistoricalMarketData data = makeMarketData();
    AlwaysTargetStrategy strategy{1.0};

    const BacktestResult result = engine.run(strategy, makeUniverse(), data);

    ASSERT_FALSE(result.fills().empty());
    for (const Fill& fill : result.fills()) {
        EXPECT_EQ(fill.date(), data.date(fill.barIndex()));
        EXPECT_NEAR(
            fill.price(),
            kCloses[fill.barIndex()] * kOpenFactor,
            kThreshold
        );
    }
}

// Fills are ordered as they happened, which is what makes the log
// replayable.
TEST(BacktestEngineTest, TheExecutionLogIsInNonDecreasingBarOrder) {
    const BacktestEngine engine = makeEngine();
    AlwaysTargetStrategy strategy{1.0};

    const BacktestResult result =
        engine.run(strategy, makeUniverse(), makeMarketData());

    ASSERT_FALSE(result.fills().empty());
    for (std::size_t i = 1; i < result.fills().size(); ++i) {
        EXPECT_LE(
            result.fills()[i - 1].barIndex(),
            result.fills()[i].barIndex()
        );
    }
}

// ------------------------------------------------------ reconciliation --

// Re-emitting the same target must not keep trading: once the position is
// there, the generator nets it against position plus pending and finds
// nothing left to do. A per-bar order would show up as a fill on nearly
// every bar.
TEST(BacktestEngineTest, AConstantTargetDoesNotTradeOnEveryBar) {
    const BacktestEngine engine = makeEngine();
    AlwaysTargetStrategy strategy{1.0};

    const BacktestResult result =
        engine.run(strategy, makeUniverse(), makeMarketData());

    EXPECT_EQ(strategy.calls(), kCloses.size());
    // Prices drift, so the weight target does rebalance -- but far less
    // often than once per bar.
    EXPECT_LT(result.fills().size(), kCloses.size());
}

// Nothing may be left working that the account does not know about: a
// double-counted pending order would show up as a position beyond target.
TEST(BacktestEngineTest, SatisfiesTheEquityIdentity) {
    const BacktestEngine engine = makeEngine();
    AlwaysTargetStrategy strategy{1.0};

    const BacktestResult result =
        engine.run(strategy, makeUniverse(), makeMarketData());

    const AccountValuation& final_valuation = result.finalValuation();

    EXPECT_NEAR(
        final_valuation.equity,
        final_valuation.cash + final_valuation.position_market_value,
        1e-6
    );
    EXPECT_NEAR(
        final_valuation.equity - kInitialCash,
        final_valuation.net_pnl,
        1e-6
    );
}

TEST(BacktestEngineTest, AShortTargetSellsFirst) {
    const BacktestEngine engine = makeEngine();
    AlwaysTargetStrategy strategy{-1.0};

    const BacktestResult result =
        engine.run(strategy, makeUniverse(), makeMarketData());

    ASSERT_FALSE(result.fills().empty());
    EXPECT_LT(result.fills().front().quantity(), 0.0);
    EXPECT_LT(result.finalValuation().position_market_value, 0.0);
}

// ---------------------------------------------------- run independence --

// Per-run state means the same engine can be reused and two runs over the
// same inputs agree bar for bar.
TEST(BacktestEngineTest, RunsAreIndependentAndDeterministic) {
    const BacktestEngine engine = makeEngine();
    const HistoricalMarketData data = makeMarketData();

    SmaCrossoverStrategy first{0, 2, 4};
    SmaCrossoverStrategy second{0, 2, 4};

    const BacktestResult a = engine.run(first, makeUniverse(), data);
    const BacktestResult b = engine.run(second, makeUniverse(), data);

    ASSERT_EQ(a.numBars(), b.numBars());
    for (std::size_t bar = 0; bar < a.numBars(); ++bar) {
        EXPECT_DOUBLE_EQ(a.equityCurve()[bar], b.equityCurve()[bar])
            << "bar " << bar;
    }

    ASSERT_EQ(a.fills().size(), b.fills().size());
    for (std::size_t i = 0; i < a.fills().size(); ++i) {
        EXPECT_EQ(a.fills()[i].barIndex(), b.fills()[i].barIndex());
        EXPECT_DOUBLE_EQ(a.fills()[i].quantity(), b.fills()[i].quantity());
        EXPECT_EQ(a.fills()[i].orderId(), b.fills()[i].orderId());
    }

    EXPECT_DOUBLE_EQ(a.metrics().total_return, b.metrics().total_return);
    EXPECT_DOUBLE_EQ(a.metrics().sharpe_ratio, b.metrics().sharpe_ratio);
}

// Replaying the log into a fresh Account must reproduce the run's own
// account exactly -- the property that makes the log an audit trail rather
// than a summary.
TEST(BacktestEngineTest, ReplayingTheLogReproducesTheAccount) {
    const BacktestEngine engine = makeEngine();
    const HistoricalMarketData data = makeMarketData();
    AlwaysTargetStrategy strategy{1.0};

    const BacktestResult result = engine.run(strategy, makeUniverse(), data);
    ASSERT_FALSE(result.fills().empty());

    const InstrumentUniverse universe = makeUniverse();
    Account replayed = Account::forUniverse(kInitialCash, universe);
    for (const Fill& fill : result.fills()) {
        replayed.applyFill(fill);
    }

    EXPECT_NEAR(replayed.cash(), result.finalValuation().cash, 1e-6);
    EXPECT_NEAR(
        replayed.realizedPnL(), result.finalValuation().realized_pnl, 1e-6
    );
    EXPECT_NEAR(
        replayed.totalFees(), result.finalValuation().total_fees, 1e-6
    );
}

// --------------------------------------------------------- the metrics --

TEST(BacktestEngineTest, MetricsDescribeTheEquityCurveItReturned) {
    const BacktestEngine engine = makeEngine();
    PassiveStrategy strategy;

    const BacktestResult result =
        engine.run(strategy, makeUniverse(), makeMarketData());

    const auto curve = result.equityCurve();
    ASSERT_FALSE(curve.empty());

    EXPECT_NEAR(
        result.metrics().total_return,
        curve.back() / curve.front() - 1.0,
        kThreshold
    );
    EXPECT_GE(result.metrics().max_drawdown, 0.0);
    EXPECT_EQ(result.metrics().num_bars, curve.size());
}

// The annualization factor reaches the metrics: the same curve under a
// different bars-per-year must scale by the ratio of the square roots.
TEST(BacktestEngineTest, PassesTheBarsPerYearFactorToTheMetrics) {
    const HistoricalMarketData data = makeMarketData();

    AlwaysTargetStrategy daily_strategy{1.0};
    AlwaysTargetStrategy hourly_strategy{1.0};

    const BacktestResult daily =
        BacktestEngine{BacktestConfig{kInitialCash, 252.0}}
            .run(daily_strategy, makeUniverse(), data);
    const BacktestResult hourly =
        BacktestEngine{BacktestConfig{kInitialCash, 252.0 * 6.5}}
            .run(hourly_strategy, makeUniverse(), data);

    ASSERT_GT(daily.metrics().volatility, 0.0);
    EXPECT_NEAR(
        hourly.metrics().volatility / daily.metrics().volatility,
        std::sqrt(6.5),
        1e-9
    );
}

// A single bar yields a one-point curve: a defined result with no returns
// in it, rather than an error.
TEST(BacktestEngineTest, HandlesASingleBarRun) {
    const BacktestEngine engine = makeEngine();
    AlwaysTargetStrategy strategy{1.0};

    const BacktestResult result = engine.run(
        strategy, makeUniverse(), makeMarketData({100.0})
    );

    EXPECT_EQ(result.numBars(), 1u);
    EXPECT_TRUE(result.fills().empty());
    EXPECT_DOUBLE_EQ(result.metrics().total_return, 0.0);
    EXPECT_DOUBLE_EQ(result.metrics().volatility, 0.0);
    EXPECT_NEAR(result.equityCurve().front(), kInitialCash, kThreshold);
}
