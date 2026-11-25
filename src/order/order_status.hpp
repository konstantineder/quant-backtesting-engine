#pragma once

#include "common/enums.hpp"
#include "order/order.hpp"
#include "execution/fill.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
#include <variant>

class OrderState {
public:
    explicit OrderState(Order order, std::size_t submitted_bar_index)
        : order_(std::move(order)),
          order_status_(OrderStatus::PENDING),
          remaining_quantity_(std::visit(
              [](const auto& ord) { return ord.quantity(); }, order_
          )),
          submitted_bar_index_(submitted_bar_index) {}

    const Order& order() const noexcept { return order_; }

    OrderId orderId() const noexcept {
        return std::visit([](const auto& ord) { return ord.id(); }, order_);
    }

    InstrumentIndex instrument() const noexcept {
        return std::visit([](const auto& ord) { return ord.instrument(); }, order_);
    }

    double remainingQuantity() const noexcept { return remaining_quantity_; }

    OrderStatus status() const noexcept { return order_status_; }

    void applyFill(const Fill& fill) {
        if(isTerminal()) {
            throw std::runtime_error("No further fill request accepted.");
        }
        if(fill.orderId() != orderId()) {
            throw std::runtime_error("Fill references to wrong order.");
        }
        if(fill.instrument() != instrument()) {
            throw std::runtime_error(
                "Instruments associated to requested fill and order do not match."
            );
        }
        if(std::signbit(fill.quantity()) != std::signbit(remaining_quantity_)) {
            throw std::runtime_error(
                "Fill direction must match the order's remaining quantity."
            );
        }
        // Strict: an execution model must never fill more than what is left.
        // The only slack granted is the rounding residue of the subtractions
        // above, which is far smaller than any tradable quantity.
        if(fill.absQuantity() > std::abs(remaining_quantity_) + numericTolerance()) {
            throw std::runtime_error("Requested fill exceeds remaining quantity.");
        }

        remaining_quantity_ -= fill.quantity();

        if(std::abs(remaining_quantity_) <= numericTolerance()) {
            // Snap away the rounding residue so remainingQuantity() reports a
            // clean zero once the order is done.
            remaining_quantity_ = 0.0;
            order_status_ = OrderStatus::FILLED;
        } else {
            order_status_ = OrderStatus::PARTIALLY_FILLED;
        }
    }

    void cancel() { 
        if(isTerminal()) {
            throw std::runtime_error("Order's state is already terminal.");
        }
        order_status_ = OrderStatus::CANCELLED; 
    }

    bool isTerminal() const noexcept {
        return order_status_ == OrderStatus::CANCELLED || order_status_ == OrderStatus::FILLED;
    }

    std::size_t submittedBarIndex() const noexcept {
        return submitted_bar_index_;
    }

private:
    // Purely numeric slack, NOT a tradable-quantity tolerance: it covers the
    // rounding residue that accumulates when fills are subtracted in double
    // arithmetic (0.3 - 0.1 - 0.2 != 0.0), and nothing else. It is expressed
    // in units of the last place of the order size, so it stays several
    // orders of magnitude below any quantity a venue could actually trade --
    // for an order of 1'000'000 it is about 2e-9, not 1e-3. Deciding that a
    // genuinely outstanding sliver is too small to bother with is a business
    // rule about the instrument's quantity precision, and does not belong
    // here.
    static constexpr double kToleranceUlps = 8.0;

    double orderQuantity() const noexcept {
        return std::visit(
            [](const auto& ord) { return ord.absQuantity(); }, order_
        );
    }

    double numericTolerance() const noexcept {
        const double scale = std::max(
            {1.0, orderQuantity(), std::abs(remaining_quantity_)}
        );
        return kToleranceUlps * std::numeric_limits<double>::epsilon() * scale;
    }

    Order order_;
    OrderStatus order_status_;
    double remaining_quantity_;
    std::size_t submitted_bar_index_;
};