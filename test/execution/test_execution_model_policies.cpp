// Composition tests for ExecutionModel<S, C>: that the two policies are
// actually called, in the right order, with the right arguments, and that
// what each contributes lands in the right field of the Fill.
//
// The regression tests for eligibility and timing live next door in
// test_execution_model.cpp and run against the default, frictionless
// composition. What is
// checked here is only what the policies add.
//
// Every expected number comes from the ticket's reference tables and was
// computed independently of this code.

#include "execution/execution_model.hpp"

#include "common/enums.hpp"
#include "execution/commission/no_commission.hpp"
#include "execution/commission/proportional_commission.hpp"
#include "execution/fill.hpp"
#include "execution/slippage/fixed_bps_slippage.hpp"
#include "execution/slippage/no_slippage.hpp"
#include "instrument/instrument.hpp"
#include "instrument/instrument_registry.hpp"
#include "instrument/instrument_universe.hpp"
#include "market_data/historical_market_data.hpp"
#include "order/order.hpp"
#include "order/pending_order_book.hpp"

#include <gtest/gtest.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace {

// The ticket's reference setup.
constexpr double kReferencePrice = 100.0;
constexpr double kQuantity = 100.0;
constexpr double kSlippageBps = 10.0;
constexpr double kCommissionBps = 2.0;

// Slot 0 is an equity (multiplier 1), slot 1 an option (multiplier 100),
// so the multiplier's effect on the fee is observable without touching the
// rest of the fixture.
constexpr auto kEquitySlot = InstrumentIndex{0};
constexpr auto kOptionSlot = InstrumentIndex{1};
constexpr double kOptionMultiplier = 100.0;

constexpr std::size_t kBarCount = 3;
constexpr std::size_t kSubmissionBar = 0;
constexpr std::size_t kExecutionBar = 1;

// Every open is exactly the reference price, so a fill price that differs
// from 100 can only have come from the slippage policy.
const HistoricalMarketData& marketData() {
    static const HistoricalMarketData data = [] {
        const auto rows = static_cast<Eigen::Index>(kBarCount);
        const Eigen::Index cols = 2;

        OHLCVData ohlcv;
        ohlcv.open = MarketMatrix::Constant(rows, cols, kReferencePrice);
        // Closes differ from the opens so that a model reaching for the
        // wrong one is visible rather than coincidentally right.
        ohlcv.close = MarketMatrix::Constant(rows, cols, 105.0);
        ohlcv.high = MarketMatrix::Constant(rows, cols, 110.0);
        ohlcv.low = MarketMatrix::Constant(rows, cols, 95.0);
        ohlcv.volume = MarketMatrix::Constant(rows, cols, 1'000.0);

        std::vector<Date> dates;
        dates.reserve(kBarCount);
        for (std::size_t bar = 0; bar < kBarCount; ++bar) {
            dates.push_back(
                std::chrono::sys_days{std::chrono::year{2024} / 1 / 1} +
                std::chrono::days{static_cast<int>(bar)}
            );
        }

        return HistoricalMarketData{
            dates, {"EQ", "OPT"}, ohlcv
        };
    }();

    return data;
}

const InstrumentUniverse& universe() {
    static const InstrumentRegistry registry = [] {
        InstrumentRegistry built;
        built.registerInstrument(Equity{"EQ", "EQ"});
        built.registerInstrument(EuropeanOption{
            "OPT",
            "EQ",
            100.0,
            std::chrono::sys_days{std::chrono::year{2024} / 1 / 1},
            std::chrono::sys_days{std::chrono::year{2025} / 1 / 1},
            OptionType::CALL,
            kOptionMultiplier
        });
        return built;
    }();

    static const InstrumentUniverse built =
        InstrumentUniverse::fromInstrumentIds(
            std::vector<std::string>{"EQ", "OPT"}, registry
        );

    return built;
}

PendingOrderBook bookWith(
    double quantity,
    InstrumentIndex slot = kEquitySlot,
    std::uint64_t id = 1,
    std::size_t submitted_bar = kSubmissionBar
) {
    PendingOrderBook book{universe()};
    book.submit(MarketOrder{OrderId{id}, slot, quantity}, submitted_bar);
    return book;
}

// The one fill a single-order book produces on the execution bar.
template <typename Model>
Fill onlyFill(const Model& model, const PendingOrderBook& book) {
    const std::vector<Fill> fills =
        model.execute(book, marketData(), universe(), kExecutionBar);
    EXPECT_EQ(fills.size(), 1u) << "expected exactly one fill";
    return fills.front();
}

// execute() is [[nodiscard]] and EXPECT_THROW evaluates its argument as a
// statement, so a call expected to throw must discard explicitly.
template <typename Model>
void run(const Model& model, const PendingOrderBook& book) {
    static_cast<void>(
        model.execute(book, marketData(), universe(), kExecutionBar)
    );
}

// ------------------------------------------------------ custom policies --
//
// Deliberately unrealistic test doubles, defined only here. Their whole
// point is that they are not production models and still compose: adding
// them required no change to ExecutionModel.

// +5 flat, so the adjustment is unmistakable in the fill price.
struct TestSlippage {
    double executionPrice(double reference_price, double) const {
        return reference_price + 5.0;
    }
};

// A tenth of whatever price it is handed. If it were handed the REFERENCE
// price instead of the executed one, the fee would come out at 10.0
// rather than 10.5 -- which is how this pins down the ordering.
struct TestCommission {
    double calculate(double execution_price, double, double) const {
        return execution_price * 0.1;
    }
};

using TestExecution = ExecutionModel<TestSlippage, TestCommission>;

// Policies that throw, to check that nothing swallows their errors.
struct ThrowingSlippage {
    double executionPrice(double, double) const {
        throw std::domain_error("slippage failed");
    }
};

struct ThrowingCommission {
    double calculate(double, double, double) const {
        throw std::domain_error("commission failed");
    }
};

} // namespace

// ================================================= compile-time architecture

// ValidPolicyCombinationsCompile. Every template argument is defaulted,
// so the frictionless composition is what ExecutionModel<> already is --
// the brackets are still required to name it.
static_assert(std::is_default_constructible_v<ExecutionModel<>>);
static_assert(std::same_as<
    ExecutionModel<>, ExecutionModel<NoSlippage, NoCommission>
>);

// A different pair of policies is a different type -- one implementation
// of the execution logic, four instantiations of it.
static_assert(!std::same_as<
    ExecutionModel<NoSlippage, NoCommission>,
    ExecutionModel<FixedBpsSlippage, ProportionalCommission>
>);
static_assert(!std::same_as<
    ExecutionModel<NoSlippage, NoCommission>,
    ExecutionModel<FixedBpsSlippage, NoCommission>
>);
static_assert(!std::same_as<
    ExecutionModel<NoSlippage, NoCommission>,
    ExecutionModel<NoSlippage, ProportionalCommission>
>);
static_assert(!std::same_as<
    ExecutionModel<FixedBpsSlippage, NoCommission>,
    ExecutionModel<NoSlippage, ProportionalCommission>
>);

// A configured policy is injected through the constructor, so a rate is a
// run-time value and no new instantiation is needed per rate.
static_assert(std::is_constructible_v<
    ExecutionModel<FixedBpsSlippage, ProportionalCommission>,
    FixedBpsSlippage, ProportionalCommission
>);
static_assert(std::is_constructible_v<
    ExecutionModel<FixedBpsSlippage, NoCommission>, FixedBpsSlippage
>);

// A stateful policy has no default, so neither does the composition.
static_assert(!std::is_default_constructible_v<
    ExecutionModel<FixedBpsSlippage, ProportionalCommission>
>);
static_assert(!std::is_default_constructible_v<
    ExecutionModel<FixedBpsSlippage, NoCommission>
>);

// InvalidPolicyCombinationsAreRejected. Naming ExecutionModel<S, C> is
// itself the check: the class template's constraints have to be satisfied
// for the type to exist.
template<typename S, typename C>
concept ValidExecutionCombination = requires {
    typename ExecutionModel<S, C>;
};

static_assert(ValidExecutionCombination<NoSlippage, NoCommission>);
static_assert(ValidExecutionCombination<FixedBpsSlippage,
                                        ProportionalCommission>);
static_assert(ValidExecutionCombination<TestSlippage, TestCommission>);

static_assert(!ValidExecutionCombination<int, NoCommission>);
static_assert(!ValidExecutionCombination<NoSlippage, double>);
static_assert(!ValidExecutionCombination<int, double>);
// The two policies are not interchangeable, even though both are policies
// of the same model: a slippage model in the commission slot is rejected.
static_assert(!ValidExecutionCombination<NoSlippage, NoSlippage>);
static_assert(!ValidExecutionCombination<NoCommission, NoCommission>);

// No assertion on sizeof: whether [[no_unique_address]] actually removes
// the storage is implementation-defined. The policies' own headers assert
// the precondition -- that the stateless ones stay empty.

// ========================================================= the four models ==
//
// Reference price 100.00, quantity +100, multiplier 1, 10 bps slippage,
// 2 bps commission.

TEST(ExecutionPoliciesTest, FrictionlessExecutionPreservesExistingBehaviour) {
    const ExecutionModel<> model;
    const Fill fill = onlyFill(model, bookWith(kQuantity));

    EXPECT_DOUBLE_EQ(fill.price(), 100.0);
    EXPECT_DOUBLE_EQ(fill.fee(), 0.0);
    EXPECT_DOUBLE_EQ(fill.quantity(), kQuantity);
}

TEST(ExecutionPoliciesTest, SlippageOnlyAdjustsPrice) {
    const ExecutionModel<FixedBpsSlippage, NoCommission> model{
        FixedBpsSlippage{kSlippageBps}
    };
    const Fill fill = onlyFill(model, bookWith(kQuantity));

    EXPECT_DOUBLE_EQ(fill.price(), 100.10);
    // Slippage is carried by the price alone -- never also as a fee.
    EXPECT_DOUBLE_EQ(fill.fee(), 0.0);
}

TEST(ExecutionPoliciesTest, CommissionOnlyAdjustsFees) {
    const ExecutionModel<NoSlippage, ProportionalCommission> model{
        NoSlippage{}, ProportionalCommission{kCommissionBps}
    };
    const Fill fill = onlyFill(model, bookWith(kQuantity));

    EXPECT_DOUBLE_EQ(fill.price(), 100.0);
    // 100 * 1 * 100.00 * 0.0002
    EXPECT_NEAR(fill.fee(), 2.0, 1e-12);
}

TEST(ExecutionPoliciesTest, RealisticBuyExecution) {
    const ExecutionModel<FixedBpsSlippage, ProportionalCommission>
        model{
        FixedBpsSlippage{kSlippageBps},
        ProportionalCommission{kCommissionBps}
    };
    const Fill fill = onlyFill(model, bookWith(kQuantity));

    EXPECT_DOUBLE_EQ(fill.price(), 100.10);
    // 100 * 1 * 100.10 * 0.0002
    EXPECT_NEAR(fill.fee(), 2.002, 1e-12);
}

TEST(ExecutionPoliciesTest, RealisticSellExecution) {
    const ExecutionModel<FixedBpsSlippage, ProportionalCommission>
        model{
        FixedBpsSlippage{kSlippageBps},
        ProportionalCommission{kCommissionBps}
    };
    const Fill fill = onlyFill(model, bookWith(-kQuantity));

    // Slippage is adverse in the other direction for a sell ...
    EXPECT_DOUBLE_EQ(fill.price(), 99.90);
    // ... while the fee is not: 100 * 1 * 99.90 * 0.0002.
    EXPECT_NEAR(fill.fee(), 1.998, 1e-12);
    EXPECT_DOUBLE_EQ(fill.quantity(), -kQuantity);
}

// Zero rates must reproduce the frictionless result exactly, not
// approximately -- this is the baseline a cost sweep starts from.
TEST(ExecutionPoliciesTest, ZeroRatesMatchFrictionlessExecution) {
    const ExecutionModel<FixedBpsSlippage, ProportionalCommission>
        charged{
        FixedBpsSlippage{0.0}, ProportionalCommission{0.0}
    };
    const ExecutionModel<> reference;

    for (const double quantity : {kQuantity, -kQuantity}) {
        const Fill with_zero_rates = onlyFill(charged, bookWith(quantity));
        const Fill frictionless = onlyFill(reference, bookWith(quantity));

        EXPECT_EQ(with_zero_rates.price(), frictionless.price())
            << "quantity " << quantity;
        EXPECT_EQ(with_zero_rates.fee(), frictionless.fee())
            << "quantity " << quantity;
    }
}

// The ordering requirement. The fee must be computed from the executed
// price, so with slippage on, it differs from the fee the same commission
// would charge on the reference price.
TEST(ExecutionPoliciesTest, CommissionUsesAdjustedExecutionPrice) {
    const ExecutionModel<FixedBpsSlippage, ProportionalCommission>
        with_slippage{
        FixedBpsSlippage{kSlippageBps},
        ProportionalCommission{kCommissionBps}
    };
    const ExecutionModel<NoSlippage, ProportionalCommission>
        without_slippage{
        NoSlippage{}, ProportionalCommission{kCommissionBps}
    };

    const double charged = onlyFill(with_slippage, bookWith(kQuantity)).fee();
    const double on_reference =
        onlyFill(without_slippage, bookWith(kQuantity)).fee();

    EXPECT_GT(charged, on_reference)
        << "the fee was charged on the reference price, not the executed one";
    // 2.002 against 2.000: exactly the slipped notional's share.
    EXPECT_NEAR(charged / on_reference, 100.10 / 100.0, 1e-12);
}

// The multiplier is what one contract trades, so it belongs in the fee's
// notional. Multiplier 100, quantity +2, 10 bps slippage:
// price 100.10, fee = 2 * 100 * 100.10 * 0.0002 = 4.004.
TEST(ExecutionPoliciesTest, CommissionRespectsContractMultiplier) {
    const ExecutionModel<FixedBpsSlippage, ProportionalCommission>
        model{
        FixedBpsSlippage{kSlippageBps},
        ProportionalCommission{kCommissionBps}
    };

    const Fill fill =
        onlyFill(model, bookWith(2.0, kOptionSlot));

    EXPECT_DOUBLE_EQ(fill.quantity(), 2.0);
    // The fill price stays a UNIT price: the multiplier belongs to the
    // notional and to settlement, never to the quoted price.
    EXPECT_DOUBLE_EQ(fill.price(), 100.10);
    EXPECT_NEAR(fill.fee(), 4.004, 1e-12);

    // The same trade on the multiplier-1 slot costs a hundredth as much.
    const Fill equity_fill = onlyFill(model, bookWith(2.0, kEquitySlot));
    EXPECT_NEAR(equity_fill.fee() * kOptionMultiplier, fill.fee(), 1e-12);
}

// A cost model changes what an execution costs, not how much of it
// happens. Neither policy may touch the quantity.
TEST(ExecutionPoliciesTest, FillQuantityRemainsUnchanged) {
    const ExecutionModel<FixedBpsSlippage, ProportionalCommission>
        model{
        FixedBpsSlippage{250.0}, ProportionalCommission{500.0}
    };

    for (const double quantity : {0.5, 1.0, 100.0, -0.5, -1.0, -100.0}) {
        const Fill fill = onlyFill(model, bookWith(quantity));
        EXPECT_EQ(fill.quantity(), quantity) << "quantity " << quantity;
    }
}

// ============================================================ custom policies

// The point of the whole design: a new pair of models composes without a
// line changing in ExecutionModel, and a closed variant could not do this
// without gaining alternatives.
TEST(ExecutionPoliciesTest, CustomPoliciesCanBeInjected) {
    const TestExecution model;
    const Fill fill = onlyFill(model, bookWith(kQuantity));

    // 100 + 5 from the slippage double ...
    EXPECT_DOUBLE_EQ(fill.price(), 105.0);
    // ... and a tenth of THAT, which is only 10.5 if the commission was
    // handed the executed price. On the reference price it would be 10.0.
    EXPECT_DOUBLE_EQ(fill.fee(), 10.5);
}

// ================================================== state and error handling

TEST(ExecutionPoliciesTest, ExecutionDoesNotModifyPendingOrders) {
    const ExecutionModel<FixedBpsSlippage, ProportionalCommission>
        model{
        FixedBpsSlippage{kSlippageBps},
        ProportionalCommission{kCommissionBps}
    };
    PendingOrderBook book = bookWith(kQuantity);

    static_cast<void>(
        model.execute(book, marketData(), universe(), kExecutionBar)
    );

    EXPECT_EQ(book.size(), 1u);
    EXPECT_EQ(book.get(OrderId{1}).status(), OrderStatus::PENDING);
    EXPECT_DOUBLE_EQ(book.pendingQuantity(kEquitySlot), kQuantity);
}

TEST(ExecutionPoliciesTest, RepeatedExecutionProducesIdenticalFills) {
    const ExecutionModel<FixedBpsSlippage, ProportionalCommission>
        model{
        FixedBpsSlippage{kSlippageBps},
        ProportionalCommission{kCommissionBps}
    };
    const PendingOrderBook book = bookWith(kQuantity);

    const Fill first = onlyFill(model, book);
    const Fill second = onlyFill(model, book);

    EXPECT_EQ(first.price(), second.price());
    EXPECT_EQ(first.fee(), second.fee());
    EXPECT_EQ(first.quantity(), second.quantity());
    EXPECT_EQ(first.orderId(), second.orderId());
}

// A policy that rejects its inputs must fail the bar, not be papered over
// with a frictionless fallback -- a backtest that silently dropped its own
// cost model would report an edge it does not have.
TEST(ExecutionPoliciesTest, SlippageExceptionPropagates) {
    const ExecutionModel<ThrowingSlippage, NoCommission> model;

    EXPECT_THROW(run(model, bookWith(kQuantity)), std::domain_error);
}

TEST(ExecutionPoliciesTest, CommissionExceptionPropagates) {
    const ExecutionModel<NoSlippage, ThrowingCommission> model;

    EXPECT_THROW(run(model, bookWith(kQuantity)), std::domain_error);
}

// Multiple orders keep submission order whichever policies are composed,
// so a run stays reproducible.
TEST(ExecutionPoliciesTest, MultipleOrdersPreserveExecutionOrder) {
    const ExecutionModel<FixedBpsSlippage, ProportionalCommission>
        model{
        FixedBpsSlippage{kSlippageBps},
        ProportionalCommission{kCommissionBps}
    };

    PendingOrderBook book{universe()};
    book.submit(MarketOrder{OrderId{7}, kEquitySlot, 10.0}, kSubmissionBar);
    book.submit(MarketOrder{OrderId{3}, kOptionSlot, 2.0}, kSubmissionBar);

    const std::vector<Fill> fills =
        model.execute(book, marketData(), universe(), kExecutionBar);

    ASSERT_EQ(fills.size(), 2u);
    EXPECT_EQ(fills[0].orderId(), OrderId{7});
    EXPECT_EQ(fills[1].orderId(), OrderId{3});

    // And each is priced and charged from its own slot's multiplier.
    EXPECT_NEAR(fills[0].fee(), 10.0 * 1.0 * 100.10 * 0.0002, 1e-12);
    EXPECT_NEAR(fills[1].fee(), 2.0 * 100.0 * 100.10 * 0.0002, 1e-12);
}
