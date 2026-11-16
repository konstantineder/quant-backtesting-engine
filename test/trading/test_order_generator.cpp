#include "backtesting/account/account.hpp"
#include "backtesting/strategy/strategy.hpp"
#include "trading/order.hpp"
#include "trading/order_generator.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>
#include <variant>
#include <vector>

namespace {

constexpr double kInitialCash = 10'000.0;
constexpr auto kEquitySlot = InstrumentIndex{0};
constexpr auto kOptionSlot = InstrumentIndex{1};

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

const Date kDate{std::chrono::sys_days{std::chrono::year{2024} / 6 / 3}};

// Slot 0: equity, multiplier 1. Slot 1: option, multiplier 25.
Account makeAccount(double cash = kInitialCash) {
    return Account{
        cash,
        {
            InstrumentTradingSpec{1.0, SettlementConvention::PREMIUM},
            InstrumentTradingSpec{25.0, SettlementConvention::PREMIUM}
        }
    };
}

void openPosition(
    Account& account,
    InstrumentIndex slot,
    double quantity,
    double price
) {
    account.applyFill(
        Fill{OrderId{999}, slot, quantity, price, 0.0, kDate, 0}
    );
}

const MarketOrder& marketOrder(const Order& order) {
    return std::get<MarketOrder>(order);
}

} // namespace

// ------------------------------------------------------- quantity targets --

TEST(OrderGeneratorTest, GeneratesLongOrderFromFlatAccount) {
    OrderGenerator generator;
    const Account account = makeAccount();
    const std::vector<double> prices{100.0, 200.0};

    const std::vector<Order> orders = generator.generate(
        TargetQuantities{30.0, 0.0}, account, prices
    );

    ASSERT_EQ(orders.size(), 1u);
    EXPECT_EQ(marketOrder(orders[0]).instrument(), kEquitySlot);
    EXPECT_DOUBLE_EQ(marketOrder(orders[0]).quantity(), 30.0);
    EXPECT_EQ(marketOrder(orders[0]).side(), OrderSide::BUY);
}

TEST(OrderGeneratorTest, GeneratesShortOrderFromFlatAccount) {
    OrderGenerator generator;
    const Account account = makeAccount();
    const std::vector<double> prices{100.0, 200.0};

    const std::vector<Order> orders = generator.generate(
        TargetQuantities{-30.0, 0.0}, account, prices
    );

    ASSERT_EQ(orders.size(), 1u);
    EXPECT_DOUBLE_EQ(marketOrder(orders[0]).quantity(), -30.0);
    EXPECT_EQ(marketOrder(orders[0]).side(), OrderSide::SELL);
}

TEST(OrderGeneratorTest, DiffsAgainstTheCurrentPosition) {
    OrderGenerator generator;
    Account account = makeAccount();
    openPosition(account, kEquitySlot, 20.0, 100.0);
    const std::vector<double> prices{100.0, 200.0};

    // Target 30 against a position of 20 is an order for 10, not 30.
    const std::vector<Order> orders = generator.generate(
        TargetQuantities{30.0, 0.0}, account, prices
    );

    ASSERT_EQ(orders.size(), 1u);
    EXPECT_DOUBLE_EQ(marketOrder(orders[0]).quantity(), 10.0);
}

TEST(OrderGeneratorTest, ReversesAPositionInOneOrder) {
    OrderGenerator generator;
    Account account = makeAccount();
    openPosition(account, kEquitySlot, 20.0, 100.0);
    const std::vector<double> prices{100.0, 200.0};

    const std::vector<Order> orders = generator.generate(
        TargetQuantities{-15.0, 0.0}, account, prices
    );

    ASSERT_EQ(orders.size(), 1u);
    EXPECT_DOUBLE_EQ(marketOrder(orders[0]).quantity(), -35.0);
}

TEST(OrderGeneratorTest, SlotOnTargetProducesNoOrder) {
    OrderGenerator generator;
    Account account = makeAccount();
    openPosition(account, kEquitySlot, 30.0, 100.0);
    const std::vector<double> prices{100.0, 200.0};

    // A zero delta must be skipped rather than turned into a MarketOrder,
    // whose constructor rejects a zero quantity.
    const std::vector<Order> orders = generator.generate(
        TargetQuantities{30.0, 0.0}, account, prices
    );

    EXPECT_TRUE(orders.empty());
}

TEST(OrderGeneratorTest, FlatAccountWithAnAllZeroTargetProducesNoOrders) {
    OrderGenerator generator;
    const Account account = makeAccount();
    const std::vector<double> prices{100.0, 200.0};

    EXPECT_TRUE(
        generator.generate(TargetQuantities{0.0, 0.0}, account, prices).empty()
    );
    EXPECT_TRUE(
        generator.generate(TargetWeights{0.0, 0.0}, account, prices).empty()
    );
}

TEST(OrderGeneratorTest, SuppressesNegligibleDelta) {
    OrderGenerator generator;
    Account account = makeAccount();
    openPosition(account, kEquitySlot, 30.0, 100.0);
    const std::vector<double> prices{100.0, 200.0};

    // Rounding noise, not a position to trade.
    EXPECT_TRUE(
        generator.generate(
            TargetQuantities{30.0 + 1e-15, 0.0}, account, prices
        ).empty()
    );

    // A genuinely small but real delta still trades.
    const std::vector<Order> orders = generator.generate(
        TargetQuantities{30.001, 0.0}, account, prices
    );
    ASSERT_EQ(orders.size(), 1u);
    EXPECT_NEAR(marketOrder(orders[0]).quantity(), 0.001, 1e-12);
}

TEST(OrderGeneratorTest, GeneratesOneOrderPerOffTargetSlot) {
    OrderGenerator generator;
    const Account account = makeAccount();
    const std::vector<double> prices{100.0, 200.0};

    const std::vector<Order> orders = generator.generate(
        TargetQuantities{10.0, -2.0}, account, prices
    );

    ASSERT_EQ(orders.size(), 2u);
    EXPECT_EQ(marketOrder(orders[0]).instrument(), kEquitySlot);
    EXPECT_EQ(marketOrder(orders[1]).instrument(), kOptionSlot);
    EXPECT_DOUBLE_EQ(marketOrder(orders[1]).quantity(), -2.0);
}

// --------------------------------------------------------- weight targets --

TEST(OrderGeneratorTest, WeightTargetSizesOffEquityNotCash) {
    OrderGenerator generator;
    Account account = makeAccount();
    // Half the account is now invested: cash 5'000, position worth 5'000,
    // equity still 10'000.
    openPosition(account, kEquitySlot, 50.0, 100.0);
    ASSERT_DOUBLE_EQ(account.cash(), 5'000.0);

    const std::vector<double> prices{100.0, 200.0};

    // Weight 1.0 means the whole portfolio: 10'000 / 100 = 100 units, so
    // 50 more on top of the 50 already held. Sizing off cash would give a
    // target of 50 and therefore no order at all.
    const std::vector<Order> orders = generator.generate(
        TargetWeights{1.0, 0.0}, account, prices
    );

    ASSERT_EQ(orders.size(), 1u);
    EXPECT_DOUBLE_EQ(marketOrder(orders[0]).quantity(), 50.0);
}

TEST(OrderGeneratorTest, WeightTargetAppliesTheContractMultiplier) {
    OrderGenerator generator;
    const Account account = makeAccount();
    const std::vector<double> prices{100.0, 200.0};

    // Slot 1 has multiplier 25: 0.5 * 10'000 / (200 * 25) = 1 contract.
    const std::vector<Order> orders = generator.generate(
        TargetWeights{0.0, 0.5}, account, prices
    );

    ASSERT_EQ(orders.size(), 1u);
    EXPECT_EQ(marketOrder(orders[0]).instrument(), kOptionSlot);
    EXPECT_DOUBLE_EQ(marketOrder(orders[0]).quantity(), 1.0);
}

TEST(OrderGeneratorTest, NegativeWeightGoesShort) {
    OrderGenerator generator;
    const Account account = makeAccount();
    const std::vector<double> prices{100.0, 200.0};

    const std::vector<Order> orders = generator.generate(
        TargetWeights{-0.25, 0.0}, account, prices
    );

    ASSERT_EQ(orders.size(), 1u);
    EXPECT_DOUBLE_EQ(marketOrder(orders[0]).quantity(), -25.0);
}

// A zero weight is zero units whatever the price is, so a slot that is both
// flat and not targeted must not need a usable quote.
TEST(OrderGeneratorTest, ZeroWeightNeedsNoPrice) {
    OrderGenerator generator;
    const Account account = makeAccount();
    const std::vector<double> prices{100.0, kNaN};

    const std::vector<Order> orders = generator.generate(
        TargetWeights{0.5, 0.0}, account, prices
    );

    ASSERT_EQ(orders.size(), 1u);
    EXPECT_DOUBLE_EQ(marketOrder(orders[0]).quantity(), 50.0);
}

// ... but a slot that is held must be priced, because it moves equity.
TEST(OrderGeneratorTest, HeldSlotStillNeedsAPriceEvenAtWeightZero) {
    OrderGenerator generator;
    Account account = makeAccount();
    openPosition(account, kOptionSlot, 1.0, 200.0);
    const std::vector<double> prices{100.0, kNaN};

    EXPECT_THROW(
        generator.generate(TargetWeights{0.5, 0.0}, account, prices),
        std::invalid_argument
    );
}

TEST(OrderGeneratorTest, RejectsUnusablePriceForNonZeroWeight) {
    OrderGenerator generator;
    const Account account = makeAccount();

    for (const double bad : {0.0, -100.0, kNaN, kInf}) {
        const std::vector<double> prices{bad, 200.0};
        EXPECT_THROW(
            generator.generate(TargetWeights{0.5, 0.0}, account, prices),
            std::invalid_argument
        ) << "price " << bad;
    }
}

// ------------------------------------------------------------- validation --

TEST(OrderGeneratorTest, RejectsTargetSizeMismatch) {
    OrderGenerator generator;
    const Account account = makeAccount();  // two slots
    const std::vector<double> prices{100.0, 200.0};

    EXPECT_THROW(
        generator.generate(TargetQuantities{1.0}, account, prices),
        std::invalid_argument
    );
    EXPECT_THROW(
        generator.generate(TargetQuantities{1.0, 2.0, 3.0}, account, prices),
        std::invalid_argument
    );
    // Oversized weight targets must be caught before the conversion loop
    // walks past the end of the account.
    EXPECT_THROW(
        generator.generate(TargetWeights{0.2, 0.3, 0.5}, account, prices),
        std::invalid_argument
    );
}

TEST(OrderGeneratorTest, RejectsPriceSizeMismatch) {
    OrderGenerator generator;
    const Account account = makeAccount();

    EXPECT_THROW(
        generator.generate(
            TargetQuantities{1.0, 2.0}, account, std::vector<double>{100.0}
        ),
        std::invalid_argument
    );
}

TEST(OrderGeneratorTest, RejectsNonFiniteTargetValues) {
    OrderGenerator generator;
    const Account account = makeAccount();
    const std::vector<double> prices{100.0, 200.0};

    for (const double bad : {kNaN, kInf, -kInf}) {
        EXPECT_THROW(
            generator.generate(TargetQuantities{bad, 0.0}, account, prices),
            std::invalid_argument
        ) << "quantity " << bad;
        EXPECT_THROW(
            generator.generate(TargetWeights{bad, 0.0}, account, prices),
            std::invalid_argument
        ) << "weight " << bad;
    }
}

// ----------------------------------------------------------- order id flow --

TEST(OrderGeneratorTest, OrderIdsAreUniqueAndMonotonic) {
    OrderGenerator generator;
    const Account account = makeAccount();
    const std::vector<double> prices{100.0, 200.0};

    const std::vector<Order> first = generator.generate(
        TargetQuantities{10.0, 2.0}, account, prices
    );
    const std::vector<Order> second = generator.generate(
        TargetQuantities{10.0, 2.0}, account, prices
    );

    ASSERT_EQ(first.size(), 2u);
    ASSERT_EQ(second.size(), 2u);

    EXPECT_EQ(marketOrder(first[0]).id(), OrderId{1});
    EXPECT_EQ(marketOrder(first[1]).id(), OrderId{2});
    // The sequence continues across calls rather than restarting.
    EXPECT_EQ(marketOrder(second[0]).id(), OrderId{3});
    EXPECT_EQ(marketOrder(second[1]).id(), OrderId{4});
}

TEST(OrderGeneratorTest, SeparateGeneratorsKeepSeparateSequences) {
    OrderGenerator first_worker;
    OrderGenerator second_worker;
    const Account account = makeAccount();
    const std::vector<double> prices{100.0, 200.0};

    const auto a = first_worker.generate(
        TargetQuantities{10.0, 0.0}, account, prices
    );
    const auto b = second_worker.generate(
        TargetQuantities{10.0, 0.0}, account, prices
    );

    // No shared global counter: each worker starts its own sequence, which
    // is what keeps parallel runs reproducible.
    EXPECT_EQ(marketOrder(a[0]).id(), OrderId{1});
    EXPECT_EQ(marketOrder(b[0]).id(), OrderId{1});
}

// A call that throws partway through the slot loop must not have consumed
// any ids: the generator has to be left exactly as it was.
TEST(OrderGeneratorTest, FailedGenerateConsumesNoOrderIds) {
    OrderGenerator generator;
    const Account account = makeAccount();

    // Slot 0 converts fine and draws an id; slot 1 overflows to infinity on
    // a denormal price and throws from inside the loop.
    const std::vector<double> broken{
        100.0, std::numeric_limits<double>::denorm_min()
    };
    ASSERT_THROW(
        generator.generate(TargetWeights{0.5, 0.5}, account, broken),
        std::invalid_argument
    );

    const std::vector<double> prices{100.0, 200.0};
    const std::vector<Order> orders = generator.generate(
        TargetQuantities{10.0, 0.0}, account, prices
    );

    ASSERT_EQ(orders.size(), 1u);
    EXPECT_EQ(marketOrder(orders[0]).id(), OrderId{1});
}

TEST(OrderIdSequenceTest, HandsOutMonotonicIdsStartingAtOne) {
    OrderIdSequence sequence;

    EXPECT_EQ(sequence.peek(), 1u);
    EXPECT_EQ(sequence.next(), OrderId{1});
    EXPECT_EQ(sequence.next(), OrderId{2});
    EXPECT_EQ(sequence.peek(), 3u);

    OrderIdSequence resumed{100};
    EXPECT_FALSE(resumed.exhausted());
    EXPECT_EQ(resumed.next(), OrderId{100});

    EXPECT_THROW(OrderIdSequence{0}, std::invalid_argument);
}

TEST(OrderIdSequenceTest, HandsOutTheLastIdBeforeReportingExhaustion) {
    constexpr std::uint64_t kMax = std::numeric_limits<std::uint64_t>::max();
    OrderIdSequence sequence{kMax};

    // The maximum is a perfectly valid id and must not be skipped.
    EXPECT_FALSE(sequence.exhausted());
    EXPECT_EQ(sequence.next(), OrderId{kMax});

    // Only now is the sequence spent -- and it must say so rather than wrap
    // around to the invalid id zero.
    EXPECT_TRUE(sequence.exhausted());
    EXPECT_THROW(sequence.next(), std::overflow_error);
    EXPECT_THROW(sequence.next(), std::overflow_error);
}
