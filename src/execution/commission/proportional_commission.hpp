#pragma once

#include "execution/commission/commission_policy.hpp"

#include <cmath>

// Commission proportional to the traded notional, quoted in basis points:
//
//     notional = |quantity| * execution_price * contract_multiplier
//     fee      = notional * bps / 10'000
//
// The notional is absolute, so a buy and a sell of the same size cost the
// same -- a fee is never a credit. The multiplier belongs in the notional
// because that is what one contract actually trades: two futures
// contracts on 100 units each are charged like 200 units, not like two.
//
// Proportional rather than per-trade or per-share, which is how most
// brokers quote institutional equity commission. A flat or tiered
// schedule would be a separate model satisfying the same contract.
//
// The rate is a run-time value, the model type a compile-time one -- so a
// parameter sweep over rates instantiates this template exactly once.
//
// NUMERIC LIMITATION: the notional is formed before the rate is applied,
// so an execution whose notional overflows is rejected even where the
// mathematical fee would be representable. Rescaling to avoid that is a
// deliberate non-goal here; the inputs it would take are orders of
// magnitude beyond anything tradable.
class ProportionalCommission {
public:
    // `bps` must be finite and non-negative. Zero is allowed and makes
    // this model equivalent to NoCommission, which is what a sweep's
    // baseline needs.
    explicit ProportionalCommission(double bps) : bps_(bps) {
        commission::validateBps(bps_);
    }

    [[nodiscard]]
    double calculate(
        double execution_price,
        double signed_quantity,
        double contract_multiplier
    ) const {
        commission::validateInputs(
            execution_price, signed_quantity, contract_multiplier
        );

        // A zero rate charges nothing whatever the notional is, and
        // saying so here is not an optimization -- it is the only way to
        // get the right answer. Computing it would form the notional
        // first, and on an extreme-but-valid execution that overflows to
        // infinity; infinity * 0.0 is NaN, so a fee-free configuration
        // would fail on inputs a charged one handles.
        if (bps_ == 0.0) {
            return 0.0;
        }

        const double notional =
            std::abs(signed_quantity) * execution_price *
            contract_multiplier;

        return commission::validated(
            notional * (bps_ / kBasisPointsPerUnit)
        );
    }

    [[nodiscard]]
    double bps() const noexcept { return bps_; }

private:
    static constexpr double kBasisPointsPerUnit = 10'000.0;

    double bps_;
};

static_assert(
    CommissionPolicy<ProportionalCommission>,
    "ProportionalCommission must satisfy the CommissionPolicy contract"
);
