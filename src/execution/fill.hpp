#pragma once

#include "common/enums.hpp"
#include "common/types.hpp"
#include "instrument/instrument_id.hpp"
#include "order/order.hpp"
#include "order/order_id.hpp"

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <variant>

// Actual execution of (part of) an Order: signed quantity, unit price and
// fee, stamped with the bar it happened on.
//
// `price` is a unit price in the same convention as PricingEngine::price --
// per share, or per option contract unit. The contract multiplier is NOT
// folded in here; it belongs to InstrumentTradingSpec and is applied once,
// during settlement and valuation.
class Fill {
public:
    Fill(
        OrderId order_id,
        InstrumentIndex instrument,
        double quantity,
        double price,
        double fee,
        Date date,
        std::size_t bar_index
    ): order_id_(order_id), 
       instrument_(instrument),
       quantity_(quantity),
       price_(price),
       fee_(fee),
       date_(date),
       bar_index_(bar_index) {
        if (!std::isfinite(quantity_) || quantity_ == 0.0) {
            throw std::invalid_argument(
                "Fill quantity must be finite and non-zero"
            );
        }
        if (!std::isfinite(price_) || price_ <= 0.0) {
            throw std::invalid_argument(
                "Fill price must be finite and positive"
            );
        }
        if (!std::isfinite(fee_) || fee_ < 0.0) {
            throw std::invalid_argument(
                "Fill fee must be finite and non-negative"
            );
        }
    }

    // Executes an order fully or partially. A partial fill keeps the
    // order's direction and may not exceed its size.
    static Fill fromOrder(
        const Order& order,
        double executed_quantity,
        double price,
        double fee,
        Date date,
        std::size_t bar_index
    ) {
        if (!std::isfinite(executed_quantity) ||
            executed_quantity == 0.0) {
            throw std::invalid_argument(
                "Executed quantity must be finite and non-zero"
            );
        }

        const auto& [order_id, instrument, order_quantity] = std::visit(
            [](const auto& ord) {
                return std::tuple{ord.id(), ord.instrument(), ord.quantity()};
            }, order
        );

        const bool same_direction =
            (executed_quantity > 0.0) == (order_quantity > 0.0);

        if (!same_direction) {
            throw std::invalid_argument(
                "Fill direction must match the order"
            );
        }

        if (std::abs(executed_quantity) > std::abs(order_quantity)) {
            throw std::invalid_argument(
                "Fill must not exceed the ordered quantity"
            );
        }

        return Fill{
            order_id,
            instrument,
            executed_quantity,
            price,
            fee,
            date,
            bar_index
        };
    }

    OrderId orderId() const noexcept { return order_id_; }

    InstrumentIndex instrument() const noexcept { return instrument_; }

    // Signed: positive was bought, negative was sold.
    double quantity() const noexcept { return quantity_; }

    double absQuantity() const noexcept { return std::abs(quantity_); }

    OrderSide side() const noexcept {
        return quantity_ > 0.0 ? OrderSide::BUY : OrderSide::SELL;
    }

    double price() const noexcept { return price_; }
    double fee() const noexcept { return fee_; }
    Date date() const noexcept { return date_; }
    std::size_t barIndex() const noexcept { return bar_index_; }

private:
    OrderId order_id_;
    InstrumentIndex instrument_;
    double quantity_;
    double price_;
    double fee_;
    Date date_;
    std::size_t bar_index_;
};
