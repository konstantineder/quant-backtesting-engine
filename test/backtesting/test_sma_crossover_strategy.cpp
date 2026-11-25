#include "strategy/sma_crossover_strategy.hpp"

#include "backtesting/backtest_config.hpp"
#include "backtesting/backtest_engine.hpp"
#include "backtesting/backtest_result.hpp"
#include "common/constants.hpp"
#include "execution/fill.hpp"
#include "instrument/instrument.hpp"
#include "instrument/instrument_registry.hpp"
#include "instrument/instrument_universe.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr std::size_t kShortWindow = 2;
constexpr std::size_t kLongWindow = 4;

// First bar at which the long SMA is defined.
constexpr std::size_t kFirstValidIndex = kLongWindow - 1;

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

// Two instruments; `closes_a` and `closes_b` must have equal length.
HistoricalMarketData makeMarketData(
    const std::vector<double>& closes_a,
    const std::vector<double>& closes_b
) {
    const Eigen::Index rows =
        static_cast<Eigen::Index>(closes_a.size());
    const Eigen::Index cols = 2;

    OHLCVData data;
    data.open = MarketMatrix::Constant(rows, cols, 100.0);
    data.high = MarketMatrix::Constant(rows, cols, 110.0);
    data.low = MarketMatrix::Constant(rows, cols, 90.0);
    data.close = MarketMatrix::Zero(rows, cols);
    data.volume = MarketMatrix::Constant(rows, cols, 1000.0);

    for (Eigen::Index t = 0; t < rows; ++t) {
        data.close(t, 0) = closes_a[static_cast<std::size_t>(t)];
        data.close(t, 1) = closes_b[static_cast<std::size_t>(t)];
    }

    return HistoricalMarketData(
        makeDates(closes_a.size()),
        {"AAPL", "MSFT"},
        data
    );
}

HistoricalMarketData makeDataFor(const std::vector<double>& closes) {
    return makeMarketData(
        closes,
        std::vector<double>(closes.size(), 100.0)
    );
}

// Rising prices: short SMA above long SMA from the first valid bar on.
HistoricalMarketData makeRisingData() {
    return makeDataFor({100.0, 101.0, 102.0, 103.0, 104.0});
}

// Falling prices: short SMA below long SMA from the first valid bar on.
HistoricalMarketData makeFallingData() {
    return makeDataFor({104.0, 103.0, 102.0, 101.0, 100.0});
}

// Rises, then falls, then rises sharply. With windows 2/4 the position
// switches at index 3 (long), 6 (cash) and 11 (long again).
HistoricalMarketData makeSwitchingData() {
    return makeDataFor({
        100.0, 101.0, 102.0, 103.0, 104.0, 103.0,
        101.0, 98.0, 94.0, 90.0, 100.0, 110.0
    });
}

SmaCrossoverStrategy makeStrategy(std::size_t instrument_index = 0) {
    return SmaCrossoverStrategy(
        instrument_index,
        kShortWindow,
        kLongWindow
    );
}

// --- driving the strategy through the engine ---------------------------

constexpr double kEngineCash = 10'000.0;

const InstrumentRegistry& registry() {
    static const InstrumentRegistry built = [] {
        InstrumentRegistry reg;
        reg.registerInstrument(Equity{"AAPL", "AAPL"});
        reg.registerInstrument(Equity{"MSFT", "MSFT"});
        return reg;
    }();
    return built;
}

InstrumentUniverse makeUniverse() {
    return InstrumentUniverse::fromInstrumentIds(
        std::vector<std::string>{"AAPL", "MSFT"}, registry()
    );
}

BacktestEngine makeEngine() {
    return BacktestEngine{
        BacktestConfig{kEngineCash, TRADING_DAYS_PER_YEAR}
    };
}

} // namespace

TEST(SmaCrossoverStrategyTest, RejectsInvalidWindows) {
    EXPECT_THROW(SmaCrossoverStrategy(0, 0, 4), std::invalid_argument);
    EXPECT_THROW(SmaCrossoverStrategy(0, 2, 0), std::invalid_argument);
    EXPECT_THROW(SmaCrossoverStrategy(0, 4, 4), std::invalid_argument);
    EXPECT_THROW(SmaCrossoverStrategy(0, 5, 4), std::invalid_argument);
    EXPECT_NO_THROW(SmaCrossoverStrategy(0, 2, 4));
}

TEST(SmaCrossoverStrategyTest, Warmup_NoSignal) {
    HistoricalMarketData market_data = makeRisingData();
    SmaCrossoverStrategy strategy = makeStrategy();

    TargetWeights weights;
    for (std::size_t index = 0; index < kFirstValidIndex; ++index) {
        StrategyContext context(market_data, index);
        EXPECT_FALSE(strategy.onBar(context, weights))
            << "unexpected signal at index " << index;
        EXPECT_TRUE(weights.empty());
    }
}

TEST(SmaCrossoverStrategyTest, FirstValidBar_EmitsLongSignal) {
    HistoricalMarketData market_data = makeRisingData();
    SmaCrossoverStrategy strategy = makeStrategy();
    StrategyContext context(market_data, kFirstValidIndex);

    TargetWeights weights;
    ASSERT_TRUE(strategy.onBar(context, weights));

    ASSERT_EQ(weights.size(), 2u);
    EXPECT_DOUBLE_EQ(weights[0], 1.0);
    EXPECT_DOUBLE_EQ(weights[1], 0.0);
}

// The first valid bar emits even when the position is cash, so the engine
// learns the intended target weights instead of guessing them.
TEST(SmaCrossoverStrategyTest, FirstValidBar_EmitsCashSignal) {
    HistoricalMarketData market_data = makeFallingData();
    SmaCrossoverStrategy strategy = makeStrategy();
    StrategyContext context(market_data, kFirstValidIndex);

    TargetWeights weights;
    ASSERT_TRUE(strategy.onBar(context, weights));

    ASSERT_EQ(weights.size(), 2u);
    EXPECT_DOUBLE_EQ(weights[0], 0.0);
    EXPECT_DOUBLE_EQ(weights[1], 0.0);
}

TEST(SmaCrossoverStrategyTest, UnchangedSignal_NoEmission) {
    HistoricalMarketData market_data = makeRisingData();
    SmaCrossoverStrategy strategy = makeStrategy();

    TargetWeights weights;
    StrategyContext first(market_data, kFirstValidIndex);
    ASSERT_TRUE(strategy.onBar(first, weights));

    // Prices keep rising, so the position stays long and nothing is emitted.
    for (std::size_t index = kFirstValidIndex + 1; index < 5; ++index) {
        StrategyContext context(market_data, index);
        TargetWeights untouched{-1.0};
        EXPECT_FALSE(strategy.onBar(context, untouched))
            << "unexpected re-emission at index " << index;
        ASSERT_EQ(untouched.size(), 1u);
        EXPECT_DOUBLE_EQ(untouched[0], -1.0);
    }
}

TEST(SmaCrossoverStrategyTest, EmitsOnEveryPositionSwitch) {
    HistoricalMarketData market_data = makeSwitchingData();
    SmaCrossoverStrategy strategy = makeStrategy();

    // Expected weight on the traded instrument at each emitting bar.
    const std::vector<std::pair<std::size_t, double>> expected{
        {3u, 1.0},   // first valid bar: long
        {6u, 0.0},   // long -> cash
        {11u, 1.0},  // cash -> long
    };

    std::vector<std::pair<std::size_t, double>> emitted;
    TargetWeights weights;
    for (std::size_t index = 0; index < 12; ++index) {
        StrategyContext context(market_data, index);
        if (strategy.onBar(context, weights)) {
            ASSERT_EQ(weights.size(), 2u);
            emitted.emplace_back(index, weights[0]);
        }
    }

    EXPECT_EQ(emitted, expected);
}

TEST(SmaCrossoverStrategyTest, TradesTheSelectedInstrumentOnly) {
    HistoricalMarketData market_data = makeMarketData(
        {100.0, 100.0, 100.0, 100.0, 100.0},
        {100.0, 101.0, 102.0, 103.0, 104.0}
    );
    SmaCrossoverStrategy strategy = makeStrategy(1);
    StrategyContext context(market_data, kFirstValidIndex);

    TargetWeights weights;
    ASSERT_TRUE(strategy.onBar(context, weights));

    EXPECT_DOUBLE_EQ(weights[0], 0.0);
    EXPECT_DOUBLE_EQ(weights[1], 1.0);
}

TEST(SmaCrossoverStrategyTest, OverwritesStaleWeights) {
    HistoricalMarketData market_data = makeFallingData();
    SmaCrossoverStrategy strategy = makeStrategy();
    StrategyContext context(market_data, kFirstValidIndex);

    TargetWeights weights{0.5, 0.5, 0.5, 0.5};
    ASSERT_TRUE(strategy.onBar(context, weights));

    ASSERT_EQ(weights.size(), 2u);
    EXPECT_DOUBLE_EQ(weights[0], 0.0);
    EXPECT_DOUBLE_EQ(weights[1], 0.0);
}

TEST(SmaCrossoverStrategyTest, RejectsOutOfRangeInstrument) {
    HistoricalMarketData market_data = makeRisingData();
    SmaCrossoverStrategy strategy = makeStrategy(7);
    StrategyContext context(market_data, kFirstValidIndex);

    TargetWeights weights;
    EXPECT_THROW(strategy.onBar(context, weights), std::out_of_range);
}

TEST(SmaCrossoverStrategyTest, Clone_CopiesConfiguration) {
    SmaCrossoverStrategy strategy(1, 3, 7);

    std::unique_ptr<Strategy> copy = strategy.clone();
    ASSERT_NE(copy, nullptr);

    const auto* typed =
        dynamic_cast<const SmaCrossoverStrategy*>(copy.get());
    ASSERT_NE(typed, nullptr);
    EXPECT_NE(typed, &strategy);
    EXPECT_EQ(typed->instrumentIndex(), 1u);
    EXPECT_EQ(typed->shortWindow(), 3u);
    EXPECT_EQ(typed->longWindow(), 7u);
}

TEST(SmaCrossoverStrategyTest, Clone_ResetsSignalState) {
    HistoricalMarketData market_data = makeRisingData();
    SmaCrossoverStrategy strategy = makeStrategy();

    // Drive the original past its first emission, so it is now silent.
    TargetWeights weights;
    for (std::size_t index = 0; index < 5; ++index) {
        StrategyContext context(market_data, index);
        strategy.onBar(context, weights);
    }
    StrategyContext last(market_data, 4);
    ASSERT_FALSE(strategy.onBar(last, weights));

    // The clone has no signal state, so it emits again on its first bar.
    std::unique_ptr<Strategy> copy = strategy.clone();
    TargetWeights clone_weights;
    ASSERT_TRUE(copy->onBar(last, clone_weights));

    ASSERT_EQ(clone_weights.size(), 2u);
    EXPECT_DOUBLE_EQ(clone_weights[0], 1.0);
}

// ----------------------------------------- the strategy under the engine --
//
// The tests above pin down what the strategy EMITS. These pin down what
// the engine then DOES with it, which is a different claim: an emission on
// bar b must turn into exactly one trade on bar b + 1, and no trade at all
// on the bars in between.
//
// makeSwitchingData opens every bar at 100, so these runs say nothing
// about execution pricing -- that is what the integration suite's varying
// opens are for. Here a flat open isolates the timing.

TEST(SmaCrossoverStrategyTest, EngineRun_TradesOneBarAfterEachSignal) {
    const BacktestEngine engine = makeEngine();
    HistoricalMarketData market_data = makeSwitchingData();
    SmaCrossoverStrategy strategy = makeStrategy();

    const BacktestResult result =
        engine.run(strategy, makeUniverse(), market_data);

    // Signals land on bars 3 (long), 6 (cash) and 11 (long again). The
    // last one has no bar left to execute on, so only two trades happen.
    ASSERT_EQ(result.fills().size(), 2u);

    EXPECT_EQ(result.fills()[0].barIndex(), 4u);
    EXPECT_EQ(result.fills()[0].side(), OrderSide::BUY);
    EXPECT_EQ(result.fills()[0].instrument(), InstrumentIndex{0});

    EXPECT_EQ(result.fills()[1].barIndex(), 7u);
    EXPECT_EQ(result.fills()[1].side(), OrderSide::SELL);
    EXPECT_EQ(result.fills()[1].instrument(), InstrumentIndex{0});

    // The cash signal on bar 6 closes the position exactly.
    EXPECT_NEAR(
        result.fills()[1].quantity(), -result.fills()[0].quantity(), 1e-9
    );
    EXPECT_NEAR(result.finalValuation().position_market_value, 0.0, 1e-6);
}

// Warmup emits nothing, so the account must still be untouched cash on
// every bar before the first signal can be executed.
TEST(SmaCrossoverStrategyTest, EngineRun_WarmupDoesNotTrade) {
    const BacktestEngine engine = makeEngine();
    HistoricalMarketData market_data = makeSwitchingData();
    SmaCrossoverStrategy strategy = makeStrategy();

    const BacktestResult result =
        engine.run(strategy, makeUniverse(), market_data);

    // First signal on bar 3, first possible execution on bar 4.
    for (std::size_t bar = 0; bar <= kFirstValidIndex; ++bar) {
        EXPECT_NEAR(result.equityCurve()[bar], kEngineCash, 1e-9)
            << "bar " << bar;
    }

    for (const Fill& fill : result.fills()) {
        EXPECT_GT(fill.barIndex(), kFirstValidIndex);
    }
}

// A run only trades the instrument the strategy was configured for, even
// though the universe holds two.
TEST(SmaCrossoverStrategyTest, EngineRun_TradesTheSelectedInstrumentOnly) {
    const BacktestEngine engine = makeEngine();
    HistoricalMarketData market_data = makeMarketData(
        std::vector<double>(12, 100.0),
        {100.0, 101.0, 102.0, 103.0, 104.0, 103.0,
         101.0, 98.0, 94.0, 90.0, 100.0, 110.0}
    );
    SmaCrossoverStrategy strategy = makeStrategy(1);

    const BacktestResult result =
        engine.run(strategy, makeUniverse(), market_data);

    ASSERT_FALSE(result.fills().empty());
    for (const Fill& fill : result.fills()) {
        EXPECT_EQ(fill.instrument(), InstrumentIndex{1});
    }
}

// A clone carries the configuration but no signal state, so running it
// from scratch reproduces the original run exactly.
TEST(SmaCrossoverStrategyTest, EngineRun_CloneReproducesTheRun) {
    const BacktestEngine engine = makeEngine();
    HistoricalMarketData market_data = makeSwitchingData();

    SmaCrossoverStrategy original = makeStrategy();
    const BacktestResult reference =
        engine.run(original, makeUniverse(), market_data);

    const std::unique_ptr<Strategy> worker = original.clone();
    const BacktestResult cloned =
        engine.run(*worker, makeUniverse(), market_data);

    ASSERT_EQ(reference.numBars(), cloned.numBars());
    for (std::size_t bar = 0; bar < reference.numBars(); ++bar) {
        EXPECT_DOUBLE_EQ(
            reference.equityCurve()[bar], cloned.equityCurve()[bar]
        ) << "bar " << bar;
    }

    ASSERT_EQ(reference.fills().size(), cloned.fills().size());
    for (std::size_t i = 0; i < reference.fills().size(); ++i) {
        EXPECT_EQ(reference.fills()[i].barIndex(), cloned.fills()[i].barIndex());
        EXPECT_DOUBLE_EQ(
            reference.fills()[i].quantity(), cloned.fills()[i].quantity()
        );
    }
}
