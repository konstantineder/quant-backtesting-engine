#pragma once

#include "strategy/strategy.hpp"

#include <cstddef>
#include <memory>
#include <optional>

// Long-only SMA crossover: holds the instrument with full weight while the
// short moving average is above the long one, and stays flat otherwise.
//
// Contract: an instance processes bars in strictly ascending time order, at
// most once per bar. A new backtest uses a fresh instance -- replaying a bar
// or stepping back in history yields undefined signals, because the strategy
// only reports target weights when its position changes.
class SmaCrossoverStrategy final : public Strategy {
public:
    SmaCrossoverStrategy(
        std::size_t instrument_index,
        std::size_t short_window,
        std::size_t long_window
    );

    // Writes target weights into `out` and returns true only when the
    // position changes: on the first valid bar, and on every switch between
    // long and cash. Returns false during warmup and while the position is
    // unchanged, leaving `out` untouched.
    bool onBar(
        const StrategyContext& context,
        TargetWeights& out
    ) override;

    // Copies the configuration but not the signal state, so the clone emits
    // again on its first valid bar.
    std::unique_ptr<Strategy> clone() const override;

    std::size_t instrumentIndex() const noexcept {
        return instrument_index_;
    }

    std::size_t shortWindow() const noexcept {
        return short_window_;
    }

    std::size_t longWindow() const noexcept {
        return long_window_;
    }

private:
    double computeSma(
        std::size_t window,
        const StrategyContext& context
    ) const;

    std::size_t instrument_index_;
    std::size_t short_window_;
    std::size_t long_window_;

    // Empty until the first valid bar; then true for long, false for cash.
    std::optional<bool> previous_signal_;
};
