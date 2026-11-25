#include "account/account.hpp"
#include "strategy/strategy.hpp"
#include "order/order.hpp"
#include "order/order_generator.hpp"
#include "order/order_instruction.hpp"
#include "order/pending_order_book.hpp"

#include "instrument/instrument.hpp"
#include "instrument/instrument_registry.hpp"
#include "instrument/instrument_universe.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace {

constexpr double kInitialCash = 10'000.0;
constexpr auto kEquitySlot = InstrumentIndex{0};
constexpr auto kOptionSlot = InstrumentIndex{1};

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

const Date kDate{std::chrono::sys_days{std::chrono::year{2024} / 6 / 3}};

// The generator never reads an order's submission bar, so one value serves
// every order the tests put into the book.
constexpr std::size_t kBarIndex = 0;

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

// Two slots, matching makeAccount(): slot 0 equity, slot 1 option.
const InstrumentUniverse& universe() {
    static const InstrumentRegistry registry = [] {
        InstrumentRegistry built;
        built.registerInstrument(Equity{"EQ", "EQ"});
        built.registerInstrument(Equity{"OPT", "OPT"});
        return built;
    }();

    static const InstrumentUniverse built = [] {
        std::vector<InstrumentId> ids;
        ids.emplace_back("EQ");
        ids.emplace_back("OPT");
        return InstrumentUniverse{std::move(ids), registry};
    }();

    return built;
}

PendingOrderBook makeBook() {
    return PendingOrderBook{universe()};
}

// An empty book: nothing in flight, so generate() diffs against the
// account position alone.
const PendingOrderBook& noPending() {
    static const PendingOrderBook book = makeBook();
    return book;
}

// The submitted order behind an instruction. Fails the test rather than
// throwing an unhelpful bad_variant_access if it is a cancellation.
const MarketOrder& marketOrder(const OrderInstruction& instruction) {
    EXPECT_TRUE(std::holds_alternative<SubmitOrder>(instruction))
        << "expected a submission, got a cancellation";
    return std::get<MarketOrder>(std::get<SubmitOrder>(instruction).order);
}

bool isCancel(const OrderInstruction& instruction) {
    return std::holds_alternative<CancelOrder>(instruction);
}

OrderId cancelledId(const OrderInstruction& instruction) {
    EXPECT_TRUE(isCancel(instruction))
        << "expected a cancellation, got a submission";
    return std::get<CancelOrder>(instruction).id;
}

// Submits an order into the book under a fixed id, the way an engine would
// have done on an earlier bar.
void submitPending(
    PendingOrderBook& book,
    std::uint64_t id,
    InstrumentIndex slot,
    double quantity
) {
    book.submit(MarketOrder{OrderId{id}, slot, quantity}, kBarIndex);
}

// Drives a pending order part-way to execution: the book keeps the
// remainder, the account keeps what was executed.
void fillPending(
    PendingOrderBook& book,
    Account& account,
    std::uint64_t id,
    InstrumentIndex slot,
    double quantity,
    double price
) {
    const Fill fill{OrderId{id}, slot, quantity, price, 0.0, kDate, 0};
    book.applyFill(fill);
    account.applyFill(fill);
}

} // namespace

// ------------------------------------------------------- quantity targets --

TEST(OrderGeneratorTest, GeneratesLongOrderFromFlatAccount) {
    OrderGenerator generator;
    const Account account = makeAccount();
    const std::vector<double> prices{100.0, 200.0};

    const std::vector<OrderInstruction> orders = generator.generate(
        TargetQuantities{30.0, 0.0}, account, prices, noPending()
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

    const std::vector<OrderInstruction> orders = generator.generate(
        TargetQuantities{-30.0, 0.0}, account, prices, noPending()
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
    const std::vector<OrderInstruction> orders = generator.generate(
        TargetQuantities{30.0, 0.0}, account, prices, noPending()
    );

    ASSERT_EQ(orders.size(), 1u);
    EXPECT_DOUBLE_EQ(marketOrder(orders[0]).quantity(), 10.0);
}

TEST(OrderGeneratorTest, ReversesAPositionInOneOrder) {
    OrderGenerator generator;
    Account account = makeAccount();
    openPosition(account, kEquitySlot, 20.0, 100.0);
    const std::vector<double> prices{100.0, 200.0};

    const std::vector<OrderInstruction> orders = generator.generate(
        TargetQuantities{-15.0, 0.0}, account, prices, noPending()
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
    const std::vector<OrderInstruction> orders = generator.generate(
        TargetQuantities{30.0, 0.0}, account, prices, noPending()
    );

    EXPECT_TRUE(orders.empty());
}

TEST(OrderGeneratorTest, FlatAccountWithAnAllZeroTargetProducesNoOrders) {
    OrderGenerator generator;
    const Account account = makeAccount();
    const std::vector<double> prices{100.0, 200.0};

    EXPECT_TRUE(
        generator.generate(TargetQuantities{0.0, 0.0}, account, prices, noPending()).empty()
    );
    EXPECT_TRUE(
        generator.generate(TargetWeights{0.0, 0.0}, account, prices, noPending()).empty()
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
            TargetQuantities{30.0 + 1e-15, 0.0}, account, prices, noPending()
        ).empty()
    );

    // A genuinely small but real delta still trades.
    const std::vector<OrderInstruction> orders = generator.generate(
        TargetQuantities{30.001, 0.0}, account, prices, noPending()
    );
    ASSERT_EQ(orders.size(), 1u);
    EXPECT_NEAR(marketOrder(orders[0]).quantity(), 0.001, 1e-12);
}

TEST(OrderGeneratorTest, GeneratesOneOrderPerOffTargetSlot) {
    OrderGenerator generator;
    const Account account = makeAccount();
    const std::vector<double> prices{100.0, 200.0};

    const std::vector<OrderInstruction> orders = generator.generate(
        TargetQuantities{10.0, -2.0}, account, prices, noPending()
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
    const std::vector<OrderInstruction> orders = generator.generate(
        TargetWeights{1.0, 0.0}, account, prices, noPending()
    );

    ASSERT_EQ(orders.size(), 1u);
    EXPECT_DOUBLE_EQ(marketOrder(orders[0]).quantity(), 50.0);
}

TEST(OrderGeneratorTest, WeightTargetAppliesTheContractMultiplier) {
    OrderGenerator generator;
    const Account account = makeAccount();
    const std::vector<double> prices{100.0, 200.0};

    // Slot 1 has multiplier 25: 0.5 * 10'000 / (200 * 25) = 1 contract.
    const std::vector<OrderInstruction> orders = generator.generate(
        TargetWeights{0.0, 0.5}, account, prices, noPending()
    );

    ASSERT_EQ(orders.size(), 1u);
    EXPECT_EQ(marketOrder(orders[0]).instrument(), kOptionSlot);
    EXPECT_DOUBLE_EQ(marketOrder(orders[0]).quantity(), 1.0);
}

TEST(OrderGeneratorTest, NegativeWeightGoesShort) {
    OrderGenerator generator;
    const Account account = makeAccount();
    const std::vector<double> prices{100.0, 200.0};

    const std::vector<OrderInstruction> orders = generator.generate(
        TargetWeights{-0.25, 0.0}, account, prices, noPending()
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

    const std::vector<OrderInstruction> orders = generator.generate(
        TargetWeights{0.5, 0.0}, account, prices, noPending()
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
        generator.generate(TargetWeights{0.5, 0.0}, account, prices, noPending()),
        std::invalid_argument
    );
}

TEST(OrderGeneratorTest, RejectsUnusablePriceForNonZeroWeight) {
    OrderGenerator generator;
    const Account account = makeAccount();

    for (const double bad : {0.0, -100.0, kNaN, kInf}) {
        const std::vector<double> prices{bad, 200.0};
        EXPECT_THROW(
            generator.generate(TargetWeights{0.5, 0.0}, account, prices, noPending()),
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
        generator.generate(TargetQuantities{1.0}, account, prices, noPending()),
        std::invalid_argument
    );
    EXPECT_THROW(
        generator.generate(TargetQuantities{1.0, 2.0, 3.0}, account, prices, noPending()),
        std::invalid_argument
    );
    // Oversized weight targets must be caught before the conversion loop
    // walks past the end of the account.
    EXPECT_THROW(
        generator.generate(TargetWeights{0.2, 0.3, 0.5}, account, prices, noPending()),
        std::invalid_argument
    );
}

TEST(OrderGeneratorTest, RejectsPriceSizeMismatch) {
    OrderGenerator generator;
    const Account account = makeAccount();

    EXPECT_THROW(
        generator.generate(
            TargetQuantities{1.0, 2.0}, account, std::vector<double>{100.0}, noPending()
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
            generator.generate(TargetQuantities{bad, 0.0}, account, prices, noPending()),
            std::invalid_argument
        ) << "quantity " << bad;
        EXPECT_THROW(
            generator.generate(TargetWeights{bad, 0.0}, account, prices, noPending()),
            std::invalid_argument
        ) << "weight " << bad;
    }
}

// ----------------------------------------------------------- order id flow --

TEST(OrderGeneratorTest, OrderIdsAreUniqueAndMonotonic) {
    OrderGenerator generator;
    const Account account = makeAccount();
    const std::vector<double> prices{100.0, 200.0};

    const std::vector<OrderInstruction> first = generator.generate(
        TargetQuantities{10.0, 2.0}, account, prices, noPending()
    );
    const std::vector<OrderInstruction> second = generator.generate(
        TargetQuantities{10.0, 2.0}, account, prices, noPending()
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
        TargetQuantities{10.0, 0.0}, account, prices, noPending()
    );
    const auto b = second_worker.generate(
        TargetQuantities{10.0, 0.0}, account, prices, noPending()
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
        generator.generate(TargetWeights{0.5, 0.5}, account, broken, noPending()),
        std::invalid_argument
    );

    const std::vector<double> prices{100.0, 200.0};
    const std::vector<OrderInstruction> orders = generator.generate(
        TargetQuantities{10.0, 0.0}, account, prices, noPending()
    );

    ASSERT_EQ(orders.size(), 1u);
    EXPECT_EQ(marketOrder(orders[0]).id(), OrderId{1});
}

// generate() commits its drafted ids as its last step, which is only
// all-or-nothing if that commit cannot throw.
TEST(OrderIdSequenceTest, CommittingADraftedSequenceCannotThrow) {
    static_assert(std::is_nothrow_copy_assignable_v<OrderIdSequence>);
    static_assert(std::is_nothrow_copy_constructible_v<OrderIdSequence>);
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

// ------------------------------------------------ reconciliation planning --
//
// TR-007.D: the plan reconciles the target against the position AND the
// orders still working, so that after it is applied every slot satisfies
// position + pending == target.

TEST(OrderGeneratorTest, NoPendingOrdersGeneratesSubmit) {
    OrderGenerator generator;
    Account account = makeAccount();
    openPosition(account, kEquitySlot, 40.0, 100.0);
    const std::vector<double> prices{100.0, 200.0};

    const auto plan = generator.generate(
        TargetQuantities{100.0, 0.0}, account, prices, noPending()
    );

    ASSERT_EQ(plan.size(), 1u);
    EXPECT_EQ(marketOrder(plan[0]).instrument(), kEquitySlot);
    EXPECT_DOUBLE_EQ(marketOrder(plan[0]).quantity(), 60.0);
}

TEST(OrderGeneratorTest, CompatiblePendingBuyGeneratesAdditionalBuy) {
    OrderGenerator generator;
    Account account = makeAccount();
    openPosition(account, kEquitySlot, 40.0, 100.0);
    const std::vector<double> prices{100.0, 200.0};

    PendingOrderBook pending = makeBook();
    submitPending(pending, 900, kEquitySlot, 30.0);

    // Needed 60, 30 of it already working: only the missing 30 is ordered,
    // and the working order is left alone.
    const auto plan = generator.generate(
        TargetQuantities{100.0, 0.0}, account, prices, pending
    );

    ASSERT_EQ(plan.size(), 1u);
    EXPECT_DOUBLE_EQ(marketOrder(plan[0]).quantity(), 30.0);
    EXPECT_EQ(marketOrder(plan[0]).side(), OrderSide::BUY);
}

// The short side mirrors the long side: a working SELL that does not yet
// reach the target is topped up, not replaced.
TEST(OrderGeneratorTest, CompatiblePendingSellGeneratesAdditionalSell) {
    OrderGenerator generator;
    Account account = makeAccount();
    openPosition(account, kEquitySlot, -20.0, 100.0);
    const std::vector<double> prices{100.0, 200.0};

    PendingOrderBook pending = makeBook();
    submitPending(pending, 900, kEquitySlot, -30.0);

    const auto plan = generator.generate(
        TargetQuantities{-100.0, 0.0}, account, prices, pending
    );

    ASSERT_EQ(plan.size(), 1u);
    EXPECT_DOUBLE_EQ(marketOrder(plan[0]).quantity(), -50.0);
    EXPECT_EQ(marketOrder(plan[0]).side(), OrderSide::SELL);
}

TEST(OrderGeneratorTest, PendingOrderAlreadyMeetsTarget) {
    OrderGenerator generator;
    Account account = makeAccount();
    openPosition(account, kEquitySlot, 40.0, 100.0);
    const std::vector<double> prices{100.0, 200.0};

    PendingOrderBook pending = makeBook();
    submitPending(pending, 900, kEquitySlot, 60.0);

    EXPECT_TRUE(
        generator.generate(
            TargetQuantities{100.0, 0.0}, account, prices, pending
        ).empty()
    );
}

TEST(OrderGeneratorTest, OversizedPendingBuyIsReplaced) {
    OrderGenerator generator;
    const Account account = makeAccount();
    const std::vector<double> prices{100.0, 200.0};

    PendingOrderBook pending = makeBook();
    submitPending(pending, 900, kEquitySlot, 100.0);

    // A working BUY 100 cannot be trimmed to 50, so it goes and is
    // replaced by the whole remaining need.
    const auto plan = generator.generate(
        TargetQuantities{50.0, 0.0}, account, prices, pending
    );

    ASSERT_EQ(plan.size(), 2u);
    EXPECT_EQ(cancelledId(plan[0]), OrderId{900});
    EXPECT_DOUBLE_EQ(marketOrder(plan[1]).quantity(), 50.0);
    EXPECT_EQ(marketOrder(plan[1]).side(), OrderSide::BUY);
}

TEST(OrderGeneratorTest, OversizedPendingSellIsReplaced) {
    OrderGenerator generator;
    const Account account = makeAccount();
    const std::vector<double> prices{100.0, 200.0};

    PendingOrderBook pending = makeBook();
    submitPending(pending, 900, kEquitySlot, -100.0);

    const auto plan = generator.generate(
        TargetQuantities{-40.0, 0.0}, account, prices, pending
    );

    ASSERT_EQ(plan.size(), 2u);
    EXPECT_EQ(cancelledId(plan[0]), OrderId{900});
    EXPECT_DOUBLE_EQ(marketOrder(plan[1]).quantity(), -40.0);
    EXPECT_EQ(marketOrder(plan[1]).side(), OrderSide::SELL);
}

TEST(OrderGeneratorTest, PendingBuyReversedToSell) {
    OrderGenerator generator;
    const Account account = makeAccount();
    const std::vector<double> prices{100.0, 200.0};

    PendingOrderBook pending = makeBook();
    submitPending(pending, 900, kEquitySlot, 100.0);

    // The replacement is sized off the position, not off the cancelled
    // order: nothing of that BUY was ever executed.
    const auto plan = generator.generate(
        TargetQuantities{-50.0, 0.0}, account, prices, pending
    );

    ASSERT_EQ(plan.size(), 2u);
    EXPECT_EQ(cancelledId(plan[0]), OrderId{900});
    EXPECT_DOUBLE_EQ(marketOrder(plan[1]).quantity(), -50.0);
}

TEST(OrderGeneratorTest, PendingOrderCancelledOnFlatten) {
    OrderGenerator generator;
    Account account = makeAccount();
    openPosition(account, kEquitySlot, 40.0, 100.0);
    const std::vector<double> prices{100.0, 200.0};

    PendingOrderBook pending = makeBook();
    submitPending(pending, 900, kEquitySlot, 60.0);

    const auto plan = generator.generate(
        TargetQuantities{0.0, 0.0}, account, prices, pending
    );

    ASSERT_EQ(plan.size(), 2u);
    EXPECT_EQ(cancelledId(plan[0]), OrderId{900});
    EXPECT_DOUBLE_EQ(marketOrder(plan[1]).quantity(), -40.0);
}

// A slot already flat with nothing but a working order needs the cancel
// and nothing else -- there is no position left to trade out of.
TEST(OrderGeneratorTest, FlatteningAFlatSlotOnlyCancels) {
    OrderGenerator generator;
    const Account account = makeAccount();
    const std::vector<double> prices{100.0, 200.0};

    PendingOrderBook pending = makeBook();
    submitPending(pending, 900, kEquitySlot, 60.0);

    const auto plan = generator.generate(
        TargetQuantities{0.0, 0.0}, account, prices, pending
    );

    ASSERT_EQ(plan.size(), 1u);
    EXPECT_EQ(cancelledId(plan[0]), OrderId{900});
}

TEST(OrderGeneratorTest, PartiallyFilledOrderIsReplaced) {
    OrderGenerator generator;
    Account account = makeAccount();
    const std::vector<double> prices{100.0, 200.0};

    // BUY 100 working, 40 of it executed: the account holds 40, the book
    // still works the remaining 60.
    PendingOrderBook pending = makeBook();
    submitPending(pending, 900, kEquitySlot, 100.0);
    fillPending(pending, account, 900, kEquitySlot, 40.0, 100.0);
    ASSERT_DOUBLE_EQ(account.position(kEquitySlot).quantity(), 40.0);
    ASSERT_DOUBLE_EQ(pending.pendingQuantity(kEquitySlot), 60.0);

    // Target 50 against 40 held and 60 working: the 60 overshoots, so it
    // is cancelled and replaced by an order for the missing 10 only. The
    // executed 40 stays where it is.
    const auto plan = generator.generate(
        TargetQuantities{50.0, 0.0}, account, prices, pending
    );

    ASSERT_EQ(plan.size(), 2u);
    EXPECT_EQ(cancelledId(plan[0]), OrderId{900});
    EXPECT_DOUBLE_EQ(marketOrder(plan[1]).quantity(), 10.0);
    EXPECT_DOUBLE_EQ(account.position(kEquitySlot).quantity(), 40.0);
}

TEST(OrderGeneratorTest, MultiplePendingOrdersAreCancelled) {
    OrderGenerator generator;
    const Account account = makeAccount();
    const std::vector<double> prices{100.0, 200.0};

    PendingOrderBook pending = makeBook();
    submitPending(pending, 900, kEquitySlot, 60.0);
    submitPending(pending, 901, kEquitySlot, 40.0);

    // Every working order on the slot has to go: leaving one behind would
    // overshoot the target once it fills.
    const auto plan = generator.generate(
        TargetQuantities{25.0, 0.0}, account, prices, pending
    );

    ASSERT_EQ(plan.size(), 3u);
    EXPECT_EQ(cancelledId(plan[0]), OrderId{900});
    EXPECT_EQ(cancelledId(plan[1]), OrderId{901});
    EXPECT_DOUBLE_EQ(marketOrder(plan[2]).quantity(), 25.0);
}

// Orders that are already terminal hold no exposure, so cancelling them
// would throw when the plan is applied.
TEST(OrderGeneratorTest, TerminalOrdersAreNotCancelled) {
    OrderGenerator generator;
    Account account = makeAccount();
    const std::vector<double> prices{100.0, 200.0};

    PendingOrderBook pending = makeBook();
    submitPending(pending, 900, kEquitySlot, 30.0);
    fillPending(pending, account, 900, kEquitySlot, 30.0, 100.0);
    submitPending(pending, 901, kEquitySlot, 100.0);

    const auto plan = generator.generate(
        TargetQuantities{50.0, 0.0}, account, prices, pending
    );

    ASSERT_EQ(plan.size(), 2u);
    EXPECT_EQ(cancelledId(plan[0]), OrderId{901});
    EXPECT_DOUBLE_EQ(marketOrder(plan[1]).quantity(), 20.0);
}

TEST(OrderGeneratorTest, IndependentInstruments) {
    OrderGenerator generator;
    Account account = makeAccount();
    openPosition(account, kOptionSlot, 2.0, 200.0);
    const std::vector<double> prices{100.0, 200.0};

    PendingOrderBook pending = makeBook();
    submitPending(pending, 900, kEquitySlot, 10.0);

    // Slot 0 tops up its working order, slot 1 is reconciled against its
    // position alone -- neither slot sees the other's exposure.
    const auto plan = generator.generate(
        TargetQuantities{25.0, 5.0}, account, prices, pending
    );

    ASSERT_EQ(plan.size(), 2u);
    EXPECT_EQ(marketOrder(plan[0]).instrument(), kEquitySlot);
    EXPECT_DOUBLE_EQ(marketOrder(plan[0]).quantity(), 15.0);
    EXPECT_EQ(marketOrder(plan[1]).instrument(), kOptionSlot);
    EXPECT_DOUBLE_EQ(marketOrder(plan[1]).quantity(), 3.0);
}

TEST(OrderGeneratorTest, CancelInstructionsPrecedeSubmissions) {
    OrderGenerator generator;
    const Account account = makeAccount();
    const std::vector<double> prices{100.0, 200.0};

    // Slot 0 needs a replacement, slot 1 needs a plain submission. The
    // slot-1 submit must not slip in front of the slot-0 cancel.
    PendingOrderBook pending = makeBook();
    submitPending(pending, 900, kEquitySlot, 100.0);
    submitPending(pending, 901, kOptionSlot, -4.0);

    const auto plan = generator.generate(
        TargetQuantities{20.0, -1.0}, account, prices, pending
    );

    ASSERT_EQ(plan.size(), 4u);
    EXPECT_EQ(cancelledId(plan[0]), OrderId{900});
    EXPECT_EQ(cancelledId(plan[1]), OrderId{901});
    EXPECT_FALSE(isCancel(plan[2]));
    EXPECT_FALSE(isCancel(plan[3]));

    EXPECT_EQ(marketOrder(plan[2]).instrument(), kEquitySlot);
    EXPECT_DOUBLE_EQ(marketOrder(plan[2]).quantity(), 20.0);
    EXPECT_EQ(marketOrder(plan[3]).instrument(), kOptionSlot);
    EXPECT_DOUBLE_EQ(marketOrder(plan[3]).quantity(), -1.0);
}

// Cancellations draw no ids, so the sequence counts submissions only.
TEST(OrderGeneratorTest, CancellationsConsumeNoOrderIds) {
    OrderGenerator generator;
    const Account account = makeAccount();
    const std::vector<double> prices{100.0, 200.0};

    PendingOrderBook pending = makeBook();
    submitPending(pending, 900, kEquitySlot, 100.0);

    const auto plan = generator.generate(
        TargetQuantities{20.0, 0.0}, account, prices, pending
    );

    ASSERT_EQ(plan.size(), 2u);
    EXPECT_EQ(marketOrder(plan[1]).id(), OrderId{1});

    const auto next = generator.generate(
        TargetQuantities{0.0, 3.0}, account, prices, noPending()
    );
    ASSERT_EQ(next.size(), 1u);
    EXPECT_EQ(marketOrder(next[0]).id(), OrderId{2});
}

TEST(OrderGeneratorTest, FailedGenerationPreservesOrderIds) {
    OrderGenerator generator;
    const Account account = makeAccount();

    PendingOrderBook pending = makeBook();
    submitPending(pending, 900, kEquitySlot, 100.0);

    // Slot 0 plans a replacement and draws an id; slot 1 then overflows to
    // infinity on a denormal price and throws from inside the loop.
    const std::vector<double> broken{
        100.0, std::numeric_limits<double>::denorm_min()
    };
    ASSERT_THROW(
        generator.generate(TargetWeights{0.5, 0.5}, account, broken, pending),
        std::invalid_argument
    );

    const std::vector<double> prices{100.0, 200.0};
    const auto plan = generator.generate(
        TargetQuantities{10.0, 0.0}, account, prices, noPending()
    );

    ASSERT_EQ(plan.size(), 1u);
    EXPECT_EQ(marketOrder(plan[0]).id(), OrderId{1});
}

// The contract of the plan: applying it in order leaves every slot with
// position + pending == target.
TEST(OrderGeneratorTest, ApplyingThePlanReachesTheTarget) {
    OrderGenerator generator;
    Account account = makeAccount();
    openPosition(account, kEquitySlot, 40.0, 100.0);
    openPosition(account, kOptionSlot, 2.0, 200.0);
    const std::vector<double> prices{100.0, 200.0};

    PendingOrderBook pending = makeBook();
    submitPending(pending, 900, kEquitySlot, 80.0);   // overshoots
    submitPending(pending, 901, kOptionSlot, -1.0);   // wrong way

    const TargetQuantities target{70.0, 5.0};
    const auto plan = generator.generate(target, account, prices, pending);

    for (const OrderInstruction& instruction : plan) {
        if (isCancel(instruction)) {
            pending.cancel(cancelledId(instruction));
        } else {
            pending.submit(
                std::get<SubmitOrder>(instruction).order, kBarIndex
            );
        }
    }

    for (const auto slot : {kEquitySlot, kOptionSlot}) {
        EXPECT_DOUBLE_EQ(
            account.position(slot).quantity() + pending.pendingQuantity(slot),
            target[slot.value()]
        ) << "slot " << slot.value();
    }

    // A second pass over the reconciled state has nothing left to do.
    EXPECT_TRUE(generator.generate(target, account, prices, pending).empty());
}
