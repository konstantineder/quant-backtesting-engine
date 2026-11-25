#include "instrument/instrument_trading_spec.hpp"

#include "common/types.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>

namespace {

constexpr double kThreshold = 1e-9;
constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

const Date kStart{std::chrono::sys_days{std::chrono::year{2024} / 1 / 1}};
const Date kExpiry{std::chrono::sys_days{std::chrono::year{2024} / 6 / 1}};

Equity makeEquity() { return Equity{"AAPL", "AAPL"}; }

EuropeanOption makeOption(double multiplier = 25.0) {
    return EuropeanOption{
        "AAPL-CALL", "AAPL", 100.0, kStart, kExpiry,
        OptionType::CALL, multiplier
    };
}

Future makeFuture() {
    return Future{"ES-FUT", "AAPL", 50.0, kStart, kExpiry};
}

InstrumentTradingSpec premiumSpec(double multiplier) {
    return InstrumentTradingSpec{
        multiplier, SettlementConvention::PREMIUM
    };
}

} // namespace

TEST(InstrumentTradingSpecTest, EquitySettlesPremiumWithMultiplierOne) {
    const auto spec =
        InstrumentTradingSpec::forInstrument(makeEquity());

    EXPECT_DOUBLE_EQ(spec.contractMultiplier(), 1.0);
    EXPECT_EQ(spec.settlement(), SettlementConvention::PREMIUM);
}

TEST(InstrumentTradingSpecTest, OptionSettlesPremiumWithItsMultiplier) {
    const auto spec =
        InstrumentTradingSpec::forInstrument(makeOption(25.0));

    EXPECT_DOUBLE_EQ(spec.contractMultiplier(), 25.0);
    EXPECT_EQ(spec.settlement(), SettlementConvention::PREMIUM);
}

TEST(InstrumentTradingSpecTest, RejectsFuturesExplicitly) {
    EXPECT_THROW(
        InstrumentTradingSpec::forInstrument(makeFuture()),
        std::invalid_argument
    );
}

TEST(InstrumentTradingSpecTest, RejectsInvalidMultipliers) {
    for (double multiplier : {0.0, -1.0, kInf, -kInf, kNaN}) {
        EXPECT_THROW(
            premiumSpec(multiplier), std::invalid_argument
        );
    }
}

TEST(InstrumentTradingSpecTest, BuildsSpecsAlignedWithTheUniverse) {
    InstrumentRegistry registry;
    registry.registerInstrument(makeEquity());
    registry.registerInstrument(makeOption(25.0));

    const InstrumentUniverse universe{
        {InstrumentId{"AAPL"}, InstrumentId{"AAPL-CALL"}}, registry
    };

    const auto specs = makeTradingSpecs(universe);

    ASSERT_EQ(specs.size(), 2u);
    EXPECT_DOUBLE_EQ(specs[0].contractMultiplier(), 1.0);
    EXPECT_DOUBLE_EQ(specs[1].contractMultiplier(), 25.0);
}

TEST(InstrumentTradingSpecTest, UniverseWithFuturesIsRejected) {
    InstrumentRegistry registry;
    registry.registerInstrument(makeEquity());
    registry.registerInstrument(makeFuture());

    const InstrumentUniverse universe{
        {InstrumentId{"AAPL"}, InstrumentId{"ES-FUT"}}, registry
    };

    EXPECT_THROW(makeTradingSpecs(universe), std::invalid_argument);
}

TEST(SettlementTest, BuyingPaysNotionalAndFee) {
    const double cash_flow =
        settlementCashFlow(premiumSpec(1.0), 10.0, 100.0, 2.5);

    EXPECT_NEAR(cash_flow, -1002.5, kThreshold);
}

TEST(SettlementTest, SellingReceivesNotionalMinusFee) {
    const double cash_flow =
        settlementCashFlow(premiumSpec(1.0), -10.0, 100.0, 2.5);

    EXPECT_NEAR(cash_flow, 997.5, kThreshold);
}

TEST(SettlementTest, ShortSaleCreditsCash) {
    // Opening a short is a sale: the proceeds land in cash.
    EXPECT_NEAR(
        settlementCashFlow(premiumSpec(1.0), -5.0, 200.0, 0.0),
        1000.0,
        kThreshold
    );
}

TEST(SettlementTest, AppliesTheContractMultiplierExactlyOnce) {
    // 2 contracts, premium 10, multiplier 25 -> 500 notional.
    EXPECT_NEAR(
        settlementCashFlow(premiumSpec(25.0), 2.0, 10.0, 0.0),
        -500.0,
        kThreshold
    );
}

TEST(SettlementTest, FeeNeverChangesSign) {
    const double buy =
        settlementCashFlow(premiumSpec(1.0), 1.0, 100.0, 5.0);
    const double sell =
        settlementCashFlow(premiumSpec(1.0), -1.0, 100.0, 5.0);

    // Both legs are 5 worse off than the fee-free notional.
    EXPECT_NEAR(buy, -105.0, kThreshold);
    EXPECT_NEAR(sell, 95.0, kThreshold);
}

TEST(SettlementTest, RejectsMarginSettlement) {
    const InstrumentTradingSpec margin{
        50.0, SettlementConvention::MARGIN
    };

    EXPECT_THROW(
        settlementCashFlow(margin, 1.0, 100.0, 0.0),
        std::logic_error
    );
}

TEST(SettlementTest, RejectsInvalidArguments) {
    const auto spec = premiumSpec(1.0);

    EXPECT_THROW(
        settlementCashFlow(spec, 0.0, 100.0, 0.0),
        std::invalid_argument
    );
    EXPECT_THROW(
        settlementCashFlow(spec, kNaN, 100.0, 0.0),
        std::invalid_argument
    );
    EXPECT_THROW(
        settlementCashFlow(spec, 1.0, 0.0, 0.0),
        std::invalid_argument
    );
    EXPECT_THROW(
        settlementCashFlow(spec, 1.0, -100.0, 0.0),
        std::invalid_argument
    );
    EXPECT_THROW(
        settlementCashFlow(spec, 1.0, 100.0, -1.0),
        std::invalid_argument
    );
    EXPECT_THROW(
        settlementCashFlow(spec, 1.0, 100.0, kInf),
        std::invalid_argument
    );
}
