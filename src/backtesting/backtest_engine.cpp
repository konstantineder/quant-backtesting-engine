#include "backtesting/backtest_engine.hpp"

#include <cmath>
#include <stdexcept>

// Only the non-template members live here. BacktestEngine::run is a
// template and its definition has to stay visible to every translation
// unit that instantiates it, which is what backtest_engine.tpp is for --
// but the constructor is an ordinary function, and defining it in a header
// would give every including TU its own copy of the symbol.

namespace {

void validateConfig(const BacktestConfig& config) {
    if (!std::isfinite(config.initial_cash) || config.initial_cash <= 0.0) {
        throw std::invalid_argument(
            "Initial cash must be finite and strictly positive"
        );
    }

    // computePerformanceMetrics rejects a bad factor too, but only once a
    // whole run has been simulated. Rejecting it here costs nothing and
    // fails at the point the mistake was actually made.
    if (!std::isfinite(config.bars_per_year) ||
        config.bars_per_year <= 0.0) {
        throw std::invalid_argument(
            "Bars per year must be finite and strictly positive"
        );
    }
}

} // namespace

BacktestEngine::BacktestEngine(BacktestConfig config)
    : config_(config) {
    validateConfig(config_);
}
