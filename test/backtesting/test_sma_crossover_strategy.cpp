#include "strategy/sma_crossover_strategy.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
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
