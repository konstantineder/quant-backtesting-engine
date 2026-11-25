#pragma once

#include "order/order.hpp"
#include "order/order_id.hpp"

#include <variant>

// What the OrderGenerator asks the engine to do with the order book. The
// generator plans only: it never touches the Account or the
// PendingOrderBook itself, so a plan can be inspected, logged or discarded
// before anything is applied.

// Submit a newly built order. Its OrderId is already drawn from the
// generator's sequence.
struct SubmitOrder {
    Order order;
};

// Withdraw the unfilled remainder of an order that is still working.
// Whatever it has already executed stays in the Account.
struct CancelOrder {
    OrderId id;
};

using OrderInstruction = std::variant<
    CancelOrder,
    SubmitOrder
>;
