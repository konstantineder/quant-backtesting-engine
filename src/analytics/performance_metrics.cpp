#include "analytics/performance_metrics.hpp"

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

    // Welford's online algorithm, rather than accumulating the returns and
    // their squares and subtracting at the end. The textbook identity
    // s^2 = (sum r_i^2 - n * mean^2) / (n - 1) subtracts two nearly equal
    // quantities whenever the mean dominates the dispersion -- exactly the
    // case for bar returns, which sit at 1e-3 around a mean of the same
    // order. The cancellation there can eat most of the significant digits
    // and even turn the variance negative. Welford keeps the running mean
    // and the sum of squared deviations from it, so no such subtraction
    // ever happens, at the same O(T) time and O(1) space.
    double mean = 0.0;
    double m2 = 0.0;
    std::size_t count = 0;

    for (std::size_t bar = 1; bar < equity_curve.size(); ++bar) {
        const double equity = equity_curve[bar];

        peak = std::max(peak, equity);
        metrics.max_drawdown =
            std::max(metrics.max_drawdown, (peak - equity) / peak);

        const double bar_return = equity / equity_curve[bar - 1] - 1.0;

        ++count;
        const double delta = bar_return - mean;
        mean += delta / static_cast<double>(count);
        const double delta_after = bar_return - mean;
        m2 += delta * delta_after;
    }

    // Bessel-corrected sample variance. A single return carries no
    // dispersion, so volatility and Sharpe stay at their zero defaults.
    if (count > 1) {
        const double stdev =
            std::sqrt(m2 / static_cast<double>(count - 1));

        metrics.volatility = stdev * std::sqrt(bars_per_year);

        // Undefined without dispersion: reported as zero rather than inf.
        // Assumes a zero risk-free rate, as the header states.
        metrics.sharpe_ratio = stdev > 0.0
            ? (mean / stdev) * std::sqrt(bars_per_year)
            : 0.0;
    }

    // Unconditional: a two-point curve computes no volatility but still has
    // a total return, and that one division can overflow on its own. Gating
    // this on the volatility branch let an infinite total_return through.
    if (!std::isfinite(metrics.total_return) ||
        !std::isfinite(metrics.volatility) ||
        !std::isfinite(metrics.sharpe_ratio)) {
        throw std::overflow_error(
            "Performance metrics cannot be represented"
        );
    }

    return metrics;
}
