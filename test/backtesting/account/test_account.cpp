#include "account/account.hpp"

#include "common/types.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>
#include <vector>

namespace {

constexpr double kThreshold = 1e-9;
constexpr double kInitialCash = 10'000.0;

const Date kDate{std::chrono::sys_days{std::chrono::year{2024} / 6 / 3}};

constexpr auto kEquitySlot = InstrumentIndex{0};
constexpr auto kOptionSlot = InstrumentIndex{1};

// Slot 0: equity (multiplier 1), slot 1: option (multiplier 25).
std::vector<InstrumentTradingSpec> makeSpecs() {
    return {
        InstrumentTradingSpec{1.0, SettlementConvention::PREMIUM},
        InstrumentTradingSpec{25.0, SettlementConvention::PREMIUM}
    };
}

Account makeAccount(double cash = kInitialCash) {
    return Account{cash, makeSpecs()};
}

std::uint64_t nextOrderId() {
    static std::uint64_t next = 1;
    return next++;
}

Fill fill(
    InstrumentIndex slot,
    double quantity,
    double price,
    double fee = 0.0,
    std::size_t bar = 0
) {
    return Fill{
        OrderId{nextOrderId()}, slot, quantity, price, fee, kDate, bar
    };
}

} // namespace

TEST(AccountTest, StartsFlatWithInitialCash) {
    const Account account = makeAccount();

    EXPECT_DOUBLE_EQ(account.cash(), kInitialCash);
    EXPECT_DOUBLE_EQ(account.initialCash(), kInitialCash);
    EXPECT_DOUBLE_EQ(account.totalFees(), 0.0);
    EXPECT_DOUBLE_EQ(account.realizedPnL(), 0.0);
    ASSERT_EQ(account.size(), 2u);

    for (const PositionState& position : account.positions()) {
        EXPECT_TRUE(position.isFlat());
    }
}

TEST(AccountTest, RejectsInvalidConstruction) {
    EXPECT_THROW(Account(-1.0, makeSpecs()), std::invalid_argument);
    EXPECT_THROW(
        Account(std::numeric_limits<double>::quiet_NaN(), makeSpecs()),
        std::invalid_argument
    );
    EXPECT_THROW(Account(kInitialCash, {}), std::invalid_argument);
    EXPECT_NO_THROW(Account(0.0, makeSpecs()));
}

TEST(AccountTest, BuyingReducesCashAndOpensPosition) {
    Account account = makeAccount();
    account.applyFill(fill(kEquitySlot, 10.0, 100.0, 1.0));

    EXPECT_NEAR(account.cash(), kInitialCash - 1000.0 - 1.0, kThreshold);
    EXPECT_NEAR(account.totalFees(), 1.0, kThreshold);
    EXPECT_DOUBLE_EQ(account.realizedPnL(), 0.0);

    const PositionState& position = account.position(kEquitySlot);
    EXPECT_DOUBLE_EQ(position.quantity(), 10.0);
    EXPECT_DOUBLE_EQ(position.averagePrice(), 100.0);
}

TEST(AccountTest, RoundTripRealizesGrossPnLAndKeepsFeesSeparate) {
    Account account = makeAccount();
    account.applyFill(fill(kEquitySlot, 10.0, 100.0, 1.0));
    account.applyFill(fill(kEquitySlot, -10.0, 130.0, 1.0));

    EXPECT_NEAR(account.cash(), kInitialCash + 300.0 - 2.0, kThreshold);
    EXPECT_NEAR(account.realizedPnL(), 300.0, kThreshold);
    EXPECT_NEAR(account.totalFees(), 2.0, kThreshold);
    EXPECT_TRUE(account.position(kEquitySlot).isFlat());

    // The invariant that keeps fees from being counted twice: a flat
    // account's cash change is exactly gross PnL minus fees.
    EXPECT_NEAR(
        account.cash() - account.initialCash(),
        account.realizedPnL() - account.totalFees(),
        kThreshold
    );
}

TEST(AccountTest, ShortSaleCreditsCashAndRealizesOnBuyBack) {
    Account account = makeAccount();
    account.applyFill(fill(kEquitySlot, -10.0, 100.0));

    EXPECT_NEAR(account.cash(), kInitialCash + 1000.0, kThreshold);
    EXPECT_TRUE(account.position(kEquitySlot).isShort());

    account.applyFill(fill(kEquitySlot, 10.0, 90.0));

    EXPECT_NEAR(account.cash(), kInitialCash + 100.0, kThreshold);
    EXPECT_NEAR(account.realizedPnL(), 100.0, kThreshold);
    EXPECT_TRUE(account.position(kEquitySlot).isFlat());
}

TEST(AccountTest, ReversalSettlesBothLegsInOneFill) {
    Account account = makeAccount();
    account.applyFill(fill(kEquitySlot, 10.0, 100.0));
    account.applyFill(fill(kEquitySlot, -15.0, 110.0));

    // Cash: -1000 on the buy, +1650 on the sale.
    EXPECT_NEAR(account.cash(), kInitialCash + 650.0, kThreshold);
    EXPECT_NEAR(account.realizedPnL(), 100.0, kThreshold);

    const PositionState& position = account.position(kEquitySlot);
    EXPECT_DOUBLE_EQ(position.quantity(), -5.0);
    EXPECT_DOUBLE_EQ(position.averagePrice(), 110.0);
}

TEST(AccountTest, AppliesTheContractMultiplierOnceOnTheCashPath) {
    Account account = makeAccount();

    // 2 option contracts at premium 10 with multiplier 25.
    account.applyFill(fill(kOptionSlot, 2.0, 10.0));
    EXPECT_NEAR(account.cash(), kInitialCash - 500.0, kThreshold);

    account.applyFill(fill(kOptionSlot, -2.0, 12.0));
    EXPECT_NEAR(account.cash(), kInitialCash + 100.0, kThreshold);
    EXPECT_NEAR(account.realizedPnL(), 100.0, kThreshold);
}

TEST(AccountTest, KeepsPositionsIndependentPerSlot) {
    Account account = makeAccount();
    account.applyFill(fill(kEquitySlot, 10.0, 100.0));
    account.applyFill(fill(kOptionSlot, -1.0, 8.0));

    EXPECT_TRUE(account.position(kEquitySlot).isLong());
    EXPECT_TRUE(account.position(kOptionSlot).isShort());
    EXPECT_NEAR(
        account.cash(),
        kInitialCash - 1000.0 + 200.0,
        kThreshold
    );
}

TEST(AccountTest, AggregatesRealizedPnLAcrossSlots) {
    Account account = makeAccount();
    account.applyFill(fill(kEquitySlot, 10.0, 100.0));
    account.applyFill(fill(kEquitySlot, -10.0, 110.0));   // +100
    account.applyFill(fill(kOptionSlot, 1.0, 10.0));
    account.applyFill(fill(kOptionSlot, -1.0, 8.0));      // -50

    EXPECT_NEAR(account.realizedPnL(), 50.0, kThreshold);
}

TEST(AccountTest, RejectsFillsForUnknownSlots) {
    Account account = makeAccount();

    EXPECT_THROW(
        account.applyFill(fill(InstrumentIndex{2}, 1.0, 100.0)),
        std::out_of_range
    );
    EXPECT_THROW(account.position(InstrumentIndex{2}), std::out_of_range);
    EXPECT_THROW(account.spec(InstrumentIndex{9}), std::out_of_range);

    // Nothing was settled.
    EXPECT_DOUBLE_EQ(account.cash(), kInitialCash);
}

TEST(AccountTest, RejectsMarginSettledFills) {
    Account account{
        kInitialCash,
        {InstrumentTradingSpec{50.0, SettlementConvention::MARGIN}}
    };

    EXPECT_THROW(
        account.applyFill(fill(kEquitySlot, 1.0, 100.0)),
        std::logic_error
    );
    EXPECT_DOUBLE_EQ(account.cash(), kInitialCash);
}

TEST(AccountTest, LeavesAccountUntouchedWhenAFillIsRejected) {
    Account account = makeAccount();
    account.applyFill(fill(kEquitySlot, 10.0, 100.0, 1.0));

    const double cash = account.cash();
    const double fees = account.totalFees();
    const double realized = account.realizedPnL();
    const double quantity = account.position(kEquitySlot).quantity();

    // A fill for a slot this account does not have.
    EXPECT_THROW(
        account.applyFill(fill(InstrumentIndex{7}, 1.0, 100.0, 5.0)),
        std::out_of_range
    );

    EXPECT_DOUBLE_EQ(account.cash(), cash);
    EXPECT_DOUBLE_EQ(account.totalFees(), fees);
    EXPECT_DOUBLE_EQ(account.realizedPnL(), realized);
    EXPECT_DOUBLE_EQ(account.position(kEquitySlot).quantity(), quantity);
}

TEST(AccountTest, OverflowingCashLeavesTheAccountIntact) {
    const double huge = std::numeric_limits<double>::max();
    Account account{huge, makeSpecs()};

    // Selling credits cash beyond what a double can hold.
    EXPECT_THROW(
        account.applyFill(fill(kEquitySlot, -huge, huge)),
        std::overflow_error
    );

    EXPECT_DOUBLE_EQ(account.cash(), huge);
    EXPECT_TRUE(account.position(kEquitySlot).isFlat());
}

TEST(AccountTest, ProcessesFillsWithoutReallocating) {
    Account account = makeAccount();
    const PositionState* before = account.positions().data();

    for (int i = 0; i < 1'000; ++i) {
        account.applyFill(fill(kEquitySlot, 1.0, 100.0));
        account.applyFill(fill(kEquitySlot, -1.0, 101.0));
    }

    // Dense storage is sized at construction: the position vector never
    // moves, so no fill allocated.
    EXPECT_EQ(account.positions().data(), before);
    EXPECT_NEAR(account.realizedPnL(), 1'000.0, 1e-6);
}

TEST(AccountTest, ReplayingFillsReproducesTheSameState) {
    const std::vector<Fill> fills{
        fill(kEquitySlot, 10.0, 100.0, 1.0, 0),
        fill(kOptionSlot, 2.0, 10.0, 0.5, 1),
        fill(kEquitySlot, -15.0, 110.0, 1.0, 2),
        fill(kOptionSlot, -2.0, 12.0, 0.5, 3)
    };

    Account first = makeAccount();
    for (const Fill& f : fills) {
        first.applyFill(f);
    }

    Account replayed = makeAccount();
    for (const Fill& f : fills) {
        replayed.applyFill(f);
    }

    EXPECT_DOUBLE_EQ(first.cash(), replayed.cash());
    EXPECT_DOUBLE_EQ(first.realizedPnL(), replayed.realizedPnL());
    EXPECT_DOUBLE_EQ(first.totalFees(), replayed.totalFees());
    for (std::size_t slot = 0; slot < first.size(); ++slot) {
        const auto index = InstrumentIndex{slot};
        EXPECT_DOUBLE_EQ(
            first.position(index).quantity(),
            replayed.position(index).quantity()
        );
        EXPECT_DOUBLE_EQ(
            first.position(index).averagePrice(),
            replayed.position(index).averagePrice()
        );
    }
}

TEST(AccountTest, BuildsPositionsFromAUniverse) {
    InstrumentRegistry registry;
    registry.registerInstrument(Equity{"AAPL", "AAPL"});
    registry.registerInstrument(EuropeanOption{
        "AAPL-CALL", "AAPL", 100.0,
        std::chrono::sys_days{std::chrono::year{2024} / 1 / 1},
        std::chrono::sys_days{std::chrono::year{2024} / 6 / 1},
        OptionType::CALL, 25.0
    });

    const InstrumentUniverse universe{
        {InstrumentId{"AAPL"}, InstrumentId{"AAPL-CALL"}}, registry
    };

    const Account account = Account::forUniverse(kInitialCash, universe);

    ASSERT_EQ(account.size(), universe.size());
    EXPECT_DOUBLE_EQ(
        account.spec(kEquitySlot).contractMultiplier(), 1.0
    );
    EXPECT_DOUBLE_EQ(
        account.spec(kOptionSlot).contractMultiplier(), 25.0
    );
}
