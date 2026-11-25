#include "execution/execution_log.hpp"
#include "execution/fill.hpp"
#include "order/order.hpp"
#include "position/position_state.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>

namespace {

constexpr double kThreshold = 1e-9;
constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

const Date kDate{std::chrono::sys_days{std::chrono::year{2024} / 6 / 3}};

Fill makeFill(double quantity, double price, double fee = 0.0) {
    return Fill{
        OrderId{1}, InstrumentIndex{0}, quantity, price, fee, kDate, 0
    };
}

} // namespace

// ---------------------------------------------------------------- Order --

TEST(OrderTest, CarriesSignedIntentWithoutPrice) {
    const MarketOrder buy{OrderId{11}, InstrumentIndex{2}, 10.0};
    EXPECT_EQ(buy.id(), OrderId{11});
    EXPECT_EQ(buy.instrument(), InstrumentIndex{2});
    EXPECT_DOUBLE_EQ(buy.quantity(), 10.0);
    EXPECT_DOUBLE_EQ(buy.absQuantity(), 10.0);
    EXPECT_EQ(buy.side(), OrderSide::BUY);

    const MarketOrder sell{OrderId{12}, InstrumentIndex{2}, -4.0};
    EXPECT_DOUBLE_EQ(sell.quantity(), -4.0);
    EXPECT_DOUBLE_EQ(sell.absQuantity(), 4.0);
    EXPECT_EQ(sell.side(), OrderSide::SELL);
}

TEST(OrderTest, RejectsInvalidQuantities) {
    for (double quantity : {0.0, kInf, -kInf, kNaN}) {
        EXPECT_THROW(
            (MarketOrder{OrderId{1}, InstrumentIndex{0}, quantity}),
            std::invalid_argument
        );
    }
}

TEST(OrderIdTest, RejectsZero) {
    EXPECT_THROW(OrderId{0}, std::invalid_argument);
    EXPECT_EQ(OrderId{42}.value(), 42u);
    EXPECT_EQ(OrderId{42}, OrderId{42});
    EXPECT_NE(OrderId{42}, OrderId{43});
}

// ----------------------------------------------------------------- Fill --

TEST(FillTest, RecordsExecutionDetail) {
    const Fill fill{
        OrderId{9}, InstrumentIndex{1}, -5.0, 101.5, 2.5, kDate, 7
    };

    EXPECT_EQ(fill.orderId(), OrderId{9});
    EXPECT_EQ(fill.instrument(), InstrumentIndex{1});
    EXPECT_DOUBLE_EQ(fill.quantity(), -5.0);
    EXPECT_DOUBLE_EQ(fill.absQuantity(), 5.0);
    EXPECT_EQ(fill.side(), OrderSide::SELL);
    EXPECT_DOUBLE_EQ(fill.price(), 101.5);
    EXPECT_DOUBLE_EQ(fill.fee(), 2.5);
    EXPECT_EQ(fill.date(), kDate);
    EXPECT_EQ(fill.barIndex(), 7u);
}

TEST(FillTest, RejectsInvalidExecutionDetail) {
    EXPECT_THROW(makeFill(0.0, 100.0), std::invalid_argument);
    EXPECT_THROW(makeFill(kNaN, 100.0), std::invalid_argument);
    EXPECT_THROW(makeFill(1.0, 0.0), std::invalid_argument);
    EXPECT_THROW(makeFill(1.0, -100.0), std::invalid_argument);
    EXPECT_THROW(makeFill(1.0, kInf), std::invalid_argument);
    EXPECT_THROW(makeFill(1.0, 100.0, -0.01), std::invalid_argument);
    EXPECT_THROW(makeFill(1.0, 100.0, kNaN), std::invalid_argument);
}

TEST(FillTest, PartialExecutionKeepsOrderDirection) {
    const Order order = MarketOrder{OrderId{5}, InstrumentIndex{3}, 10.0};

    const Fill partial =
        Fill::fromOrder(order, 4.0, 99.0, 1.0, kDate, 2);

    // fromOrder must carry the order's identity into the fill, so that an
    // OrderState can match the two up again.
    EXPECT_EQ(partial.orderId(), OrderId{5});
    EXPECT_EQ(partial.instrument(), InstrumentIndex{3});
    EXPECT_DOUBLE_EQ(partial.quantity(), 4.0);
    EXPECT_EQ(partial.side(), OrderSide::BUY);
}

TEST(FillTest, RejectsExecutionThatContradictsTheOrder) {
    const Order buy = MarketOrder{OrderId{1}, InstrumentIndex{0}, 10.0};

    // Opposite direction.
    EXPECT_THROW(
        Fill::fromOrder(buy, -4.0, 99.0, 0.0, kDate, 0),
        std::invalid_argument
    );
    // More than ordered.
    EXPECT_THROW(
        Fill::fromOrder(buy, 10.5, 99.0, 0.0, kDate, 0),
        std::invalid_argument
    );
    EXPECT_NO_THROW(Fill::fromOrder(buy, 10.0, 99.0, 0.0, kDate, 0));

    const Order sell = MarketOrder{OrderId{2}, InstrumentIndex{0}, -10.0};
    EXPECT_THROW(
        Fill::fromOrder(sell, 4.0, 99.0, 0.0, kDate, 0),
        std::invalid_argument
    );
    EXPECT_THROW(
        Fill::fromOrder(sell, -11.0, 99.0, 0.0, kDate, 0),
        std::invalid_argument
    );
    EXPECT_NO_THROW(Fill::fromOrder(sell, -3.0, 99.0, 0.0, kDate, 0));
}

// -------------------------------------------------------- PositionState --

TEST(PositionStateTest, DefaultIsFlat) {
    const PositionState position;

    EXPECT_TRUE(position.isFlat());
    EXPECT_FALSE(position.isLong());
    EXPECT_FALSE(position.isShort());
    EXPECT_DOUBLE_EQ(position.quantity(), 0.0);
    EXPECT_DOUBLE_EQ(position.averagePrice(), 0.0);
    EXPECT_DOUBLE_EQ(position.realizedPnL(), 0.0);
}

TEST(PositionStateTest, OpensLong) {
    PositionState position;
    const double realized = position.applyFill(10.0, 100.0, 1.0);

    EXPECT_DOUBLE_EQ(realized, 0.0);
    EXPECT_TRUE(position.isLong());
    EXPECT_DOUBLE_EQ(position.quantity(), 10.0);
    EXPECT_DOUBLE_EQ(position.averagePrice(), 100.0);
}

TEST(PositionStateTest, OpensShort) {
    PositionState position;
    const double realized = position.applyFill(-10.0, 100.0, 1.0);

    EXPECT_DOUBLE_EQ(realized, 0.0);
    EXPECT_TRUE(position.isShort());
    EXPECT_DOUBLE_EQ(position.quantity(), -10.0);
    EXPECT_DOUBLE_EQ(position.averagePrice(), 100.0);
}

TEST(PositionStateTest, IncreasesLongWithWeightedAveragePrice) {
    PositionState position;
    position.applyFill(10.0, 100.0, 1.0);
    const double realized = position.applyFill(30.0, 120.0, 1.0);

    EXPECT_DOUBLE_EQ(realized, 0.0);
    EXPECT_DOUBLE_EQ(position.quantity(), 40.0);
    // (10*100 + 30*120) / 40
    EXPECT_NEAR(position.averagePrice(), 115.0, kThreshold);
}

TEST(PositionStateTest, IncreasesShortWithWeightedAveragePrice) {
    PositionState position;
    position.applyFill(-10.0, 100.0, 1.0);
    position.applyFill(-10.0, 90.0, 1.0);

    EXPECT_DOUBLE_EQ(position.quantity(), -20.0);
    EXPECT_NEAR(position.averagePrice(), 95.0, kThreshold);
    EXPECT_DOUBLE_EQ(position.realizedPnL(), 0.0);
}

TEST(PositionStateTest, PartialCloseRealizesAndKeepsAveragePrice) {
    PositionState position;
    position.applyFill(10.0, 100.0, 1.0);

    const double realized = position.applyFill(-4.0, 110.0, 1.0);

    EXPECT_NEAR(realized, 40.0, kThreshold);
    EXPECT_NEAR(position.realizedPnL(), 40.0, kThreshold);
    EXPECT_DOUBLE_EQ(position.quantity(), 6.0);
    EXPECT_DOUBLE_EQ(position.averagePrice(), 100.0);
}

TEST(PositionStateTest, PartialCloseOfShortRealizesInverted) {
    PositionState position;
    position.applyFill(-10.0, 100.0, 1.0);

    // Buying back cheaper is a profit for a short.
    const double realized = position.applyFill(4.0, 90.0, 1.0);

    EXPECT_NEAR(realized, 40.0, kThreshold);
    EXPECT_DOUBLE_EQ(position.quantity(), -6.0);
    EXPECT_DOUBLE_EQ(position.averagePrice(), 100.0);
}

TEST(PositionStateTest, FullCloseResetsAveragePrice) {
    PositionState position;
    position.applyFill(10.0, 100.0, 1.0);

    const double realized = position.applyFill(-10.0, 130.0, 1.0);

    EXPECT_NEAR(realized, 300.0, kThreshold);
    EXPECT_TRUE(position.isFlat());
    EXPECT_DOUBLE_EQ(position.averagePrice(), 0.0);
    EXPECT_NEAR(position.realizedPnL(), 300.0, kThreshold);
}

TEST(PositionStateTest, ReversalRealizesClosingLegAndReopensAtFillPrice) {
    PositionState position;
    position.applyFill(10.0, 100.0, 1.0);

    // Sell 15: closes 10 long, opens 5 short at 110.
    const double realized = position.applyFill(-15.0, 110.0, 1.0);

    EXPECT_NEAR(realized, 100.0, kThreshold);
    EXPECT_TRUE(position.isShort());
    EXPECT_DOUBLE_EQ(position.quantity(), -5.0);
    EXPECT_DOUBLE_EQ(position.averagePrice(), 110.0);
}

TEST(PositionStateTest, ReversalFromShortToLong) {
    PositionState position;
    position.applyFill(-10.0, 100.0, 1.0);

    // Buy 25: closes 10 short at a 20 loss, opens 15 long at 120.
    const double realized = position.applyFill(25.0, 120.0, 1.0);

    EXPECT_NEAR(realized, -200.0, kThreshold);
    EXPECT_TRUE(position.isLong());
    EXPECT_DOUBLE_EQ(position.quantity(), 15.0);
    EXPECT_DOUBLE_EQ(position.averagePrice(), 120.0);
}

TEST(PositionStateTest, AppliesContractMultiplierToRealizedPnLOnly) {
    PositionState position;
    position.applyFill(2.0, 10.0, 25.0);

    // Average price stays a unit price; only PnL is scaled.
    EXPECT_DOUBLE_EQ(position.averagePrice(), 10.0);

    const double realized = position.applyFill(-2.0, 12.0, 25.0);
    EXPECT_NEAR(realized, 2.0 * 2.0 * 25.0, kThreshold);
}

TEST(PositionStateTest, RejectsInvalidFillArguments) {
    PositionState position;
    position.applyFill(10.0, 100.0, 1.0);

    EXPECT_THROW(position.applyFill(0.0, 100.0, 1.0), std::invalid_argument);
    EXPECT_THROW(position.applyFill(kNaN, 100.0, 1.0), std::invalid_argument);
    EXPECT_THROW(position.applyFill(1.0, 0.0, 1.0), std::invalid_argument);
    EXPECT_THROW(position.applyFill(1.0, -1.0, 1.0), std::invalid_argument);
    EXPECT_THROW(position.applyFill(1.0, 100.0, 0.0), std::invalid_argument);
    EXPECT_THROW(position.applyFill(1.0, 100.0, -5.0), std::invalid_argument);
    EXPECT_THROW(position.applyFill(1.0, 100.0, kInf), std::invalid_argument);
}

TEST(PositionStateTest, LeavesStateUntouchedWhenAFillIsRejected) {
    PositionState position;
    position.applyFill(10.0, 100.0, 1.0);
    position.applyFill(-4.0, 110.0, 1.0);

    const double quantity = position.quantity();
    const double average_price = position.averagePrice();
    const double realized = position.realizedPnL();

    EXPECT_THROW(
        position.applyFill(5.0, -1.0, 1.0), std::invalid_argument
    );

    EXPECT_DOUBLE_EQ(position.quantity(), quantity);
    EXPECT_DOUBLE_EQ(position.averagePrice(), average_price);
    EXPECT_DOUBLE_EQ(position.realizedPnL(), realized);
}

TEST(PositionStateTest, RejectsInconsistentConstructedState) {
    // Flat but priced.
    EXPECT_THROW(PositionState(0.0, 100.0, 0.0), std::invalid_argument);
    // Open but unpriced.
    EXPECT_THROW(PositionState(5.0, 0.0, 0.0), std::invalid_argument);
    EXPECT_THROW(PositionState(5.0, -1.0, 0.0), std::invalid_argument);
    EXPECT_THROW(PositionState(kNaN, 1.0, 0.0), std::invalid_argument);
    EXPECT_THROW(PositionState(5.0, 1.0, kInf), std::invalid_argument);

    EXPECT_NO_THROW(PositionState(0.0, 0.0, 42.0));
    EXPECT_NO_THROW(PositionState(-5.0, 100.0, 42.0));
}

TEST(PositionStateTest, DetectsNonRepresentableRealizedPnL) {
    const double huge = std::numeric_limits<double>::max();
    PositionState position{-1.0, 1.0, -huge};

    EXPECT_THROW(
        position.applyFill(1.0, huge, huge), std::overflow_error
    );
    // The rejected fill left the state intact.
    EXPECT_DOUBLE_EQ(position.quantity(), -1.0);
    EXPECT_DOUBLE_EQ(position.realizedPnL(), -huge);
}

// ---------------------------------------------------------- ExecutionLog --

TEST(ExecutionLogTest, RecordsFillsInOrder) {
    ExecutionLog log;
    EXPECT_TRUE(log.empty());

    log.record(makeFill(10.0, 100.0, 1.0));
    log.record(makeFill(-10.0, 110.0, 1.0));

    ASSERT_EQ(log.size(), 2u);
    EXPECT_FALSE(log.empty());
    EXPECT_DOUBLE_EQ(log.at(0).quantity(), 10.0);
    EXPECT_DOUBLE_EQ(log.at(1).price(), 110.0);
    EXPECT_EQ(log.fills().size(), 2u);
    EXPECT_DOUBLE_EQ(log.fills().front().price(), 100.0);
}

TEST(ExecutionLogTest, RejectsOutOfRangeAccess) {
    ExecutionLog log;
    EXPECT_THROW(log.at(0), std::out_of_range);

    log.record(makeFill(1.0, 100.0));
    EXPECT_NO_THROW(log.at(0));
    EXPECT_THROW(log.at(1), std::out_of_range);
}

TEST(ExecutionLogTest, ReplaysIndependentlyOfAnyAccount) {
    ExecutionLog log;
    log.record(makeFill(10.0, 100.0));
    log.record(makeFill(-15.0, 110.0));

    // Replaying the log reproduces the position state deterministically.
    PositionState first;
    for (const Fill& fill : log.fills()) {
        first.applyFill(fill.quantity(), fill.price(), 1.0);
    }

    PositionState second;
    for (const Fill& fill : log.fills()) {
        second.applyFill(fill.quantity(), fill.price(), 1.0);
    }

    EXPECT_DOUBLE_EQ(first.quantity(), second.quantity());
    EXPECT_DOUBLE_EQ(first.averagePrice(), second.averagePrice());
    EXPECT_DOUBLE_EQ(first.realizedPnL(), second.realizedPnL());
    EXPECT_DOUBLE_EQ(first.quantity(), -5.0);
}

TEST(ExecutionLogTest, ClearResetsTheLog) {
    ExecutionLog log;
    log.reserve(8);
    log.record(makeFill(1.0, 100.0));
    log.clear();

    EXPECT_TRUE(log.empty());
    EXPECT_EQ(log.size(), 0u);
}
