#include "valuation/account_valuator.hpp"

#include "volatility/flat_volatility_surface.hpp"
#include "yield_curve/flat_yield_curve.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <vector>

namespace {

constexpr double kThreshold = 1e-7;
constexpr double kInitialCash = 100'000.0;

constexpr auto kAapl = InstrumentIndex{0};
constexpr auto kMsft = InstrumentIndex{1};
constexpr auto kCall = InstrumentIndex{2};

const Date kValuationDate{
    std::chrono::sys_days{std::chrono::year{2024} / 6 / 1}
};

std::uint64_t nextOrderId() {
    static std::uint64_t next = 1;
    return next++;
}

Fill fill(
    InstrumentIndex slot,
    double quantity,
    double price,
    double fee = 0.0
) {
    return Fill{
        OrderId{nextOrderId()}, slot, quantity, price, fee, kValuationDate, 0
    };
}

} // namespace

class AccountValuationTest : public ::testing::Test {
protected:
    InstrumentRegistry registry;
    PricingEngine pricer;
    AccountValuator valuator;

    void SetUp() override {
        registry.registerInstrument(Equity{"AAPL", "AAPL"});
        registry.registerInstrument(Equity{"MSFT", "MSFT"});
        // Expires on the valuation date: priced at intrinsic value, which
        // keeps the expectations independent of the Black-Scholes model.
        registry.registerInstrument(EuropeanOption{
            "AAPL-CALL", "AAPL", 100.0,
            std::chrono::sys_days{std::chrono::year{2024} / 1 / 1},
            kValuationDate, OptionType::CALL, 25.0
        });
    }

    InstrumentUniverse makeUniverse() const {
        return InstrumentUniverse{
            {
                InstrumentId{"AAPL"},
                InstrumentId{"MSFT"},
                InstrumentId{"AAPL-CALL"}
            },
            registry
        };
    }

    MarketContext makeMarket(const MarketSnapshot& snapshot) const {
        return MarketContext{
            kValuationDate,
            snapshot,
            {},
            std::make_shared<FlatYieldCurve>(0.0),
            {}
        };
    }

    MarketSnapshot makeSnapshot(
        double aapl = 120.0,
        double msft = 220.0
    ) const {
        MarketSnapshot snapshot;
        snapshot.updatePrice("AAPL", aapl);
        snapshot.updatePrice("MSFT", msft);
        return snapshot;
    }

    // equity - initialCash must equal realized + unrealized - fees.
    static void expectConsistent(
        const AccountValuation& valuation,
        const Account& account
    ) {
        EXPECT_NEAR(
            valuation.equity,
            valuation.cash + valuation.position_market_value,
            kThreshold
        );
        EXPECT_NEAR(
            valuation.net_pnl,
            valuation.realized_pnl +
                valuation.unrealized_pnl -
                valuation.total_fees,
            kThreshold
        );
        EXPECT_NEAR(
            valuation.equity - account.initialCash(),
            valuation.net_pnl,
            kThreshold
        );
    }
};

TEST_F(AccountValuationTest, FlatAccountIsWorthItsCash) {
    const InstrumentUniverse universe = makeUniverse();
    const Account account = Account::forUniverse(kInitialCash, universe);

    const MarketSnapshot snapshot = makeSnapshot();
    const AccountValuation valuation = valuator.evaluate(
        account, universe, makeMarket(snapshot), pricer
    );

    EXPECT_NEAR(valuation.cash, kInitialCash, kThreshold);
    EXPECT_NEAR(valuation.position_market_value, 0.0, kThreshold);
    EXPECT_NEAR(valuation.equity, kInitialCash, kThreshold);
    EXPECT_NEAR(valuation.realized_pnl, 0.0, kThreshold);
    EXPECT_NEAR(valuation.unrealized_pnl, 0.0, kThreshold);
    EXPECT_NEAR(valuation.net_pnl, 0.0, kThreshold);
    expectConsistent(valuation, account);
}

TEST_F(AccountValuationTest, LongEquityIsMarkedToMarket) {
    const InstrumentUniverse universe = makeUniverse();
    Account account = Account::forUniverse(kInitialCash, universe);
    account.applyFill(fill(kAapl, 10.0, 100.0));

    const MarketSnapshot snapshot = makeSnapshot(120.0);
    const AccountValuation valuation = valuator.evaluate(
        account, universe, makeMarket(snapshot), pricer
    );

    EXPECT_NEAR(valuation.cash, kInitialCash - 1000.0, kThreshold);
    EXPECT_NEAR(valuation.position_market_value, 1200.0, kThreshold);
    EXPECT_NEAR(valuation.unrealized_pnl, 200.0, kThreshold);
    EXPECT_NEAR(valuation.equity, kInitialCash + 200.0, kThreshold);
    expectConsistent(valuation, account);
}

TEST_F(AccountValuationTest, ShortEquityContributesNegativeMarketValue) {
    const InstrumentUniverse universe = makeUniverse();
    Account account = Account::forUniverse(kInitialCash, universe);
    account.applyFill(fill(kAapl, -10.0, 130.0));

    const MarketSnapshot snapshot = makeSnapshot(120.0);
    const AccountValuation valuation = valuator.evaluate(
        account, universe, makeMarket(snapshot), pricer
    );

    EXPECT_NEAR(valuation.cash, kInitialCash + 1300.0, kThreshold);
    EXPECT_NEAR(valuation.position_market_value, -1200.0, kThreshold);
    // Sold at 130, now worth 120: a 100 gain on 10 shares.
    EXPECT_NEAR(valuation.unrealized_pnl, 100.0, kThreshold);
    EXPECT_NEAR(valuation.equity, kInitialCash + 100.0, kThreshold);
    expectConsistent(valuation, account);
}

TEST_F(AccountValuationTest, AggregatesLongAndShortAcrossInstruments) {
    const InstrumentUniverse universe = makeUniverse();
    Account account = Account::forUniverse(kInitialCash, universe);
    account.applyFill(fill(kAapl, 10.0, 100.0));
    account.applyFill(fill(kMsft, -5.0, 200.0));

    const MarketSnapshot snapshot = makeSnapshot(120.0, 220.0);
    const AccountValuation valuation = valuator.evaluate(
        account, universe, makeMarket(snapshot), pricer
    );

    // 10 AAPL at 120 = 1200, short 5 MSFT at 220 = -1100.
    EXPECT_NEAR(valuation.position_market_value, 100.0, kThreshold);
    // +200 on AAPL, -100 on MSFT.
    EXPECT_NEAR(valuation.unrealized_pnl, 100.0, kThreshold);
    expectConsistent(valuation, account);
}

TEST_F(AccountValuationTest, AppliesOptionContractMultiplierExactlyOnce) {
    const InstrumentUniverse universe = makeUniverse();
    Account account = Account::forUniverse(kInitialCash, universe);

    // 2 calls at premium 10 with multiplier 25 -> 500 paid.
    account.applyFill(fill(kCall, 2.0, 10.0));

    // Spot 120, strike 100, expiring today -> intrinsic 20 per unit.
    const MarketSnapshot snapshot = makeSnapshot(120.0);
    const AccountValuation valuation = valuator.evaluate(
        account, universe, makeMarket(snapshot), pricer
    );

    EXPECT_NEAR(valuation.cash, kInitialCash - 500.0, kThreshold);
    // 2 * 25 * 20
    EXPECT_NEAR(valuation.position_market_value, 1000.0, kThreshold);
    // 2 * 25 * (20 - 10)
    EXPECT_NEAR(valuation.unrealized_pnl, 500.0, kThreshold);
    expectConsistent(valuation, account);
}

TEST_F(AccountValuationTest, ClosedPositionReportsRealizedPnLOnly) {
    const InstrumentUniverse universe = makeUniverse();
    Account account = Account::forUniverse(kInitialCash, universe);
    account.applyFill(fill(kAapl, 10.0, 100.0));
    account.applyFill(fill(kAapl, -10.0, 130.0));

    const MarketSnapshot snapshot = makeSnapshot(120.0);
    const AccountValuation valuation = valuator.evaluate(
        account, universe, makeMarket(snapshot), pricer
    );

    EXPECT_NEAR(valuation.position_market_value, 0.0, kThreshold);
    EXPECT_NEAR(valuation.unrealized_pnl, 0.0, kThreshold);
    EXPECT_NEAR(valuation.realized_pnl, 300.0, kThreshold);
    EXPECT_NEAR(valuation.equity, kInitialCash + 300.0, kThreshold);
    expectConsistent(valuation, account);
}

TEST_F(AccountValuationTest, MixesRealizedAndUnrealizedWithFees) {
    const InstrumentUniverse universe = makeUniverse();
    Account account = Account::forUniverse(kInitialCash, universe);
    account.applyFill(fill(kAapl, 20.0, 100.0, 2.0));
    account.applyFill(fill(kAapl, -10.0, 130.0, 1.0));
    account.applyFill(fill(kMsft, 5.0, 200.0, 1.5));

    const MarketSnapshot snapshot = makeSnapshot(120.0, 220.0);
    const AccountValuation valuation = valuator.evaluate(
        account, universe, makeMarket(snapshot), pricer
    );

    EXPECT_NEAR(valuation.realized_pnl, 300.0, kThreshold);
    // 10 AAPL from 100 to 120 plus 5 MSFT from 200 to 220.
    EXPECT_NEAR(valuation.unrealized_pnl, 300.0, kThreshold);
    EXPECT_NEAR(valuation.total_fees, 4.5, kThreshold);
    EXPECT_NEAR(valuation.net_pnl, 595.5, kThreshold);
    expectConsistent(valuation, account);
}

TEST_F(AccountValuationTest, FeesAreNotCountedTwice) {
    const InstrumentUniverse universe = makeUniverse();

    Account with_fees = Account::forUniverse(kInitialCash, universe);
    with_fees.applyFill(fill(kAapl, 10.0, 100.0, 7.0));

    Account without_fees = Account::forUniverse(kInitialCash, universe);
    without_fees.applyFill(fill(kAapl, 10.0, 100.0, 0.0));

    const MarketSnapshot snapshot = makeSnapshot(120.0);
    const MarketContext market = makeMarket(snapshot);

    const AccountValuation charged =
        valuator.evaluate(with_fees, universe, market, pricer);
    const AccountValuation free =
        valuator.evaluate(without_fees, universe, market, pricer);

    // The fee hits equity exactly once, through cash.
    EXPECT_NEAR(charged.equity, free.equity - 7.0, kThreshold);
    EXPECT_NEAR(charged.unrealized_pnl, free.unrealized_pnl, kThreshold);
    EXPECT_NEAR(charged.net_pnl, free.net_pnl - 7.0, kThreshold);
}

TEST_F(AccountValuationTest, MissingMarketDataForAHeldPositionThrows) {
    const InstrumentUniverse universe = makeUniverse();
    Account account = Account::forUniverse(kInitialCash, universe);
    account.applyFill(fill(kMsft, 5.0, 200.0));

    MarketSnapshot snapshot;
    snapshot.updatePrice("AAPL", 120.0);

    EXPECT_THROW(
        valuator.evaluate(
            account, universe, makeMarket(snapshot), pricer
        ),
        std::out_of_range
    );
}

TEST_F(AccountValuationTest, MissingMarketDataForFlatSlotsIsIgnored) {
    const InstrumentUniverse universe = makeUniverse();
    Account account = Account::forUniverse(kInitialCash, universe);
    account.applyFill(fill(kAapl, 10.0, 100.0));

    // No MSFT price at all, but nothing is held there.
    MarketSnapshot snapshot;
    snapshot.updatePrice("AAPL", 120.0);

    const AccountValuation valuation = valuator.evaluate(
        account, universe, makeMarket(snapshot), pricer
    );

    EXPECT_NEAR(valuation.position_market_value, 1200.0, kThreshold);
    expectConsistent(valuation, account);
}

TEST_F(AccountValuationTest, RejectsMismatchedUniverse) {
    const InstrumentUniverse universe = makeUniverse();
    const Account account = Account::forUniverse(
        kInitialCash,
        InstrumentUniverse{{InstrumentId{"AAPL"}}, registry}
    );

    const MarketSnapshot snapshot = makeSnapshot();
    EXPECT_THROW(
        valuator.evaluate(
            account, universe, makeMarket(snapshot), pricer
        ),
        std::invalid_argument
    );
}

TEST_F(AccountValuationTest, PrecomputedPricesMatchDirectValuation) {
    const InstrumentUniverse universe = makeUniverse();
    Account account = Account::forUniverse(kInitialCash, universe);
    account.applyFill(fill(kAapl, 10.0, 100.0, 1.0));
    account.applyFill(fill(kCall, 2.0, 10.0));

    const MarketSnapshot snapshot = makeSnapshot(120.0);
    const MarketContext market = makeMarket(snapshot);

    std::vector<double> prices;
    valuator.priceUniverse(universe, market, pricer, prices);

    ASSERT_EQ(prices.size(), universe.size());
    EXPECT_NEAR(prices[0], 120.0, kThreshold);   // AAPL spot
    EXPECT_NEAR(prices[1], 220.0, kThreshold);   // MSFT spot
    EXPECT_NEAR(prices[2], 20.0, kThreshold);    // call intrinsic

    const AccountValuation direct =
        valuator.evaluate(account, universe, market, pricer);
    const AccountValuation cached =
        valuator.evaluateWithPrices(account, prices);

    EXPECT_NEAR(cached.equity, direct.equity, kThreshold);
    EXPECT_NEAR(
        cached.position_market_value,
        direct.position_market_value,
        kThreshold
    );
    EXPECT_NEAR(cached.unrealized_pnl, direct.unrealized_pnl, kThreshold);
    EXPECT_NEAR(cached.net_pnl, direct.net_pnl, kThreshold);
}

TEST_F(AccountValuationTest, RejectsMismatchedPriceVector) {
    const InstrumentUniverse universe = makeUniverse();
    const Account account = Account::forUniverse(kInitialCash, universe);

    const std::vector<double> too_short{120.0};
    EXPECT_THROW(
        valuator.evaluateWithPrices(account, too_short),
        std::invalid_argument
    );
}

TEST_F(AccountValuationTest, RejectsUnusablePriceForAHeldPosition) {
    const InstrumentUniverse universe = makeUniverse();
    Account account = Account::forUniverse(kInitialCash, universe);
    account.applyFill(fill(kAapl, 10.0, 100.0));

    const std::vector<double> prices{
        std::numeric_limits<double>::quiet_NaN(), 220.0, 20.0
    };

    EXPECT_THROW(
        valuator.evaluateWithPrices(account, prices),
        std::invalid_argument
    );
}
