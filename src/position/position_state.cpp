#include "position/position_state.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {

bool sameSign(double lhs, double rhs) noexcept {
    return (lhs > 0.0) == (rhs > 0.0);
}

} // namespace

PositionState::PositionState(
    double quantity,
    double average_price,
    double realized_pnl
): quantity_(quantity),
   average_price_(average_price),
   realized_pnl_(realized_pnl) {
    validate(quantity_, average_price_, realized_pnl_);
}

void PositionState::validate(
    double quantity,
    double average_price,
    double realized_pnl
) {
    if (!std::isfinite(quantity)) {
        throw std::invalid_argument(
            "Position quantity must be finite"
        );
    }
    if (!std::isfinite(average_price) || average_price < 0.0) {
        throw std::invalid_argument(
            "Average price must be finite and non-negative"
        );
    }
    if (!std::isfinite(realized_pnl)) {
        throw std::invalid_argument(
            "Realized PnL must be finite"
        );
    }
    if (quantity == 0.0 && average_price != 0.0) {
        throw std::invalid_argument(
            "Average price must be zero while flat"
        );
    }
    if (quantity != 0.0 && average_price == 0.0) {
        throw std::invalid_argument(
            "An open position requires a positive average price"
        );
    }
}

double PositionState::applyFill(
    double quantity,
    double price,
    double contract_multiplier
) {
    if (!std::isfinite(quantity) || quantity == 0.0) {
        throw std::invalid_argument(
            "Fill quantity must be finite and non-zero"
        );
    }
    if (!std::isfinite(price) || price <= 0.0) {
        throw std::invalid_argument(
            "Fill price must be finite and positive"
        );
    }
    if (!std::isfinite(contract_multiplier) ||
        contract_multiplier <= 0.0) {
        throw std::invalid_argument(
            "Contract multiplier must be finite and positive"
        );
    }

    double new_quantity = quantity_ + quantity;
    double new_average_price = average_price_;
    double realized_delta = 0.0;

    if (isFlat()) {
        // Opening a fresh long or short.
        new_average_price = price;
    } else if (sameSign(quantity_, quantity)) {
        // Increasing: weighted average over absolute sizes.
        const double open_size = std::abs(quantity_);
        const double added_size = std::abs(quantity);
        const double total_cost =
            average_price_ * open_size + price * added_size;

        new_average_price = total_cost / (open_size + added_size);
    } else {
        // Reducing, closing or reversing: the overlapping part realizes.
        const double closed_size =
            std::min(std::abs(quantity_), std::abs(quantity));
        const double direction = quantity_ > 0.0 ? 1.0 : -1.0;

        realized_delta =
            (price - average_price_) * closed_size * direction *
            contract_multiplier;

        if (new_quantity == 0.0) {
            new_average_price = 0.0;
        } else if (!sameSign(quantity_, new_quantity)) {
            // Reversal: the surplus opens a new position at this price.
            new_average_price = price;
        }
        // Partial close keeps the existing average price.
    }

    const double new_realized_pnl = realized_pnl_ + realized_delta;

    if (!std::isfinite(realized_delta) ||
        !std::isfinite(new_realized_pnl)) {
        throw std::overflow_error(
            "Realized PnL cannot be represented"
        );
    }

    validate(new_quantity, new_average_price, new_realized_pnl);

    // Commit: nothing below can throw.
    quantity_ = new_quantity;
    average_price_ = new_average_price;
    realized_pnl_ = new_realized_pnl;

    return realized_delta;
}
