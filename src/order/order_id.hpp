#pragma once

#include <cstddef>
#include <functional>
#include <stdexcept>
#include <compare>
#include <cstdint>

// Strongly typed order identifier. Wraps the monotonic 64-bit counter
// handed out when an order is created, so that an order id cannot be
// confused with an instrument slot, a trade id or any other integer
// flying around the trading domain. Zero is not a valid id.
class OrderId {
public:
    constexpr explicit OrderId(std::uint64_t value)
        : value_(value) {
            if(value == 0) {
                throw std::invalid_argument("Order ID needs to be strictly positive.");
            }
        }

    constexpr std::uint64_t value() const noexcept { return value_; }

    friend constexpr bool operator==(
        OrderId,
        OrderId
    ) noexcept = default;

    friend constexpr auto operator<=>(
        OrderId,
        OrderId
    ) noexcept = default;

private:
    std::uint64_t value_;
};


// Hands out a monotonic OrderId sequence.
//
// Deliberately an object rather than a static counter on OrderId: a global
// would be mutable state shared between parallel backtest workers, which
// makes runs irreproducible, and it could not be rolled back when the
// operation that drew the ids fails.
class OrderIdSequence {
public:
    OrderIdSequence() = default;

    explicit OrderIdSequence(std::uint64_t first): next_(first) {
        if (first == 0) {
            throw std::invalid_argument(
                "Order ID sequence must start at a strictly positive value."
            );
        }
    }

    // Copying a sequence is how an operation drafts ids it may still roll
    // back, and committing the draft must not be able to fail -- callers
    // rely on the assignment being noexcept to make the commit their last
    // step. Spelt out rather than left implicit so it stays that way.
    OrderIdSequence(const OrderIdSequence&) noexcept = default;
    OrderIdSequence& operator=(const OrderIdSequence&) noexcept = default;
    OrderIdSequence(OrderIdSequence&&) noexcept = default;
    OrderIdSequence& operator=(OrderIdSequence&&) noexcept = default;
    ~OrderIdSequence() = default;

    // Hands out the next id, including the very last one. Unsigned
    // increment wraps to zero exactly when the maximum has just been
    // handed out, and zero is never a valid OrderId -- so the wrap is
    // itself the exhausted marker and needs no extra flag.
    OrderId next() {
        if (exhausted()) {
            throw std::overflow_error("Order ID sequence exhausted.");
        }
        return OrderId{next_++};
    }

    // The id `next()` would hand out, without consuming it. Only valid
    // while the sequence is not exhausted.
    std::uint64_t peek() const noexcept { return next_; }

    bool exhausted() const noexcept { return next_ == 0; }

private:
    std::uint64_t next_ = 1;
};


template <>
struct std::hash<OrderId> {
    std::size_t operator()(OrderId order_id) const noexcept {
        return std::hash<std::uint64_t>{}(order_id.value());
    }
};
