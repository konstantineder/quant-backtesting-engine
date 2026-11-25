#pragma once

#include <span>
#include <unordered_map>
#include <vector>
#include <utility>
#include <variant>
#include <stdexcept>
#include <cstddef>
#include "order/order_id.hpp"
#include "order/order_status.hpp"
#include "instrument/instrument_universe.hpp"

class PendingOrderBook {
public:
    explicit PendingOrderBook(const InstrumentUniverse& universe) {
        remaining_quantities_ = std::vector<double>(universe.size(), 0.0);
    }

    void submit(Order order, std::size_t submitted_bar_index) {
        const auto order_id = std::visit(
            [](const auto& ord) {
                return ord.id();
            },
            order
        );

        if (order_indices_.contains(order_id)) {
            throw std::runtime_error("Order already registered.");
        }

        OrderState order_state{std::move(order), submitted_bar_index};

        const auto instrument_idx = checkedSlot(order_state.instrument());

        const double current_pending = remaining_quantities_[instrument_idx];
        const double new_pending = order_state.remainingQuantity();
        if (current_pending != 0.0 &&
            std::signbit(current_pending) != std::signbit(new_pending)) {
                throw std::logic_error("Conflicting pending orders require cancellation");
            }
        const double updated = current_pending + new_pending;

        if (!std::isfinite(updated)) {
            throw std::overflow_error("Invalid aggregate pending quantity");
        }

        const auto index = pending_orders_.size();

        pending_orders_.push_back(std::move(order_state));

        try {
            order_indices_.emplace(order_id, index);
        } catch (...) {
            pending_orders_.pop_back();
            throw;
        }

        // Commit only after successful registration.
        remaining_quantities_[instrument_idx] = updated;
    }

    void applyFill(const Fill& fill) {
        auto& order_state = getOrderState(fill.orderId());

        const auto instrument_idx =
            order_state.instrument().value();

        const double before = order_state.remainingQuantity();

        order_state.applyFill(fill);

        const double after = order_state.remainingQuantity();

        remaining_quantities_[instrument_idx] += after - before;
    }

    void cancel(OrderId id) {
        auto& order_state = getOrderState(id);

        const auto instrument_idx =
            order_state.instrument().value();

        const double remaining = order_state.remainingQuantity();

        order_state.cancel();

        remaining_quantities_[instrument_idx] -= remaining;
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

    double pendingQuantity(InstrumentIndex instrument) const {
        return remaining_quantities_[checkedSlot(instrument)];
    }

private:
    // Every slot reaching the aggregate goes through here: an index that is
    // not in the universe is a caller error, not an out-of-bounds read.
    std::size_t checkedSlot(InstrumentIndex instrument) const {
        const auto index = instrument.value();
        if (index >= remaining_quantities_.size()) {
            throw std::out_of_range("Invalid instrument index");
        }
        return index;
    }

    OrderState& getOrderState(OrderId id) {
        const auto order_idx = order_indices_.at(id);
        return pending_orders_[order_idx];
    }

    std::vector<OrderState> pending_orders_ = {};
    std::vector<double> remaining_quantities_;
    std::unordered_map<OrderId, std::size_t> order_indices_ = {};
};
