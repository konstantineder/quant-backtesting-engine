#include "market_data/market_context.hpp"
#include <gtest/gtest.h>
#include <chrono>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace {

// These providers deliberately accept all arguments so the context must validate them.
class PermissiveYieldCurve final : public YieldCurve {
public:
    double discountFactor(double maturity) const override {
        return 1.0 + 0.01 * maturity;
    }

    double zeroRate(double maturity) const override {
        return -0.02 + 0.001 * maturity;
    }
};

class PermissiveVolatilitySurface final : public VolatilitySurface {
public:
    double impliedVolatility(double strike, double maturity) const override {
        return 0.001 * strike + 0.1 * maturity;
    }
};

} // namespace

class MarketContextTest : public ::testing::Test {
protected:
    std::chrono::sys_days valuation_date{std::chrono::year{2020}/1/2};
    MarketSnapshot snapshot;
    std::shared_ptr<const YieldCurve> curve = std::make_shared<PermissiveYieldCurve>();
    std::unordered_map<std::string, std::shared_ptr<const VolatilitySurface>> surfaces{
        {"AAPL", std::make_shared<PermissiveVolatilitySurface>()}
    };
    std::unordered_map<std::string, double> dividend_yields{{"AAPL", -0.01}};

    void SetUp() override {
        snapshot.updatePrice("AAPL", 120.0);
    }

    MarketContext makeContext() const {
        return MarketContext{valuation_date, snapshot, surfaces, curve, dividend_yields};
    }
};

TEST_F(MarketContextTest, PreservesHistoricalDateAndDelegatesQueries) {
    const auto context = makeContext();

    EXPECT_EQ(context.valuationDate(), valuation_date);
    EXPECT_DOUBLE_EQ(context.spot("AAPL"), 120.0);
    EXPECT_NEAR(context.impliedVolatility("AAPL", 100.0, 2.0), 0.3, 1e-12);
    EXPECT_NEAR(context.discountFactor(2.0), 1.02, 1e-12);
    EXPECT_NEAR(context.zeroRate(2.0), -0.018, 1e-12);
    EXPECT_DOUBLE_EQ(context.dividendYield("AAPL"), -0.01);
}

TEST_F(MarketContextTest, RejectsNullDiscountCurve) {
    curve.reset();
    EXPECT_THROW(makeContext(), std::invalid_argument);
}

TEST_F(MarketContextTest, RejectsNullVolatilitySurface) {
    surfaces.at("AAPL").reset();
    EXPECT_THROW(makeContext(), std::invalid_argument);
}

TEST_F(MarketContextTest, RejectsEmptyMapKeys) {
    surfaces.emplace("", std::make_shared<PermissiveVolatilitySurface>());
    EXPECT_THROW(makeContext(), std::invalid_argument);

    surfaces.erase("");
    dividend_yields.emplace("", 0.0);
    EXPECT_THROW(makeContext(), std::invalid_argument);
}

TEST_F(MarketContextTest, RejectsNonfiniteDividendYields) {
    const double infinity = std::numeric_limits<double>::infinity();
    for (double value : {std::numeric_limits<double>::quiet_NaN(), infinity, -infinity}) {
        dividend_yields.at("AAPL") = value;
        EXPECT_THROW(makeContext(), std::invalid_argument);
    }
}

TEST_F(MarketContextTest, AcceptsFiniteDividendYields) {
    for (double value : {-0.01, 0.0, 0.03}) {
        dividend_yields.at("AAPL") = value;
        const auto context = makeContext();
        EXPECT_DOUBLE_EQ(context.dividendYield("AAPL"), value);
    }
}

TEST_F(MarketContextTest, AcceptsEmptyMarketInputsAndEpochDate) {
    const MarketContext context{std::chrono::sys_days{}, MarketSnapshot{}, {}, curve, {}};

    EXPECT_EQ(context.valuationDate(), std::chrono::sys_days{});
    EXPECT_DOUBLE_EQ(context.discountFactor(0.0), 1.0);
}

TEST_F(MarketContextTest, RejectsEmptyQueryIdentifiers) {
    const auto context = makeContext();

    EXPECT_THROW(context.spot(""), std::invalid_argument);
    EXPECT_THROW(context.impliedVolatility("", 100.0, 1.0), std::invalid_argument);
    EXPECT_THROW(context.dividendYield(""), std::invalid_argument);
}

TEST_F(MarketContextTest, MissingIdentifiersThrowOutOfRange) {
    const auto context = makeContext();

    EXPECT_THROW(context.spot("MSFT"), std::out_of_range);
    EXPECT_THROW(context.impliedVolatility("MSFT", 100.0, 1.0), std::out_of_range);
    EXPECT_DOUBLE_EQ(context.dividendYield("MSFT"), 0.0);
}

TEST_F(MarketContextTest, RejectsInvalidStrikes) {
    const auto context = makeContext();
    const double infinity = std::numeric_limits<double>::infinity();
    for (double strike : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(), infinity, -infinity}) {
        EXPECT_THROW(context.impliedVolatility("AAPL", strike, 1.0), std::invalid_argument);
    }
}

TEST_F(MarketContextTest, RejectsInvalidMaturities) {
    const auto context = makeContext();
    const double infinity = std::numeric_limits<double>::infinity();
    for (double maturity : {-1.0, std::numeric_limits<double>::quiet_NaN(), infinity, -infinity}) {
        EXPECT_THROW(context.impliedVolatility("AAPL", 100.0, maturity), std::invalid_argument);
        EXPECT_THROW(context.discountFactor(maturity), std::invalid_argument);
        EXPECT_THROW(context.zeroRate(maturity), std::invalid_argument);
    }
}

TEST_F(MarketContextTest, AcceptsZeroMaturity) {
    const auto context = makeContext();

    EXPECT_NEAR(context.impliedVolatility("AAPL", 100.0, 0.0), 0.1, 1e-12);
    EXPECT_DOUBLE_EQ(context.discountFactor(0.0), 1.0);
    EXPECT_DOUBLE_EQ(context.zeroRate(0.0), -0.02);
}
