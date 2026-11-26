#pragma once

#include <cmath>
#include <concepts>
#include <stdexcept>

// The compile-time contract every slippage model satisfies.
//
// A model answers one question: given the price the market is quoting and
// the signed quantity about to trade, what price does this order actually
// get? Direction comes from the sign of the quantity alone -- a buy pays
// up, a sell receives less -- so no separate side flag is needed and the
// two cannot disagree.
//
// Deliberately a concept rather than a virtual base. The model is a
// template parameter of ExecutionModel, so the call is resolved at compile
// time and a stateless model costs nothing at run time; a vtable would put
// an indirect call in the innermost loop of the simulation.
template<typename T>
concept SlippagePolicy = requires(
    const T& model,
    double reference_price,
    double signed_quantity
) {
    {
        model.executionPrice(
            reference_price,
            signed_quantity
        )
    } -> std::same_as<double>;
};

// Input and output validation shared by every model.
//
// Shared on purpose: if each model validated for itself, swapping a
// frictionless model for a realistic one could change which inputs a
// backtest accepts, and a bad quote would surface in one configuration but
// not another. These two functions are part of the contract above, not an
// implementation detail of any one model.
namespace slippage {

// A reference price is a tradable price and a signed quantity carries the
// direction, so neither may be absent, infinite or zero.
inline void validateInputs(
    double reference_price,
    double signed_quantity
) {
    if (!std::isfinite(reference_price) || reference_price <= 0.0) {
        throw std::invalid_argument(
            "Slippage reference price must be finite and positive"
        );
    }

    // Zero has no sign to read a direction from, so the adjustment would
    // be undefined rather than merely zero. Orders and fills reject a zero
    // quantity as well, so nothing legitimate reaches here with one.
    if (!std::isfinite(signed_quantity) || signed_quantity == 0.0) {
        throw std::invalid_argument(
            "Slippage quantity must be finite and non-zero"
        );
    }
}

// Whatever a model computes must still be a tradable price. A buy
// adjustment can overflow to infinity on an extreme reference price, and a
// sell adjustment can underflow to zero on a subnormal one -- both would
// otherwise travel on as a Fill price and be rejected much later, with no
// hint of which model produced them.
inline double validated(double execution_price) {
    if (!std::isfinite(execution_price) || execution_price <= 0.0) {
        throw std::overflow_error(
            "Slippage produced a price that is not tradable"
        );
    }
    return execution_price;
}

}  // namespace slippage
