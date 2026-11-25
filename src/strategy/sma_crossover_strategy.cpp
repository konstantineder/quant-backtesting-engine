#include "strategy/sma_crossover_strategy.hpp"

#include <stdexcept>

SmaCrossoverStrategy::SmaCrossoverStrategy(
    std::size_t instrument_index,
    std::size_t short_window,
    std::size_t long_window
): instrument_index_(instrument_index),
   short_window_(short_window),
   long_window_(long_window) {
    if (short_window_ == 0 || long_window_ == 0) {
        throw std::invalid_argument(
            "SMA windows must be positive"
        );
    }

    if (short_window_ >= long_window_) {
        throw std::invalid_argument(
            "short_window must be smaller than long_window"
        );
    }
}

std::unique_ptr<Strategy> SmaCrossoverStrategy::clone() const {
    // Deliberately not a copy of *this: the clone starts without signal
    // state so it emits target weights on its own first valid bar.
    return std::make_unique<SmaCrossoverStrategy>(
        instrument_index_,
        short_window_,
        long_window_
    );
}

double SmaCrossoverStrategy::computeSma(
    std::size_t window,
    const StrategyContext& context
) const {
    double sum_prices = 0.0;
    for (std::size_t lookback = 0; lookback < window; ++lookback) {
        sum_prices += context.close(instrument_index_, lookback);
    }

    return sum_prices / static_cast<double>(window);
}

bool SmaCrossoverStrategy::onBar(
    const StrategyContext& context,
    TargetWeights& out
) {
    if (instrument_index_ >= context.numInstruments()) {
        throw std::out_of_range(
            "instrument_index out of range for market data"
        );
    }

    // The long SMA needs long_window_ bars including the current one;
    // long_window_ > 0 is guaranteed by the constructor.
    if (context.currentIndex() < long_window_ - 1) {
        return false;
    }

    const bool invested =
        computeSma(short_window_, context) >
        computeSma(long_window_, context);

    if (previous_signal_.has_value() &&
        *previous_signal_ == invested) {
        return false;
    }

    out.assign(context.numInstruments(), 0.0);
    out[instrument_index_] = invested ? 1.0 : 0.0;

    previous_signal_ = invested;
    return true;
}
