#pragma once

#include "common/enums.hpp"
#include "trading/instrument_id.hpp"
#include "trading/order_id.hpp"

#include <cmath>
#include <stdexcept>
#include <variant>

// Trading intent: what the strategy wants executed, with no execution
// detail attached. A quantity is signed -- positive buys, negative sells --
// so a short MarketOrder needs no separate flag. The matching execution is a
// Fill; an MarketOrder never carries a price, because the intent does not know
// what the market will give it.
class MarketOrder {
public:
    MarketOrder(OrderId order_id, InstrumentIndex instrument, double quantity)
        : order_id_(order_id), instrument_(instrument), quantity_(quantity) {
        if (!std::isfinite(quantity_) || quantity_ == 0.0) {
            throw std::invalid_argument(
                "MarketOrder quantity must be finite and non-zero"
            );
        }
    }

    OrderId id() const noexcept { return order_id_; }

    InstrumentIndex instrument() const noexcept { return instrument_; }

    // Signed: positive is a buy, negative is a sell.
    double quantity() const noexcept { return quantity_; }

    double absQuantity() const noexcept {
        return std::abs(quantity_);
    }

    OrderSide side() const noexcept {
        return quantity_ > 0.0 ? OrderSide::BUY : OrderSide::SELL;
    }

private:
    OrderId order_id_;
    InstrumentIndex instrument_;
    double quantity_;
};

using Order = std::variant<MarketOrder>;