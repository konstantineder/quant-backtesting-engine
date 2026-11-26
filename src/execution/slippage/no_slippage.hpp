#pragma once

#include "execution/slippage/slippage_policy.hpp"

// Frictionless execution: an order trades at the quoted price.
//
// Not a null object to be optimized away silently -- it is the reference
// point every other model is measured against, and the honest statement
// that a backtest made no assumption about market impact. It still
// validates its inputs, so moving from this model to a realistic one
// cannot change which quotes a run accepts.
//
// Stateless, so it is empty: as a [[no_unique_address]] member of
// ExecutionModel it costs no storage, and the call inlines to nothing.
class NoSlippage {
public:
    [[nodiscard]]
    double executionPrice(
        double reference_price,
        double signed_quantity
    ) const {
        slippage::validateInputs(reference_price, signed_quantity);
        return reference_price;
    }
};

static_assert(
    SlippagePolicy<NoSlippage>,
    "NoSlippage must satisfy the SlippagePolicy contract"
);
