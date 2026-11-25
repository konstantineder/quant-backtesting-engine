#include "execution/fill.hpp"
#include "order/order.hpp"
#include "order/pending_order_book.hpp"

#include "instrument/instrument.hpp"
#include "instrument/instrument_registry.hpp"
#include "instrument/instrument_universe.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

// Wide enough for every slot the tests below address.
constexpr std::size_t kSlotCount = 10;

std::string slotId(std::size_t slot) {
    return "INST" + std::to_string(slot);
}

// The book only needs the universe for its slot count, so one shared
// universe serves every test.
const InstrumentUniverse& universe() {
    static const InstrumentRegistry registry = [] {
        InstrumentRegistry built;
        for (std::size_t slot = 0; slot < kSlotCount; ++slot) {
            built.registerInstrument(Equity{slotId(slot), slotId(slot)});
        }
        return built;
    }();

    static const InstrumentUniverse built = [] {
        std::vector<InstrumentId> ids;
        ids.reserve(kSlotCount);
        for (std::size_t slot = 0; slot < kSlotCount; ++slot) {
            ids.emplace_back(slotId(slot));
        }
        return InstrumentUniverse{std::move(ids), registry};
    }();

    return built;
}

PendingOrderBook makeBook() {
    return PendingOrderBook{universe()};
}

const Date kDate{std::chrono::sys_days{std::chrono::year{2024} / 6 / 3}};

// The bar an order was submitted on. The book only stores it, so a single
// value serves every test that does not read it back.
constexpr std::size_t kBarIndex = 0;

constexpr InstrumentIndex kSlot{0};

Order makeOrder(
    std::uint64_t id,
    double quantity = 100.0,
    InstrumentIndex slot = kSlot
) {
    return MarketOrder{OrderId{id}, slot, quantity};
}

Fill makeFill(
    std::uint64_t order_id,
    double quantity,
    InstrumentIndex slot = kSlot
) {
    return Fill{OrderId{order_id}, slot, quantity, 100.0, 0.0, kDate, 0};
}

std::vector<OrderId> submittedIds(const PendingOrderBook& book) {
    std::vector<OrderId> ids;
    for (const OrderState& state : book.orders()) {
        ids.push_back(state.orderId());
    }
    return ids;
}

} // namespace

TEST(PendingOrderBookTest, StartsEmpty) {
    const PendingOrderBook book = makeBook();

    EXPECT_EQ(book.size(), 0u);
    EXPECT_TRUE(book.empty());
    EXPECT_TRUE(book.orders().empty());
}

TEST(PendingOrderBookTest, SubmitSingleOrder) {
    PendingOrderBook book = makeBook();
    book.submit(makeOrder(1, 100.0), kBarIndex);

    ASSERT_EQ(book.size(), 1u);
    EXPECT_FALSE(book.empty());

    const OrderState& state = book.get(OrderId{1});
    EXPECT_EQ(state.orderId(), OrderId{1});
    EXPECT_EQ(state.instrument(), kSlot);
    EXPECT_EQ(state.status(), OrderStatus::PENDING);
    EXPECT_DOUBLE_EQ(state.remainingQuantity(), 100.0);
}

TEST(PendingOrderBookTest, SubmitMultipleOrders) {
    PendingOrderBook book = makeBook();
    book.submit(makeOrder(1, 10.0, InstrumentIndex{0}), kBarIndex);
    book.submit(makeOrder(2, -20.0, InstrumentIndex{1}), kBarIndex);
    book.submit(makeOrder(3, 30.0, InstrumentIndex{2}), kBarIndex);

    ASSERT_EQ(book.size(), 3u);

    // Every submitted order is reachable by its own id, and the lookup
    // returns the state that actually belongs to that id.
    EXPECT_EQ(book.get(OrderId{1}).instrument(), InstrumentIndex{0});
    EXPECT_DOUBLE_EQ(book.get(OrderId{1}).remainingQuantity(), 10.0);

    EXPECT_EQ(book.get(OrderId{2}).instrument(), InstrumentIndex{1});
    EXPECT_DOUBLE_EQ(book.get(OrderId{2}).remainingQuantity(), -20.0);

    EXPECT_EQ(book.get(OrderId{3}).instrument(), InstrumentIndex{2});
    EXPECT_DOUBLE_EQ(book.get(OrderId{3}).remainingQuantity(), 30.0);
}

TEST(PendingOrderBookTest, DuplicateOrderRejected) {
    PendingOrderBook book = makeBook();
    book.submit(makeOrder(1, 100.0), kBarIndex);
    book.submit(makeOrder(2, 50.0), kBarIndex);
    book.applyFill(makeFill(1, 40.0));

    EXPECT_THROW(book.submit(makeOrder(1, 999.0), kBarIndex), std::runtime_error);

    // Neither the size nor the already registered state may change.
    EXPECT_EQ(book.size(), 2u);
    EXPECT_DOUBLE_EQ(book.get(OrderId{1}).remainingQuantity(), 60.0);
    EXPECT_EQ(book.get(OrderId{1}).status(), OrderStatus::PARTIALLY_FILLED);
    EXPECT_EQ(submittedIds(book), (std::vector<OrderId>{OrderId{1}, OrderId{2}}));
}

TEST(PendingOrderBookTest, ApplyPartialFill) {
    PendingOrderBook book = makeBook();
    book.submit(makeOrder(1, 100.0), kBarIndex);
    book.submit(makeOrder(2, 100.0), kBarIndex);

    book.applyFill(makeFill(1, 30.0));

    EXPECT_EQ(book.get(OrderId{1}).status(), OrderStatus::PARTIALLY_FILLED);
    EXPECT_DOUBLE_EQ(book.get(OrderId{1}).remainingQuantity(), 70.0);

    // The fill is routed to the addressed order only.
    EXPECT_EQ(book.get(OrderId{2}).status(), OrderStatus::PENDING);
    EXPECT_DOUBLE_EQ(book.get(OrderId{2}).remainingQuantity(), 100.0);

    // A partially filled order stays in the book.
    EXPECT_EQ(book.size(), 2u);
}

TEST(PendingOrderBookTest, ApplyFullFill) {
    PendingOrderBook book = makeBook();
    book.submit(makeOrder(1, 100.0), kBarIndex);

    book.applyFill(makeFill(1, 60.0));
    book.applyFill(makeFill(1, 40.0));

    EXPECT_EQ(book.get(OrderId{1}).status(), OrderStatus::FILLED);
    EXPECT_DOUBLE_EQ(book.get(OrderId{1}).remainingQuantity(), 0.0);
    EXPECT_TRUE(book.get(OrderId{1}).isTerminal());

    // Reaching a terminal status does not by itself remove the order.
    EXPECT_EQ(book.size(), 1u);
}

TEST(PendingOrderBookTest, CancelOrder) {
    PendingOrderBook book = makeBook();
    book.submit(makeOrder(1, 100.0), kBarIndex);
    book.submit(makeOrder(2, 100.0), kBarIndex);

    book.applyFill(makeFill(1, 30.0));
    book.cancel(OrderId{1});

    EXPECT_EQ(book.get(OrderId{1}).status(), OrderStatus::CANCELLED);
    EXPECT_TRUE(book.get(OrderId{1}).isTerminal());
    // Cancelling does not undo what was already executed.
    EXPECT_DOUBLE_EQ(book.get(OrderId{1}).remainingQuantity(), 70.0);

    EXPECT_EQ(book.get(OrderId{2}).status(), OrderStatus::PENDING);
    EXPECT_EQ(book.size(), 2u);
}

TEST(PendingOrderBookTest, RejectsFillAndCancelForUnknownOrder) {
    PendingOrderBook book = makeBook();
    book.submit(makeOrder(1, 100.0), kBarIndex);

    EXPECT_THROW(book.applyFill(makeFill(99, 10.0)), std::out_of_range);
    EXPECT_THROW(book.cancel(OrderId{99}), std::out_of_range);
    EXPECT_THROW(book.get(OrderId{99}), std::out_of_range);

    EXPECT_EQ(book.size(), 1u);
    EXPECT_EQ(book.get(OrderId{1}).status(), OrderStatus::PENDING);
}

TEST(PendingOrderBookTest, RemoveTerminalOrders) {
    PendingOrderBook book = makeBook();
    for (std::uint64_t id = 1; id <= 5; ++id) {
        book.submit(makeOrder(id, 100.0), kBarIndex);
    }

    book.applyFill(makeFill(2, 100.0));  // FILLED
    book.cancel(OrderId{4});             // CANCELLED
    book.applyFill(makeFill(5, 25.0));   // PARTIALLY_FILLED, stays active

    book.removeTerminal();

    ASSERT_EQ(book.size(), 3u);
    EXPECT_EQ(
        submittedIds(book),
        (std::vector<OrderId>{OrderId{1}, OrderId{3}, OrderId{5}})
    );

    // The terminal ones are gone for good, including from the index.
    EXPECT_THROW(book.get(OrderId{2}), std::out_of_range);
    EXPECT_THROW(book.get(OrderId{4}), std::out_of_range);

    // Survivors keep their state across the compaction.
    EXPECT_EQ(book.get(OrderId{5}).status(), OrderStatus::PARTIALLY_FILLED);
    EXPECT_DOUBLE_EQ(book.get(OrderId{5}).remainingQuantity(), 75.0);

    // Removing again with nothing terminal left is a no-op.
    book.removeTerminal();
    EXPECT_EQ(book.size(), 3u);
}

TEST(PendingOrderBookTest, RemoveTerminalOnEmptyAndAllTerminalBook) {
    PendingOrderBook book = makeBook();
    book.removeTerminal();
    EXPECT_TRUE(book.empty());

    book.submit(makeOrder(1, 10.0), kBarIndex);
    book.submit(makeOrder(2, 10.0), kBarIndex);
    book.applyFill(makeFill(1, 10.0));
    book.cancel(OrderId{2});

    book.removeTerminal();

    EXPECT_TRUE(book.empty());
    EXPECT_EQ(book.size(), 0u);
    EXPECT_THROW(book.get(OrderId{1}), std::out_of_range);

    // The freed id space is reusable once the order is gone.
    EXPECT_NO_THROW(book.submit(makeOrder(1, 10.0), kBarIndex));
    EXPECT_EQ(book.size(), 1u);
}

// The index maps every surviving id onto the slot that really holds its
// state. A compaction that shifts elements without rebuilding the index
// would pass size() checks but hand out the wrong OrderState here.
TEST(PendingOrderBookTest, IndexConsistencyAfterRemoval) {
    PendingOrderBook book = makeBook();
    for (std::uint64_t id = 1; id <= 8; ++id) {
        // Distinct quantity and instrument per id, so a mixed-up index is
        // visible rather than coincidentally correct.
        book.submit(
            makeOrder(
                id,
                static_cast<double>(id) * 10.0,
                InstrumentIndex{static_cast<std::size_t>(id)}
            ),
            kBarIndex
        );
    }

    // Retire a mix: first, last and some in the middle.
    book.applyFill(makeFill(1, 10.0, InstrumentIndex{1}));
    book.cancel(OrderId{3});
    book.cancel(OrderId{4});
    book.applyFill(makeFill(8, 80.0, InstrumentIndex{8}));

    book.removeTerminal();

    const std::span<const OrderState> active = book.orders();
    ASSERT_EQ(active.size(), 4u);

    for (std::size_t slot = 0; slot < active.size(); ++slot) {
        const OrderId id = active[slot].orderId();

        // get(id) must resolve to exactly the element at this slot ...
        EXPECT_EQ(&book.get(id), &active[slot]);
        // ... and that element must still carry its own payload.
        const auto raw_id = id.value();
        EXPECT_EQ(
            active[slot].instrument(),
            InstrumentIndex{static_cast<std::size_t>(raw_id)}
        );
        EXPECT_DOUBLE_EQ(
            active[slot].remainingQuantity(),
            static_cast<double>(raw_id) * 10.0
        );
    }

    // A fill after the compaction still lands on the right order.
    book.applyFill(makeFill(7, 70.0, InstrumentIndex{7}));
    EXPECT_EQ(book.get(OrderId{7}).status(), OrderStatus::FILLED);
    EXPECT_EQ(book.get(OrderId{2}).status(), OrderStatus::PENDING);
    EXPECT_EQ(book.get(OrderId{5}).status(), OrderStatus::PENDING);
    EXPECT_EQ(book.get(OrderId{6}).status(), OrderStatus::PENDING);
}

// Iteration order drives which order consumes cash first, so it must be
// submission order and nothing else -- an unordered_map as primary storage
// would make backtests irreproducible.
TEST(PendingOrderBookTest, PreserveSubmissionOrder) {
    PendingOrderBook book = makeBook();
    const std::vector<std::uint64_t> submission{7, 3, 9, 1, 42, 5};

    for (const std::uint64_t id : submission) {
        book.submit(makeOrder(id, 100.0), kBarIndex);
    }

    std::vector<OrderId> expected;
    for (const std::uint64_t id : submission) {
        expected.push_back(OrderId{id});
    }
    EXPECT_EQ(submittedIds(book), expected);

    // Fills and cancels must not reshuffle anything.
    book.applyFill(makeFill(9, 50.0));
    book.cancel(OrderId{1});
    EXPECT_EQ(submittedIds(book), expected);

    // Compaction keeps the relative order of the survivors.
    book.removeTerminal();
    EXPECT_EQ(
        submittedIds(book),
        (std::vector<OrderId>{
            OrderId{7}, OrderId{3}, OrderId{9}, OrderId{42}, OrderId{5}
        })
    );

    // New submissions append at the back.
    book.submit(makeOrder(100, 100.0), kBarIndex);
    EXPECT_EQ(
        submittedIds(book),
        (std::vector<OrderId>{
            OrderId{7}, OrderId{3}, OrderId{9}, OrderId{42}, OrderId{5},
            OrderId{100}
        })
    );
}

// ------------------------------------------------- aggregate pending size --

TEST(PendingOrderBookTest, PendingQuantityStartsAtZeroForEverySlot) {
    const PendingOrderBook book = makeBook();

    for (std::size_t slot = 0; slot < kSlotCount; ++slot) {
        EXPECT_DOUBLE_EQ(book.pendingQuantity(InstrumentIndex{slot}), 0.0)
            << "slot " << slot;
    }
}

TEST(PendingOrderBookTest, PendingQuantityAggregatesPerSlot) {
    PendingOrderBook book = makeBook();
    book.submit(makeOrder(1, 100.0, InstrumentIndex{0}), kBarIndex);
    book.submit(makeOrder(2, 25.0, InstrumentIndex{0}), kBarIndex);
    book.submit(makeOrder(3, -40.0, InstrumentIndex{1}), kBarIndex);

    // Same-slot orders net into one exposure ...
    EXPECT_DOUBLE_EQ(book.pendingQuantity(InstrumentIndex{0}), 125.0);
    // ... and slots do not bleed into each other.
    EXPECT_DOUBLE_EQ(book.pendingQuantity(InstrumentIndex{1}), -40.0);
    EXPECT_DOUBLE_EQ(book.pendingQuantity(InstrumentIndex{2}), 0.0);
}

TEST(PendingOrderBookTest, FillsAndCancelsRetireTheirPendingQuantity) {
    PendingOrderBook book = makeBook();
    book.submit(makeOrder(1, 100.0), kBarIndex);
    book.submit(makeOrder(2, 50.0), kBarIndex);

    book.applyFill(makeFill(1, 30.0));
    EXPECT_DOUBLE_EQ(book.pendingQuantity(kSlot), 120.0);

    book.applyFill(makeFill(1, 70.0));
    EXPECT_DOUBLE_EQ(book.pendingQuantity(kSlot), 50.0);

    // A cancel releases what was never executed, and nothing more.
    book.cancel(OrderId{2});
    EXPECT_DOUBLE_EQ(book.pendingQuantity(kSlot), 0.0);

    // Compaction is bookkeeping only: the exposure is already retired.
    book.removeTerminal();
    EXPECT_TRUE(book.empty());
    EXPECT_DOUBLE_EQ(book.pendingQuantity(kSlot), 0.0);
}

TEST(PendingOrderBookTest, RejectsAnOrderOpposingPendingExposure) {
    PendingOrderBook book = makeBook();
    book.submit(makeOrder(1, 100.0), kBarIndex);

    EXPECT_THROW(book.submit(makeOrder(2, -10.0), kBarIndex), std::logic_error);

    // The rejected order left no trace: no state, no exposure change.
    EXPECT_EQ(book.size(), 1u);
    EXPECT_THROW(book.get(OrderId{2}), std::out_of_range);
    EXPECT_DOUBLE_EQ(book.pendingQuantity(kSlot), 100.0);

    // Opposing the exposure on another slot is not a conflict.
    EXPECT_NO_THROW(
        book.submit(makeOrder(3, -10.0, InstrumentIndex{1}), kBarIndex)
    );
}

TEST(PendingOrderBookTest, SlotWithNoExposureLeftAcceptsTheOppositeSide) {
    PendingOrderBook book = makeBook();
    book.submit(makeOrder(1, 100.0), kBarIndex);
    book.applyFill(makeFill(1, 100.0));

    // The buy is done, so a sell is no longer a conflicting order.
    ASSERT_DOUBLE_EQ(book.pendingQuantity(kSlot), 0.0);
    EXPECT_NO_THROW(book.submit(makeOrder(2, -60.0), kBarIndex));
    EXPECT_DOUBLE_EQ(book.pendingQuantity(kSlot), -60.0);
}

TEST(PendingOrderBookTest, RejectsAnOrderOnAnUnknownSlot) {
    PendingOrderBook book = makeBook();

    EXPECT_THROW(
        book.submit(makeOrder(1, 10.0, InstrumentIndex{kSlotCount}), kBarIndex),
        std::out_of_range
    );
    EXPECT_TRUE(book.empty());
}

// Reading an unknown slot is a caller error too, and must be reported the
// same way the write side reports it rather than read past the end.
TEST(PendingOrderBookTest, RejectsAPendingQuantityQueryForAnUnknownSlot) {
    const PendingOrderBook book = makeBook();

    EXPECT_THROW(
        book.pendingQuantity(InstrumentIndex{kSlotCount}),
        std::out_of_range
    );
    EXPECT_NO_THROW(book.pendingQuantity(InstrumentIndex{kSlotCount - 1}));
}
