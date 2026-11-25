#include "market_data/historical_market_data.hpp"
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

OHLCVData makeValidData(std::size_t rows, std::size_t cols) {
    OHLCVData data;
    const auto r = static_cast<Eigen::Index>(rows);
    const auto c = static_cast<Eigen::Index>(cols);

    data.open = MarketMatrix::Constant(r, c, 100.0);
    data.high = MarketMatrix::Constant(r, c, 110.0);
    data.low = MarketMatrix::Constant(r, c, 90.0);
    data.close = MarketMatrix::Constant(r, c, 105.0);
    data.volume = MarketMatrix::Constant(r, c, 1000.0);

    return data;
}

} // namespace

TEST(HistoricalMarketDataTest, ValidConstruction) {
    HistoricalMarketData market_data(
        makeDates(3),
        {"AAPL", "MSFT"},
        makeValidData(3, 2)
    );

    EXPECT_EQ(market_data.numDates(), 3u);
    EXPECT_EQ(market_data.numInstruments(), 2u);
}

TEST(HistoricalMarketDataTest, EmptyDatesThrows) {
    EXPECT_THROW(
        HistoricalMarketData(
            {},
            {"AAPL"},
            makeValidData(0, 1)
        ),
        std::invalid_argument
    );
}

TEST(HistoricalMarketDataTest, EmptyInstrumentsThrows) {
    EXPECT_THROW(
        HistoricalMarketData(
            makeDates(3),
            {},
            makeValidData(3, 0)
        ),
        std::invalid_argument
    );
}

TEST(HistoricalMarketDataTest, DuplicateInstrumentIdsThrows) {
    EXPECT_THROW(
        HistoricalMarketData(
            makeDates(3),
            {"AAPL", "AAPL"},
            makeValidData(3, 2)
        ),
        std::invalid_argument
    );
}

TEST(HistoricalMarketDataTest, UnsortedDatesThrows) {
    auto dates = makeDates(3);
    std::swap(dates[0], dates[1]);

    EXPECT_THROW(
        HistoricalMarketData(
            dates,
            {"AAPL"},
            makeValidData(3, 1)
        ),
        std::invalid_argument
    );
}

TEST(HistoricalMarketDataTest, InvalidMatrixDimensionsThrows) {
    EXPECT_THROW(
        HistoricalMarketData(
            makeDates(3),
            {"AAPL", "MSFT"},
            makeValidData(3, 1)
        ),
        std::invalid_argument
    );
}

TEST(HistoricalMarketDataTest, NonFiniteOHLCThrows) {
    auto data = makeValidData(3, 1);
    data.close(1, 0) = std::numeric_limits<double>::quiet_NaN();

    EXPECT_THROW(
        HistoricalMarketData(
            makeDates(3),
            {"AAPL"},
            data
        ),
        std::invalid_argument
    );
}

TEST(HistoricalMarketDataTest, NonPositiveOHLCThrows) {
    auto data = makeValidData(3, 1);
    data.low(1, 0) = 0.0;

    EXPECT_THROW(
        HistoricalMarketData(
            makeDates(3),
            {"AAPL"},
            data
        ),
        std::invalid_argument
    );
}

TEST(HistoricalMarketDataTest, NegativeVolumeThrows) {
    auto data = makeValidData(3, 1);
    data.volume(1, 0) = -1.0;

    EXPECT_THROW(
        HistoricalMarketData(
            makeDates(3),
            {"AAPL"},
            data
        ),
        std::invalid_argument
    );
}

TEST(HistoricalMarketDataTest, HighBelowOpenThrows) {
    auto data = makeValidData(3, 1);
    data.high(1, 0) = data.open(1, 0) - 1.0;

    EXPECT_THROW(
        HistoricalMarketData(
            makeDates(3),
            {"AAPL"},
            data
        ),
        std::invalid_argument
    );
}

TEST(HistoricalMarketDataTest, LowAboveCloseThrows) {
    auto data = makeValidData(3, 1);
    data.low(1, 0) = data.close(1, 0) + 1.0;

    EXPECT_THROW(
        HistoricalMarketData(
            makeDates(3),
            {"AAPL"},
            data
        ),
        std::invalid_argument
    );
}

TEST(HistoricalMarketDataTest, BarByIndex) {
    HistoricalMarketData market_data(
        makeDates(3),
        {"AAPL", "MSFT"},
        makeValidData(3, 2)
    );

    const Bar bar = market_data.bar(1, 1);

    EXPECT_DOUBLE_EQ(bar.open, 100.0);
    EXPECT_DOUBLE_EQ(bar.high, 110.0);
    EXPECT_DOUBLE_EQ(bar.low, 90.0);
    EXPECT_DOUBLE_EQ(bar.close, 105.0);
    EXPECT_DOUBLE_EQ(bar.volume, 1000.0);
}

TEST(HistoricalMarketDataTest, BarByInstrumentId) {
    HistoricalMarketData market_data(
        makeDates(3),
        {"AAPL", "MSFT"},
        makeValidData(3, 2)
    );

    const Bar bar = market_data.bar(0, "MSFT");

    EXPECT_DOUBLE_EQ(bar.close, 105.0);
}

TEST(HistoricalMarketDataTest, ClosePrice) {
    HistoricalMarketData market_data(
        makeDates(3),
        {"AAPL"},
        makeValidData(3, 1)
    );

    EXPECT_DOUBLE_EQ(market_data.close(2, 0), 105.0);
}

TEST(HistoricalMarketDataTest, UnknownInstrumentThrows) {
    HistoricalMarketData market_data(
        makeDates(3),
        {"AAPL"},
        makeValidData(3, 1)
    );

    EXPECT_THROW(
        market_data.instrumentIndex("MSFT"),
        std::out_of_range
    );
}

TEST(HistoricalMarketDataTest, OutOfRangeIndexThrows) {
    HistoricalMarketData market_data(
        makeDates(3),
        {"AAPL"},
        makeValidData(3, 1)
    );

    EXPECT_THROW(market_data.bar(3, 0), std::out_of_range);
    EXPECT_THROW(market_data.bar(0, 1), std::out_of_range);
    EXPECT_THROW(market_data.close(3, 0), std::out_of_range);
    EXPECT_THROW(market_data.date(3), std::out_of_range);
}
