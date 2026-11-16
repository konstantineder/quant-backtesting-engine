#pragma once

#include <cstddef>
#include <span>

// Summary statistics of an equity curve.
//
// Returns are simple per-bar returns of the equity curve. `volatility` and
// `sharpe_ratio` are annualized with the caller's bars-per-year factor and
// assume a zero risk-free rate; `max_drawdown` is a positive fraction of
// the running peak.
struct PerformanceMetrics {
    double total_return = 0.0;
    double max_drawdown = 0.0;
    double volatility = 0.0;
    double sharpe_ratio = 0.0;
    std::size_t num_bars = 0;
};

// Default for daily bars.
inline constexpr double kTradingDaysPerYear = 252.0;

// Computes the metrics of an equity curve.
//
// The curve must stay strictly positive: a wiped-out account makes simple
// returns meaningless, so that case is rejected here rather than silently
// producing nonsense. An empty curve yields zeroed metrics, and a
// single-point curve has no returns and therefore no volatility.
PerformanceMetrics computePerformanceMetrics(
    std::span<const double> equity_curve,
    double bars_per_year = kTradingDaysPerYear
);
