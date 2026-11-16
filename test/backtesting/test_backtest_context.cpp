#include "backtesting/strategy/strategy_context.hpp"
#include <gtest/gtest.h>

namespace {

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
    const Eigen::Index rows = 5;
    const Eigen::Index cols = 2;

    OHLCVData data;
    data.open = MarketMatrix::Constant(rows, cols, 100.0);
    data.high = MarketMatrix::Constant(rows, cols, 110.0);
    data.low = MarketMatrix::Constant(rows, cols, 90.0);
    data.close = MarketMatrix::Constant(rows, cols, 105.0);
    data.volume = MarketMatrix::Constant(rows, cols, 1000.0);

    for (Eigen::Index t = 0; t < rows; ++t) {
        for (Eigen::Index i = 0; i < cols; ++i) {
            data.close(t, i) = 100.0 + static_cast<double>(t);
        }
    }

    return HistoricalMarketData(
        makeDates(5),
        {"AAPL", "MSFT"},
        data
    );
}

} // namespace

TEST(StrategyContextTest, CurrentBarMatchesCurrentIndex) {
    HistoricalMarketData market_data = makeMarketData();
    StrategyContext context(market_data, 2);

    EXPECT_DOUBLE_EQ(context.close(0, 0), 102.0);
}

TEST(StrategyContextTest, HistoricalBarLooksIntoThePast) {
    HistoricalMarketData market_data = makeMarketData();
    StrategyContext context(market_data, 3);

    EXPECT_DOUBLE_EQ(context.close(0, 0), 103.0);
    EXPECT_DOUBLE_EQ(context.close(0, 1), 102.0);
    EXPECT_DOUBLE_EQ(context.close(0, 3), 100.0);
}

TEST(StrategyContextTest, LookbackNeverReturnsFutureData) {
    HistoricalMarketData market_data = makeMarketData();
    StrategyContext context(market_data, 0);

    EXPECT_DOUBLE_EQ(context.close(0, 0), 100.0);
    EXPECT_THROW(context.close(0, 1), std::out_of_range);
}

TEST(StrategyContextTest, BarByInstrumentId) {
    HistoricalMarketData market_data = makeMarketData();
    StrategyContext context(market_data, 1);

    const Bar bar = context.bar("MSFT", 1);
    EXPECT_DOUBLE_EQ(bar.close, 100.0);
}

TEST(StrategyContextTest, CurrentDateMatchesCurrentIndex) {
    HistoricalMarketData market_data = makeMarketData();
    StrategyContext context(market_data, 2);

    EXPECT_EQ(context.currentDate(), market_data.date(2));
}

TEST(StrategyContextTest, OutOfRangeCurrentIndexThrows) {
    HistoricalMarketData market_data = makeMarketData();

    EXPECT_THROW(
        StrategyContext(market_data, 5),
        std::out_of_range
    );
}

TEST(StrategyContextTest, UnknownInstrumentThrows) {
    HistoricalMarketData market_data = makeMarketData();
    StrategyContext context(market_data, 2);

    EXPECT_THROW(context.bar("UNKNOWN", 0), std::out_of_range);
}
