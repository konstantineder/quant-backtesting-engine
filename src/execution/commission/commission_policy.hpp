#pragma once

#include <cmath>
#include <concepts>
#include <stdexcept>

// The compile-time contract every commission model satisfies.
//
// A model answers one question: what does the broker charge for this
// execution? It is handed three primitives -- the price actually executed
// at, the signed quantity and the instrument's contract multiplier -- and
// deliberately not an Order, an Instrument or an Account. Fees are then
// computable without the model knowing anything about the trading domain,
// which keeps it testable in isolation and reusable outside a backtest.
//
// The price passed in is the price AFTER slippage. Slippage is already
// carried by the execution price itself, so a model that also folded it
// into the fee would charge for it twice.
//
// Unlike slippage, commission does not depend on the direction of the
// trade: a buy and a sell of the same size cost the same.
template<typename T>
concept CommissionPolicy = requires(
    const T& model,
    double execution_price,
    double signed_quantity,
    double contract_multiplier
) {
    {
        model.calculate(
            execution_price,
            signed_quantity,
            contract_multiplier
        )
    } -> std::same_as<double>;
};

// Validation shared by every model.
//
// Shared on purpose: if each model validated for itself, switching a
// commission-free backtest to a charged one could change which executions
// a run accepts, and a bad quote or multiplier would surface in one
// configuration but not another. These functions are part of the contract
// above, not an implementation detail of any one model.
namespace commission {

inline void validateBps(double bps) {
    // No upper bound, unlike slippage. A rate above 10'000 bps charges
    // more than the notional, which is economically odd but perfectly
    // well defined -- and not this layer's business to forbid. The reason
    // slippage needs a bound is that beyond it a SELL price turns
    // negative, and no such thing happens to a fee.
    if (!std::isfinite(bps) || bps < 0.0) {
        throw std::invalid_argument(
            "Commission in basis points must be finite and non-negative"
        );
    }
}

inline void validateInputs(
    double execution_price,
    double signed_quantity,
    double contract_multiplier
) {
    if (!std::isfinite(execution_price) || execution_price <= 0.0) {
        throw std::invalid_argument(
            "Commission execution price must be finite and positive"
        );
    }

    // Not because the direction would be unreadable -- commission ignores
    // direction -- but because a zero quantity means no trade happened,
    // and charging a fee on it, even a zero one, would be answering a
    // question nobody asked. Orders and fills reject zero as well.
    if (!std::isfinite(signed_quantity) || signed_quantity == 0.0) {
        throw std::invalid_argument(
            "Commission quantity must be finite and non-zero"
        );
    }

    // The multiplier scales one contract into units of the underlying, so
    // zero would make every notional vanish and a negative one would flip
    // the sign of the fee.
    if (!std::isfinite(contract_multiplier) ||
        contract_multiplier <= 0.0) {
        throw std::invalid_argument(
            "Contract multiplier must be finite and positive"
        );
    }
}

// Whatever a model computes must still be a chargeable fee.
//
// Note how this differs from the slippage counterpart: zero IS valid here.
// A zero rate charges nothing, and a notional small enough to underflow
// rounds the fee away to nothing -- both are correct answers, not failures.
// What must never pass is a non-finite fee, which is what an intermediate
// overflow in the notional produces.
inline double validated(double fee) {
    if (!std::isfinite(fee) || fee < 0.0) {
        throw std::overflow_error(
            "Commission is not representable as a non-negative fee"
        );
    }
    return fee;
}

}  // namespace commission
