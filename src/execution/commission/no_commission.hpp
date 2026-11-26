#pragma once

#include "execution/commission/commission_policy.hpp"

// Fee-free execution: the broker charges nothing.
//
// The reference point other models are measured against, and the honest
// statement that a backtest made no assumption about transaction costs.
// It still validates its inputs, so moving from this model to a charged
// one cannot change which executions a run accepts.
//
// Stateless, so it is empty: as a [[no_unique_address]] member of
// ExecutionModel it costs no storage, and the call inlines to nothing.
class NoCommission {
public:
    [[nodiscard]]
    double calculate(
        double execution_price,
        double signed_quantity,
        double contract_multiplier
    ) const {
        commission::validateInputs(
            execution_price, signed_quantity, contract_multiplier
        );
        return 0.0;
    }
};

static_assert(
    CommissionPolicy<NoCommission>,
    "NoCommission must satisfy the CommissionPolicy contract"
);
