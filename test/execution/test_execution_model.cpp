// Eligibility and timing, unchanged from TR-008 and pinned against the
// default, frictionless composition: these rules must hold whatever the
// cost policies are. What the policies themselves add is covered in
// test_execution_model_policies.cpp.

#include "execution/execution_model.hpp"

#include "market_data/historical_market_data.hpp"
#include "instrument/instrument.hpp"
#include "instrument/instrument_registry.hpp"
#include "instrument/instrument_universe.hpp"
#include "order/order.hpp"
#include "order/pending_order_book.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr std::size_t kBarCount = 4;
constexpr auto kSlotA = InstrumentIndex{0};
constexpr auto kSlotB = InstrumentIndex{1};

// Opens are distinct per bar and per slot, so a fill priced off the wrong
// bar or the wrong column is visible rather than coincidentally right.
// Slot A opens at 100, 110, 120, 130; slot B at 200, 210, 220, 230.
double expectedOpen(std::size_t bar, std::size_t slot) {
    return 100.0 * static_cast<double>(slot + 1) +
           10.0 * static_cast<double>(bar);
}

const std::vector<std::string>& instrumentIds() {
    static const std::vector<std::string> ids{"EQ_A", "EQ_B"};
    return ids;
}

const HistoricalMarketData& marketData() {
    static const HistoricalMarketData data = [] {
        const auto rows = static_cast<Eigen::Index>(kBarCount);
        const auto cols = static_cast<Eigen::Index>(instrumentIds().size());

        OHLCVData ohlcv;
        ohlcv.open = MarketMatrix::Zero(rows, cols);
        for (Eigen::Index bar = 0; bar < rows; ++bar) {
            for (Eigen::Index slot = 0; slot < cols; ++slot) {
                ohlcv.open(bar, slot) = expectedOpen(
                    static_cast<std::size_t>(bar),
                    static_cast<std::size_t>(slot)
                );
            }
        }

        // The close differs from the open throughout, so a model that
        // reached for the close would not pass by accident.
        ohlcv.close = ohlcv.open * 1.05;
        ohlcv.high = ohlcv.open * 1.10;
        ohlcv.low = ohlcv.open * 0.95;
        ohlcv.volume = MarketMatrix::Constant(rows, cols, 1'000.0);

        std::vector<Date> dates;
        dates.reserve(kBarCount);
        for (std::size_t bar = 0; bar < kBarCount; ++bar) {
            dates.push_back(
                std::chrono::sys_days{std::chrono::year{2024} / 1 / 1} +
                std::chrono::days{static_cast<int>(bar)}
            );
        }

        return HistoricalMarketData{dates, instrumentIds(), ohlcv};
    }();

    return data;
}

const InstrumentUniverse& universe() {
    static const InstrumentRegistry registry = [] {
        InstrumentRegistry built;
        for (const std::string& id : instrumentIds()) {
            built.registerInstrument(Equity{id, id});
        }
        return built;
    }();

    static const InstrumentUniverse built =
        InstrumentUniverse::fromInstrumentIds(instrumentIds(), registry);

    return built;
}

PendingOrderBook makeBook() {
    return PendingOrderBook{universe()};
}

void submit(
    PendingOrderBook& book,
    std::uint64_t id,
    double quantity,
    std::size_t submitted_bar,
    InstrumentIndex slot = kSlotA
) {
    book.submit(MarketOrder{OrderId{id}, slot, quantity}, submitted_bar);
}

// execute() is [[nodiscard]] and EXPECT_THROW evaluates its argument as a
// statement, so a call expected to throw has to discard the result
// explicitly. There are no fills to read when it does throw.
template <typename Model>
void run(
    const Model& model,
    const PendingOrderBook& book,
    std::size_t bar_index
) {
    static_cast<void>(
        model.execute(book, marketData(), universe(), bar_index)
    );
}

} // namespace

TEST(ExecutionModelTest, AnEmptyBookProducesNoFills) {
    const ExecutionModel<> model;
    const PendingOrderBook book = makeBook();

    EXPECT_TRUE(model.execute(book, marketData(), universe(), 1).empty());
}

TEST(ExecutionModelTest, FillsAWorkingOrderAtTheOpenOfTheExecutedBar) {
    const ExecutionModel<> model;
    PendingOrderBook book = makeBook();
    submit(book, 1, 40.0, 0);

    const std::vector<Fill> fills = model.execute(book, marketData(), universe(), 1);

    ASSERT_EQ(fills.size(), 1u);
    const Fill& fill = fills.front();

    EXPECT_EQ(fill.orderId(), OrderId{1});
    EXPECT_EQ(fill.instrument(), kSlotA);
    EXPECT_DOUBLE_EQ(fill.quantity(), 40.0);
    EXPECT_DOUBLE_EQ(fill.price(), expectedOpen(1, 0));
    EXPECT_DOUBLE_EQ(fill.fee(), 0.0);
    EXPECT_EQ(fill.barIndex(), 1u);
    EXPECT_EQ(fill.date(), marketData().date(1));
}

// The guard that keeps the backtest honest: an order may not execute on the
// bar whose prices the strategy has just acted on.
TEST(ExecutionModelTest, DoesNotFillOnTheSubmissionBar) {
    const ExecutionModel<> model;
    PendingOrderBook book = makeBook();
    submit(book, 1, 40.0, 2);

    EXPECT_TRUE(model.execute(book, marketData(), universe(), 2).empty());

    // A later bar does execute it, and at that bar's open.
    const std::vector<Fill> fills = model.execute(book, marketData(), universe(), 3);
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_DOUBLE_EQ(fills.front().price(), expectedOpen(3, 0));
}

// Nothing can have been submitted before the first bar, so bar 0 is always
// a no-op however the book is seeded.
TEST(ExecutionModelTest, FillsNothingOnTheFirstBar) {
    const ExecutionModel<> model;
    PendingOrderBook book = makeBook();
    submit(book, 1, 40.0, 0);

    EXPECT_TRUE(model.execute(book, marketData(), universe(), 0).empty());
}

TEST(ExecutionModelTest, KeepsTheDirectionOfAShortOrder) {
    const ExecutionModel<> model;
    PendingOrderBook book = makeBook();
    submit(book, 1, -15.0, 0);

    const std::vector<Fill> fills = model.execute(book, marketData(), universe(), 1);

    ASSERT_EQ(fills.size(), 1u);
    EXPECT_DOUBLE_EQ(fills.front().quantity(), -15.0);
    EXPECT_EQ(fills.front().side(), OrderSide::SELL);
}

TEST(ExecutionModelTest, SkipsTerminalOrders) {
    const ExecutionModel<> model;
    PendingOrderBook book = makeBook();
    submit(book, 1, 40.0, 0);
    submit(book, 2, 10.0, 0);
    submit(book, 3, 25.0, 0);

    book.applyFill(
        Fill{OrderId{1}, kSlotA, 40.0, 100.0, 0.0, marketData().date(0), 0}
    );
    book.cancel(OrderId{2});

    const std::vector<Fill> fills = model.execute(book, marketData(), universe(), 1);

    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills.front().orderId(), OrderId{3});
}

TEST(ExecutionModelTest, ExecutesOnlyTheOutstandingRemainder) {
    const ExecutionModel<> model;
    PendingOrderBook book = makeBook();
    submit(book, 1, 100.0, 0);

    book.applyFill(
        Fill{OrderId{1}, kSlotA, 30.0, 100.0, 0.0, marketData().date(0), 0}
    );
    ASSERT_EQ(book.get(OrderId{1}).status(), OrderStatus::PARTIALLY_FILLED);

    const std::vector<Fill> fills = model.execute(book, marketData(), universe(), 1);

    ASSERT_EQ(fills.size(), 1u);
    EXPECT_DOUBLE_EQ(fills.front().quantity(), 70.0);
}

TEST(ExecutionModelTest, PricesEachSlotFromItsOwnColumn) {
    const ExecutionModel<> model;
    PendingOrderBook book = makeBook();
    submit(book, 1, 10.0, 0, kSlotA);
    submit(book, 2, -20.0, 0, kSlotB);

    const std::vector<Fill> fills = model.execute(book, marketData(), universe(), 2);

    ASSERT_EQ(fills.size(), 2u);
    // Submission order is preserved, so slot A comes first.
    EXPECT_EQ(fills[0].instrument(), kSlotA);
    EXPECT_DOUBLE_EQ(fills[0].price(), expectedOpen(2, 0));
    EXPECT_EQ(fills[1].instrument(), kSlotB);
    EXPECT_DOUBLE_EQ(fills[1].price(), expectedOpen(2, 1));
}

// The model plans, it does not act. Nothing it returns may have changed the
// book, so the same call twice must give the same answer.
TEST(ExecutionModelTest, LeavesTheBookUntouched) {
    const ExecutionModel<> model;
    PendingOrderBook book = makeBook();
    submit(book, 1, 40.0, 0);

    const std::vector<Fill> first = model.execute(book, marketData(), universe(), 1);
    const std::vector<Fill> second = model.execute(book, marketData(), universe(), 1);

    ASSERT_EQ(first.size(), 1u);
    ASSERT_EQ(second.size(), 1u);
    EXPECT_DOUBLE_EQ(first.front().quantity(), second.front().quantity());

    EXPECT_EQ(book.size(), 1u);
    EXPECT_EQ(book.get(OrderId{1}).status(), OrderStatus::PENDING);
    EXPECT_DOUBLE_EQ(book.pendingQuantity(kSlotA), 40.0);
}

// Applying the returned fills is what retires the orders -- the round trip
// the engine loop will perform.
TEST(ExecutionModelTest, ApplyingTheFillsRetiresTheOrders) {
    const ExecutionModel<> model;
    PendingOrderBook book = makeBook();
    submit(book, 1, 40.0, 0, kSlotA);
    submit(book, 2, -20.0, 0, kSlotB);

    for (const Fill& fill : model.execute(book, marketData(), universe(), 1)) {
        book.applyFill(fill);
    }

    EXPECT_EQ(book.get(OrderId{1}).status(), OrderStatus::FILLED);
    EXPECT_EQ(book.get(OrderId{2}).status(), OrderStatus::FILLED);
    EXPECT_DOUBLE_EQ(book.pendingQuantity(kSlotA), 0.0);
    EXPECT_DOUBLE_EQ(book.pendingQuantity(kSlotB), 0.0);

    // With everything terminal, the next bar has nothing left to execute.
    EXPECT_TRUE(model.execute(book, marketData(), universe(), 2).empty());
}

TEST(ExecutionModelTest, RejectsABarIndexBeyondTheMarketData) {
    const ExecutionModel<> model;
    PendingOrderBook book = makeBook();
    submit(book, 1, 40.0, 0);

    EXPECT_THROW(run(model, book, kBarCount), std::out_of_range);
}

// The bar index is validated up front, before anything is iterated, so an
// invalid one is rejected even when there is nothing to execute. Deferring
// the check until a workable order needed a price would make the same call
// throw or not depending on the contents of the book -- which would hide a
// caller's off-by-one on exactly the bars where no order happens to rest.
TEST(ExecutionModelTest, RejectsAnInvalidBarIndexWithAnEmptyBook) {
    const ExecutionModel<> model;
    const PendingOrderBook book = makeBook();

    EXPECT_THROW(run(model, book, kBarCount), std::out_of_range);
}
