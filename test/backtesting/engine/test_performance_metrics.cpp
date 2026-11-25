#include "backtesting/backtest_result.hpp"
#include "analytics/performance_metrics.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

constexpr double kThreshold = 1e-9;

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

} // namespace

TEST(PerformanceMetricsTest, EmptyCurveYieldsZeroedMetrics) {
    const PerformanceMetrics metrics =
        computePerformanceMetrics(std::span<const double>{});

    EXPECT_EQ(metrics.num_bars, 0u);
    EXPECT_DOUBLE_EQ(metrics.total_return, 0.0);
    EXPECT_DOUBLE_EQ(metrics.max_drawdown, 0.0);
    EXPECT_DOUBLE_EQ(metrics.volatility, 0.0);
    EXPECT_DOUBLE_EQ(metrics.sharpe_ratio, 0.0);
}

TEST(PerformanceMetricsTest, SinglePointCurveHasNoReturns) {
    const std::vector<double> curve{100.0};
    const PerformanceMetrics metrics = computePerformanceMetrics(curve);

    EXPECT_EQ(metrics.num_bars, 1u);
    EXPECT_DOUBLE_EQ(metrics.total_return, 0.0);
    EXPECT_DOUBLE_EQ(metrics.volatility, 0.0);
}

TEST(PerformanceMetricsTest, ComputesTotalReturn) {
    const std::vector<double> curve{100.0, 110.0, 120.0};
    const PerformanceMetrics metrics = computePerformanceMetrics(curve);

    EXPECT_NEAR(metrics.total_return, 0.2, kThreshold);
    EXPECT_EQ(metrics.num_bars, 3u);
}

TEST(PerformanceMetricsTest, ComputesMaxDrawdownFromTheRunningPeak) {
    // Peak 120, trough 90 -> 25% drawdown; recovers afterwards.
    const std::vector<double> curve{
        100.0, 120.0, 110.0, 90.0, 130.0
    };
    const PerformanceMetrics metrics = computePerformanceMetrics(curve);

    EXPECT_NEAR(metrics.max_drawdown, 0.25, kThreshold);
    EXPECT_NEAR(metrics.total_return, 0.3, kThreshold);
}

TEST(PerformanceMetricsTest, MonotonicCurveHasNoDrawdown) {
    const std::vector<double> curve{100.0, 101.0, 102.0, 103.0};
    EXPECT_DOUBLE_EQ(
        computePerformanceMetrics(curve).max_drawdown, 0.0
    );
}

TEST(PerformanceMetricsTest, ConstantCurveHasNoVolatilityOrSharpe) {
    const std::vector<double> curve{100.0, 100.0, 100.0, 100.0};
    const PerformanceMetrics metrics = computePerformanceMetrics(curve);

    EXPECT_DOUBLE_EQ(metrics.total_return, 0.0);
    EXPECT_DOUBLE_EQ(metrics.volatility, 0.0);
    // Zero dispersion: reported as zero rather than infinite.
    EXPECT_DOUBLE_EQ(metrics.sharpe_ratio, 0.0);
}

TEST(PerformanceMetricsTest, AnnualizesVolatilityAndSharpe) {
    // Returns of +10%, -10%, +10% around a small positive mean.
    const std::vector<double> curve{100.0, 110.0, 99.0, 108.9};
    const PerformanceMetrics metrics =
        computePerformanceMetrics(curve, 252.0);

    const double expected_stdev = 0.11547005383792517;
    EXPECT_NEAR(
        metrics.volatility,
        expected_stdev * std::sqrt(252.0),
        1e-6
    );
    EXPECT_NEAR(
        metrics.sharpe_ratio,
        (1.0 / 30.0) / expected_stdev * std::sqrt(252.0),
        1e-6
    );
}

TEST(PerformanceMetricsTest, ScalesWithTheBarFrequency) {
    const std::vector<double> curve{100.0, 110.0, 99.0, 108.9};

    const PerformanceMetrics daily =
        computePerformanceMetrics(curve, 252.0);
    const PerformanceMetrics monthly =
        computePerformanceMetrics(curve, 12.0);

    EXPECT_NEAR(
        daily.volatility / monthly.volatility,
        std::sqrt(252.0 / 12.0),
        1e-9
    );
}

TEST(PerformanceMetricsTest, RejectsNonPositiveEquity) {
    const std::vector<double> wiped_out{100.0, 50.0, 0.0};
    EXPECT_THROW(
        computePerformanceMetrics(wiped_out), std::invalid_argument
    );

    const std::vector<double> negative{100.0, -10.0};
    EXPECT_THROW(
        computePerformanceMetrics(negative), std::invalid_argument
    );

    const std::vector<double> not_finite{
        100.0, std::numeric_limits<double>::quiet_NaN()
    };
    EXPECT_THROW(
        computePerformanceMetrics(not_finite), std::invalid_argument
    );
}

// A two-point curve has one return, so volatility and Sharpe are never
// computed -- but the total return is, and that single division can
// overflow on its own. The representability check must therefore not be
// gated on the volatility branch.
TEST(PerformanceMetricsTest, RejectsAnUnrepresentableTotalReturn) {
    const std::vector<double> curve{
        std::numeric_limits<double>::denorm_min(), 1e300
    };

    ASSERT_EQ(curve.size(), 2u);
    EXPECT_THROW(
        computePerformanceMetrics(curve), std::overflow_error
    );
}

TEST(PerformanceMetricsTest, RejectsInvalidBarFrequency) {
    const std::vector<double> curve{100.0, 110.0};
    EXPECT_THROW(
        computePerformanceMetrics(curve, 0.0), std::invalid_argument
    );
    EXPECT_THROW(
        computePerformanceMetrics(curve, -252.0), std::invalid_argument
    );
}

// -------------------------------------------------------- BacktestResult --

TEST(BacktestResultTest, ExposesCurveFillsValuationAndMetrics) {
    ExecutionLog log;
    log.record(Fill{
        OrderId{1}, InstrumentIndex{0}, 10.0, 100.0, 1.0,
        std::chrono::sys_days{std::chrono::year{2024} / 1 / 2}, 1
    });

    AccountValuation valuation{};
    valuation.cash = 500.0;
    valuation.position_market_value = 1200.0;
    valuation.equity = 1700.0;

    const BacktestResult result{
        makeDates(3), {100.0, 110.0, 120.0}, log, valuation
    };

    EXPECT_EQ(result.numBars(), 3u);
    EXPECT_EQ(result.dates().size(), 3u);
    EXPECT_EQ(result.equityCurve().size(), 3u);
    EXPECT_NEAR(result.equityCurve().back(), 120.0, kThreshold);

    ASSERT_EQ(result.fills().size(), 1u);
    EXPECT_DOUBLE_EQ(result.fills().front().quantity(), 10.0);
    EXPECT_EQ(result.executionLog().size(), 1u);

    EXPECT_NEAR(result.finalValuation().equity, 1700.0, kThreshold);
    EXPECT_NEAR(result.metrics().total_return, 0.2, kThreshold);
    EXPECT_EQ(result.metrics().num_bars, 3u);
}

TEST(BacktestResultTest, DefaultIsEmpty) {
    const BacktestResult result;

    EXPECT_EQ(result.numBars(), 0u);
    EXPECT_TRUE(result.fills().empty());
    EXPECT_EQ(result.metrics().num_bars, 0u);
    EXPECT_DOUBLE_EQ(result.finalValuation().equity, 0.0);
}

TEST(BacktestResultTest, RejectsMisalignedDatesAndCurve) {
    EXPECT_THROW(
        (BacktestResult{makeDates(2), {100.0}, {}, {}}),
        std::invalid_argument
    );
}
