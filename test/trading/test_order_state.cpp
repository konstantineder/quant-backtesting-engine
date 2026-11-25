#include "execution/fill.hpp"
#include "order/order.hpp"
#include "order/order_status.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <stdexcept>

namespace {

const Date kDate{std::chrono::sys_days{std::chrono::year{2024} / 6 / 3}};

constexpr OrderId kOrderId{1};
constexpr InstrumentIndex kSlot{0};

// The bar an order was submitted on. Irrelevant to every assertion here
// except the one that reads it back, so one value serves them all.
constexpr std::size_t kBarIndex = 7;

Order makeOrder(double quantity, InstrumentIndex slot = kSlot) {
    return MarketOrder{kOrderId, slot, quantity};
}

Fill makeFill(
    double quantity,
    OrderId order_id = kOrderId,
    InstrumentIndex slot = kSlot
) {
    return Fill{order_id, slot, quantity, 100.0, 0.0, kDate, 0};
}

} // namespace

TEST(OrderStateTest, StartsPendingWithFullQuantityOutstanding) {
    const OrderState state{makeOrder(100.0), kBarIndex};

    EXPECT_EQ(state.status(), OrderStatus::PENDING);
    EXPECT_EQ(state.orderId(), kOrderId);
    EXPECT_EQ(state.instrument(), kSlot);
    EXPECT_DOUBLE_EQ(state.remainingQuantity(), 100.0);
    EXPECT_FALSE(state.isTerminal());
    EXPECT_EQ(state.submittedBarIndex(), kBarIndex);
}

// The submission bar is recorded once and never moves: an execution model
// that decides a resting order has aged out reads it on every later bar.
TEST(OrderStateTest, SubmittedBarIndexSurvivesFillsAndCancellation) {
    OrderState state{makeOrder(100.0), kBarIndex};

    state.applyFill(makeFill(40.0));
    EXPECT_EQ(state.submittedBarIndex(), kBarIndex);

    state.cancel();
    EXPECT_EQ(state.submittedBarIndex(), kBarIndex);
}

TEST(OrderStateTest, MultiplePartialFills) {
    OrderState state{makeOrder(100.0), kBarIndex};

    state.applyFill(makeFill(30.0));
    EXPECT_EQ(state.status(), OrderStatus::PARTIALLY_FILLED);
    EXPECT_DOUBLE_EQ(state.remainingQuantity(), 70.0);
    EXPECT_FALSE(state.isTerminal());

    state.applyFill(makeFill(45.0));
    EXPECT_EQ(state.status(), OrderStatus::PARTIALLY_FILLED);
    EXPECT_DOUBLE_EQ(state.remainingQuantity(), 25.0);

    state.applyFill(makeFill(25.0));
    EXPECT_EQ(state.status(), OrderStatus::FILLED);
    EXPECT_DOUBLE_EQ(state.remainingQuantity(), 0.0);
    EXPECT_TRUE(state.isTerminal());
}

TEST(OrderStateTest, MultiplePartialFillsOnAShortOrder) {
    OrderState state{makeOrder(-100.0), kBarIndex};

    state.applyFill(makeFill(-40.0));
    EXPECT_EQ(state.status(), OrderStatus::PARTIALLY_FILLED);
    EXPECT_DOUBLE_EQ(state.remainingQuantity(), -60.0);

    state.applyFill(makeFill(-60.0));
    EXPECT_EQ(state.status(), OrderStatus::FILLED);
    EXPECT_DOUBLE_EQ(state.remainingQuantity(), 0.0);
}

// 0.3 - 0.1 - 0.2 is not 0.0 in IEEE-754 double arithmetic. The order must
// still reach FILLED, and remainingQuantity() must report a clean zero rather
// than the residue.
TEST(OrderStateTest, FloatingPointResidual) {
    ASSERT_NE(0.3 - 0.1 - 0.2, 0.0) << "premise of this test no longer holds";

    OrderState state{makeOrder(0.3), kBarIndex};

    state.applyFill(makeFill(0.1));
    EXPECT_EQ(state.status(), OrderStatus::PARTIALLY_FILLED);

    state.applyFill(makeFill(0.2));
    EXPECT_EQ(state.status(), OrderStatus::FILLED);
    EXPECT_DOUBLE_EQ(state.remainingQuantity(), 0.0);
    EXPECT_TRUE(state.isTerminal());
}

// The same residue, but on an order large enough that a relative business
// tolerance would wrongly swallow a genuinely outstanding quantity.
TEST(OrderStateTest, FloatingPointResidualOnALargeOrder) {
    OrderState state{makeOrder(1'000'000.0), kBarIndex};

    state.applyFill(makeFill(999'999.9));
    EXPECT_EQ(state.status(), OrderStatus::PARTIALLY_FILLED);
    // 0.1 is still outstanding and must NOT be mistaken for rounding noise.
    EXPECT_NEAR(state.remainingQuantity(), 0.1, 1e-9);
    EXPECT_FALSE(state.isTerminal());

    state.applyFill(makeFill(0.1));
    EXPECT_EQ(state.status(), OrderStatus::FILLED);
    EXPECT_DOUBLE_EQ(state.remainingQuantity(), 0.0);
}

TEST(OrderStateTest, OverfillRejected) {
    OrderState state{makeOrder(100.0), kBarIndex};
    state.applyFill(makeFill(60.0));

    const double remaining_before = state.remainingQuantity();
    const OrderStatus status_before = state.status();

    EXPECT_THROW(state.applyFill(makeFill(40.1)), std::runtime_error);

    // applyFill must leave the state untouched when it rejects a fill.
    EXPECT_DOUBLE_EQ(state.remainingQuantity(), remaining_before);
    EXPECT_EQ(state.status(), status_before);

    // ... and the order must still be fillable afterwards.
    state.applyFill(makeFill(40.0));
    EXPECT_EQ(state.status(), OrderStatus::FILLED);
}

TEST(OrderStateTest, RejectsFillOfWrongOrderInstrumentOrDirection) {
    OrderState state{makeOrder(100.0), kBarIndex};

    EXPECT_THROW(
        state.applyFill(makeFill(10.0, OrderId{99})),
        std::runtime_error
    );
    EXPECT_THROW(
        state.applyFill(makeFill(10.0, kOrderId, InstrumentIndex{7})),
        std::runtime_error
    );
    EXPECT_THROW(state.applyFill(makeFill(-10.0)), std::runtime_error);

    EXPECT_EQ(state.status(), OrderStatus::PENDING);
    EXPECT_DOUBLE_EQ(state.remainingQuantity(), 100.0);
}

TEST(OrderStateTest, CancelPartiallyFilledOrder) {
    OrderState state{makeOrder(100.0), kBarIndex};
    state.applyFill(makeFill(30.0));

    state.cancel();

    EXPECT_EQ(state.status(), OrderStatus::CANCELLED);
    EXPECT_TRUE(state.isTerminal());
    // Cancelling does not retroactively undo what was already executed.
    EXPECT_DOUBLE_EQ(state.remainingQuantity(), 70.0);

    // A terminal order accepts neither further fills nor another cancel.
    EXPECT_THROW(state.applyFill(makeFill(10.0)), std::runtime_error);
    EXPECT_THROW(state.cancel(), std::runtime_error);
}

TEST(OrderStateTest, FilledOrderIsTerminal) {
    OrderState state{makeOrder(100.0), kBarIndex};
    state.applyFill(makeFill(100.0));

    ASSERT_EQ(state.status(), OrderStatus::FILLED);
    EXPECT_THROW(state.applyFill(makeFill(1.0)), std::runtime_error);
    EXPECT_THROW(state.cancel(), std::runtime_error);
}

// OrderState owns its Order by value, so it stays valid after the source
// order is gone and can be moved around freely.
TEST(OrderStateTest, OwnsItsOrderByValue) {
    auto makeState = [] {
        const Order order = makeOrder(50.0);
        return OrderState{order, kBarIndex};
    };

    OrderState state = makeState();
    EXPECT_EQ(state.orderId(), kOrderId);
    EXPECT_DOUBLE_EQ(state.remainingQuantity(), 50.0);

    const OrderState copy = state;
    state.applyFill(makeFill(50.0));

    EXPECT_EQ(state.status(), OrderStatus::FILLED);
    EXPECT_EQ(copy.status(), OrderStatus::PENDING);
    EXPECT_DOUBLE_EQ(copy.remainingQuantity(), 50.0);
}
