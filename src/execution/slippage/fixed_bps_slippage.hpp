#pragma once

#include "execution/slippage/slippage_policy.hpp"

#include <cmath>
#include <stdexcept>

// Constant proportional slippage, quoted in basis points of the reference
// price and applied against the direction of the trade:
//
//     buy   ->  price * (1 + bps / 10'000)
//     sell  ->  price * (1 - bps / 10'000)
//
// Proportional rather than absolute, so one rate describes an instrument
// at 5 and at 5'000 without rescaling. Always adverse: slippage is a cost,
// and a model that could improve a price would let a backtest earn money
// from its own execution assumption.
//
// The rate is a run-time value, the model type a compile-time one -- so a
// parameter sweep over rates instantiates this template exactly once.
class FixedBpsSlippage {
public:
    // `bps` must be finite and in [0, 10'000). Zero is allowed and makes
    // this model equivalent to NoSlippage, which is what a sweep's
    // baseline needs. The upper bound is not cosmetic: at 10'000 bps a
    // sell executes at exactly zero and beyond it at a negative price, so
    // the rate itself is the configuration error -- catching it here beats
    // a rejected Fill thousands of bars later with no trace of the cause.
    explicit FixedBpsSlippage(double bps) : bps_(bps) {
        if (!std::isfinite(bps_) || bps_ < 0.0) {
            throw std::invalid_argument(
                "Slippage in basis points must be finite and non-negative"
            );
        }
        if (bps_ >= kBasisPointsPerUnit) {
            throw std::invalid_argument(
                "Slippage of 10'000 bps or more would make a sell price "
                "non-positive"
            );
        }
    }

    [[nodiscard]]
    double executionPrice(
        double reference_price,
        double signed_quantity
    ) const {
        slippage::validateInputs(reference_price, signed_quantity);

        const double rate = bps_ / kBasisPointsPerUnit;

        // The sign of the quantity is the direction, and std::signbit is
        // what reads it: a buy crosses the spread upwards, a sell
        // downwards. Validation above has already ruled out a zero, whose
        // direction would be unanswerable.
        const double factor = std::signbit(signed_quantity)
            ? 1.0 - rate
            : 1.0 + rate;

        return slippage::validated(reference_price * factor);
    }

    [[nodiscard]]
    double bps() const noexcept { return bps_; }

private:
    static constexpr double kBasisPointsPerUnit = 10'000.0;

    double bps_;
};

static_assert(
    SlippagePolicy<FixedBpsSlippage>,
    "FixedBpsSlippage must satisfy the SlippagePolicy contract"
);
