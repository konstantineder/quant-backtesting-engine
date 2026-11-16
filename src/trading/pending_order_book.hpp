#pragma once

#include <span>
#include <unordered_map>
#include <vector>
#include <utility>
#include <variant>
#include <stdexcept>
#include <cstddef>
#include "trading/order_id.hpp"
#include "trading/order_status.hpp"

class PendingOrderBook {
public:
    void submit(Order order) {
        const auto& order_id = std::visit(
            [](const auto& ord) {
                return ord.id();
            }, order
        );
        auto it = order_indices_.find(order_id);
        if(it != order_indices_.end()) {
            throw std::runtime_error("Order already registered.");
        }
        auto order_state = OrderState(order);
        
        const auto index = pending_orders_.size();

        pending_orders_.push_back(std::move(order_state));

        try {
            order_indices_.emplace(order_id, index);
        } catch (...) {
            pending_orders_.pop_back();
            throw;
        }
    }

    void applyFill(const Fill& fill) {
        auto& order_state = getOrderState(fill.orderId());
        order_state.applyFill(fill);
    }

    void cancel(OrderId id) {
        auto& order_state = getOrderState(id);
        order_state.cancel();
    }

    const OrderState& get(OrderId id) const {
        const auto order_idx = order_indices_.at(id);
        return pending_orders_[order_idx];
    }

    // Active orders in submission order. Any reference or index obtained
    // from the book is valid only until the next mutation.
    std::span<const OrderState> orders() const noexcept {
        return pending_orders_;
    }

    bool empty() const noexcept { return pending_orders_.empty(); }

    std::size_t size() const noexcept {
        return pending_orders_.size();
    };

    void removeTerminal() {
        auto pending_orders = std::vector<OrderState>{};
        auto order_indices = std::unordered_map<OrderId, std::size_t>{};
        for (const auto& order : pending_orders_) {
            if(!order.isTerminal()) {
                pending_orders.push_back(order);
                order_indices[order.orderId()] = pending_orders.size() - 1;
            }
        }
        pending_orders_.swap(pending_orders);
        order_indices_.swap(order_indices);
    }

private:
    OrderState& getOrderState(OrderId id) {
        const auto order_idx = order_indices_.at(id);
        return pending_orders_[order_idx];
    }

    std::vector<OrderState> pending_orders_ = {};
    std::unordered_map<OrderId, std::size_t> order_indices_ = {};
};