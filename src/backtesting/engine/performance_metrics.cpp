#include "backtesting/engine/performance_metrics.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

PerformanceMetrics computePerformanceMetrics(
    std::span<const double> equity_curve,
    double bars_per_year
) {
    if (!std::isfinite(bars_per_year) || bars_per_year <= 0.0) {
        throw std::invalid_argument(
            "Bars per year must be finite and positive"
        );
    }

    PerformanceMetrics metrics{};
    metrics.num_bars = equity_curve.size();

    if (equity_curve.empty()) {
        return metrics;
    }

    for (const double equity : equity_curve) {
        if (!std::isfinite(equity) || equity <= 0.0) {
            throw std::invalid_argument(
                "Equity curve must be finite and strictly positive"
            );
        }
    }

    metrics.total_return =
        equity_curve.back() / equity_curve.front() - 1.0;

    double peak = equity_curve.front();
    double sum_returns = 0.0;
    double sum_squared_returns = 0.0;

    for (std::size_t bar = 1; bar < equity_curve.size(); ++bar) {
        const double equity = equity_curve[bar];

        peak = std::max(peak, equity);
        metrics.max_drawdown =
            std::max(metrics.max_drawdown, (peak - equity) / peak);

        const double bar_return = equity / equity_curve[bar - 1] - 1.0;
        sum_returns += bar_return;
        sum_squared_returns += bar_return * bar_return;
    }

    const std::size_t num_returns = equity_curve.size() - 1;

    // A single sample has no dispersion, so volatility stays zero.
    if (num_returns < 2) {
        return metrics;
    }

    const double count = static_cast<double>(num_returns);
    const double mean = sum_returns / count;

    // Sample variance (Bessel-corrected), clamped against the rounding
    // noise that can push an all-equal series slightly below zero.
    const double variance = std::max(
        (sum_squared_returns - count * mean * mean) / (count - 1.0),
        0.0
    );

    const double stdev = std::sqrt(variance);

    metrics.volatility = stdev * std::sqrt(bars_per_year);

    // Undefined without dispersion: reported as zero rather than inf.
    metrics.sharpe_ratio = stdev > 0.0
        ? (mean / stdev) * std::sqrt(bars_per_year)
        : 0.0;

    if (!std::isfinite(metrics.total_return) ||
        !std::isfinite(metrics.volatility) ||
        !std::isfinite(metrics.sharpe_ratio)) {
        throw std::overflow_error(
            "Performance metrics cannot be represented"
        );
    }

    return metrics;
}
