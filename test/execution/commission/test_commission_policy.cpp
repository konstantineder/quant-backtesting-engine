#include "execution/commission/commission_policy.hpp"
#include "execution/commission/no_commission.hpp"
#include "execution/commission/proportional_commission.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace {

constexpr double kPrice = 100.0;
constexpr double kBuy = 50.0;
constexpr double kSell = -50.0;
constexpr double kMultiplier = 1.0;

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kMax = std::numeric_limits<double>::max();
constexpr double kDenormMin = std::numeric_limits<double>::denorm_min();

// ------------------------------------------- near misses on the contract --
//
// The concept earns its keep only if it also REJECTS things. Each of these
// is wrong in exactly one way, so a contract loosened by accident shows up
// as one of the negative assertions below flipping.

// Returns the wrong type.
struct FloatReturning {
    float calculate(double, double, double) const { return 0.0f; }
};

// Cannot be called on a const model, which is how ExecutionModel holds it.
struct NonConst {
    double calculate(double, double, double) { return 0.0; }
};

// Wrong arity: no multiplier, so it cannot see the real notional.
struct NoMultiplier {
    double calculate(double, double) const { return 0.0; }
};

// Right shape, wrong name.
struct Misnamed {
    double commission(double, double, double) const { return 0.0; }
};

// A slippage model is not a commission model, even though both are
// policies of the same ExecutionModel. Mixing the two up at the call site
// is exactly the mistake the two separate concepts prevent.
struct SlippageShaped {
    double executionPrice(double, double) const { return 0.0; }
};

// calculate is [[nodiscard]] and EXPECT_THROW evaluates its argument as a
// statement, so a call expected to throw has to discard the result
// explicitly. There is none to read when it does throw.
template <CommissionPolicy Policy>
void fee(
    const Policy& model,
    double price,
    double quantity,
    double multiplier
) {
    static_cast<void>(model.calculate(price, quantity, multiplier));
}

} // namespace

// ================================================== compile-time contract ==

// BothPoliciesSatisfyConcept
static_assert(CommissionPolicy<NoCommission>);
static_assert(CommissionPolicy<ProportionalCommission>);

// InvalidPoliciesDoNotSatisfyConcept
static_assert(!CommissionPolicy<int>);
static_assert(!CommissionPolicy<double>);
static_assert(!CommissionPolicy<FloatReturning>);
static_assert(!CommissionPolicy<NonConst>);
static_assert(!CommissionPolicy<NoMultiplier>);
static_assert(!CommissionPolicy<Misnamed>);
static_assert(!CommissionPolicy<SlippageShaped>);

// NoCommissionIsEmpty -- keep the fee-free policy empty so that
// [[no_unique_address]] on ExecutionModel's member CAN eliminate its
// storage. The attribute permits the saving; it does not guarantee it, and
// the layout stays compiler-dependent. What this assertion protects is the
// precondition: give NoCommission a data member and the saving is off.
static_assert(std::is_empty_v<NoCommission>);

// ============================================================= NoCommission

TEST(NoCommissionTest, ReturnsZeroForEveryValidExecution) {
    const NoCommission model;

    // Exact, not within a few ULPs: nothing is supposed to be computed.
    EXPECT_EQ(model.calculate(kPrice, kBuy, kMultiplier), 0.0);
    EXPECT_EQ(model.calculate(kPrice, kSell, kMultiplier), 0.0);
    EXPECT_EQ(model.calculate(0.01, 1e9, 1'000.0), 0.0);
    EXPECT_EQ(model.calculate(1e6, 0.001, 0.5), 0.0);
}

// ==================================================== ProportionalCommission

// The reference table, each value computed independently of the code.
TEST(ProportionalCommissionTest, ComputesKnownCommissionRates) {
    struct Case {
        double price;
        double quantity;
        double multiplier;
        double bps;
        double expected;
    };

    const std::vector<Case> cases{
        {100.0, 50.0, 1.0, 10.0, 5.0},
        {100.0, -50.0, 1.0, 10.0, 5.0},
        {200.0, 50.0, 1.0, 10.0, 10.0},
        {100.0, 100.0, 1.0, 10.0, 10.0},
        {100.0, 50.0, 100.0, 10.0, 500.0},
        {200.0, 0.5, 1.0, 10.0, 0.1},
        {100.0, 50.0, 1.0, 2.5, 1.25},
        {100.0, 50.0, 1.0, 0.0, 0.0}
    };

    for (const Case& test_case : cases) {
        const ProportionalCommission model{test_case.bps};

        EXPECT_NEAR(
            model.calculate(
                test_case.price, test_case.quantity, test_case.multiplier
            ),
            test_case.expected,
            1e-9
        ) << test_case.bps << " bps on " << test_case.quantity
          << " @ " << test_case.price << " x" << test_case.multiplier;
    }
}

// A fee is never a credit, and the two sides of the same trade cost the
// same. This is the one place commission differs from slippage, which is
// adverse in opposite directions.
TEST(ProportionalCommissionTest, IsIndependentOfTradeDirection) {
    const ProportionalCommission model{7.5};

    EXPECT_EQ(
        model.calculate(kPrice, kBuy, kMultiplier),
        model.calculate(kPrice, -kBuy, kMultiplier)
    );
    EXPECT_GT(model.calculate(kPrice, kSell, kMultiplier), 0.0);
}

TEST(ProportionalCommissionTest, ScalesWithExecutionPrice) {
    const ProportionalCommission model{10.0};

    const double base = model.calculate(kPrice, kBuy, kMultiplier);

    EXPECT_NEAR(model.calculate(2.0 * kPrice, kBuy, kMultiplier), 2.0 * base, 1e-9);
    EXPECT_NEAR(model.calculate(0.5 * kPrice, kBuy, kMultiplier), 0.5 * base, 1e-9);
}

TEST(ProportionalCommissionTest, ScalesWithQuantityMagnitude) {
    const ProportionalCommission model{10.0};

    const double base = model.calculate(kPrice, kBuy, kMultiplier);

    EXPECT_NEAR(model.calculate(kPrice, 2.0 * kBuy, kMultiplier), 2.0 * base, 1e-9);
    EXPECT_NEAR(model.calculate(kPrice, 0.5 * kBuy, kMultiplier), 0.5 * base, 1e-9);
    // Magnitude, not sign.
    EXPECT_NEAR(model.calculate(kPrice, -2.0 * kBuy, kMultiplier), 2.0 * base, 1e-9);
}

// The multiplier is what one contract actually trades, so it belongs in
// the notional. A model that ignored it would under-charge every
// derivative by the multiplier.
TEST(ProportionalCommissionTest, ScalesWithContractMultiplier) {
    const ProportionalCommission model{10.0};

    const double base = model.calculate(kPrice, kBuy, 1.0);

    EXPECT_NEAR(model.calculate(kPrice, kBuy, 100.0), 100.0 * base, 1e-9);
    EXPECT_NEAR(model.calculate(kPrice, kBuy, 0.5), 0.5 * base, 1e-9);
}

TEST(ProportionalCommissionTest, SupportsFractionalQuantities) {
    const ProportionalCommission model{10.0};

    EXPECT_NEAR(model.calculate(200.0, 0.5, 1.0), 0.1, 1e-12);
    EXPECT_NEAR(model.calculate(100.0, 0.001, 1.0), 1e-4, 1e-15);

    // Fractional quantities compose with the multiplier.
    EXPECT_NEAR(model.calculate(100.0, 0.25, 4.0), 0.1, 1e-12);
}

// Zero bps is the baseline a parameter sweep starts from, and it must
// agree with NoCommission exactly rather than approximately.
TEST(ProportionalCommissionTest, ZeroBpsMatchesNoCommission) {
    const ProportionalCommission model{0.0};
    const NoCommission reference;

    for (const double quantity : {kBuy, kSell}) {
        EXPECT_EQ(
            model.calculate(kPrice, quantity, kMultiplier),
            reference.calculate(kPrice, quantity, kMultiplier)
        ) << "quantity " << quantity;
    }
}

TEST(ProportionalCommissionTest, ExposesItsRate) {
    EXPECT_EQ(ProportionalCommission{12.5}.bps(), 12.5);
    EXPECT_EQ(ProportionalCommission{0.0}.bps(), 0.0);

    // No upper bound: charging more than the notional is economically odd
    // but mathematically fine, and not this layer's business to forbid.
    EXPECT_NO_THROW(ProportionalCommission{50'000.0});
}

// ======================================================= input validation ==

TEST(ProportionalCommissionTest, RejectsInvalidCommissionRate) {
    EXPECT_THROW(ProportionalCommission{-1.0}, std::invalid_argument);
    EXPECT_THROW(ProportionalCommission{-0.001}, std::invalid_argument);
    EXPECT_THROW(ProportionalCommission{kNaN}, std::invalid_argument);
    EXPECT_THROW(ProportionalCommission{kInf}, std::invalid_argument);
    EXPECT_THROW(ProportionalCommission{-kInf}, std::invalid_argument);
}

// Both models must accept and reject exactly the same inputs: adding a fee
// model to a backtest may change costs, never which executions it
// tolerates.
template <CommissionPolicy Policy>
void expectSharedInputValidation(const Policy& model) {
    // RejectsInvalidExecutionPrice. Negative zero compares equal to zero,
    // so the same `<= 0.0` test has to catch it.
    EXPECT_THROW(fee(model, 0.0, kBuy, kMultiplier), std::invalid_argument);
    EXPECT_THROW(fee(model, -0.0, kBuy, kMultiplier), std::invalid_argument);
    EXPECT_THROW(fee(model, -1.0, kBuy, kMultiplier), std::invalid_argument);
    EXPECT_THROW(fee(model, kNaN, kBuy, kMultiplier), std::invalid_argument);
    EXPECT_THROW(fee(model, kInf, kBuy, kMultiplier), std::invalid_argument);
    EXPECT_THROW(fee(model, -kInf, kBuy, kMultiplier), std::invalid_argument);

    // RejectsInvalidQuantity. A zero quantity is no trade at all, so there
    // is nothing to charge for -- not even nothing.
    EXPECT_THROW(fee(model, kPrice, 0.0, kMultiplier), std::invalid_argument);
    EXPECT_THROW(fee(model, kPrice, -0.0, kMultiplier), std::invalid_argument);
    EXPECT_THROW(fee(model, kPrice, kNaN, kMultiplier), std::invalid_argument);
    EXPECT_THROW(fee(model, kPrice, kInf, kMultiplier), std::invalid_argument);
    EXPECT_THROW(fee(model, kPrice, -kInf, kMultiplier), std::invalid_argument);

    // RejectsInvalidContractMultiplier. Zero would make every notional
    // vanish; a negative one would flip the sign of the fee.
    EXPECT_THROW(fee(model, kPrice, kBuy, 0.0), std::invalid_argument);
    EXPECT_THROW(fee(model, kPrice, kBuy, -0.0), std::invalid_argument);
    EXPECT_THROW(fee(model, kPrice, kBuy, -1.0), std::invalid_argument);
    EXPECT_THROW(fee(model, kPrice, kBuy, kNaN), std::invalid_argument);
    EXPECT_THROW(fee(model, kPrice, kBuy, kInf), std::invalid_argument);
    EXPECT_THROW(fee(model, kPrice, kBuy, -kInf), std::invalid_argument);

    EXPECT_NO_THROW(fee(model, kPrice, kBuy, kMultiplier));
}

TEST(CommissionValidationTest, NoCommissionValidatesItsInputs) {
    expectSharedInputValidation(NoCommission{});
}

TEST(CommissionValidationTest, ProportionalCommissionValidatesItsInputs) {
    expectSharedInputValidation(ProportionalCommission{10.0});
}

// A zero rate must not become a shortcut past validation: the inputs are
// still wrong, whatever the fee would have been.
TEST(CommissionValidationTest, BothPoliciesShareInputValidation) {
    expectSharedInputValidation(ProportionalCommission{0.0});
}

// ==================================================== numerical edge cases ==

// The notional is formed before the rate is applied, so an execution large
// enough to overflow it is rejected rather than reported as an infinite
// fee that would travel on into a Fill.
TEST(CommissionValidationTest, RejectsOverflowingCommission) {
    const ProportionalCommission model{10'000.0};  // rate 1.0

    EXPECT_THROW(fee(model, kMax, kMax, 1.0), std::overflow_error);
    EXPECT_THROW(fee(model, kMax, 1.0, kMax), std::overflow_error);
}

// The case the zero-rate short circuit exists for. Computing it would form
// the notional first, overflow to infinity, and then multiply by zero --
// which is NaN, not zero. A fee-free configuration would fail on inputs a
// charged one handles.
TEST(CommissionValidationTest, ZeroRateHandlesNumericExtremes) {
    const ProportionalCommission model{0.0};

    EXPECT_EQ(model.calculate(kMax, kMax, kMax), 0.0);
    EXPECT_EQ(model.calculate(kMax, -kMax, 1.0), 0.0);
    EXPECT_EQ(model.calculate(kDenormMin, kDenormMin, kDenormMin), 0.0);
}

TEST(CommissionValidationTest, NoCommissionHandlesNumericExtremes) {
    const NoCommission model;

    EXPECT_EQ(model.calculate(kMax, kMax, kMax), 0.0);
    EXPECT_EQ(model.calculate(kDenormMin, -kDenormMin, kDenormMin), 0.0);
}

// Unlike an execution price, a fee of zero is a valid answer: a notional
// small enough simply rounds the charge away. This must not be mistaken
// for the overflow case and rejected.
TEST(CommissionValidationTest, UnderflowToZeroIsAllowed) {
    const ProportionalCommission model{1.0};

    double underflowed = -1.0;
    EXPECT_NO_THROW(
        underflowed = model.calculate(kDenormMin, kDenormMin, 1.0)
    );
    EXPECT_EQ(underflowed, 0.0);

    // Not an artefact of the extreme: a merely very small fee survives.
    EXPECT_GT(model.calculate(1e-10, 1e-10, 1.0), 0.0);
}

// Determinism: the same inputs give the same answer, and no call leaves a
// trace in the model.
TEST(CommissionValidationTest, IsDeterministicAndStateless) {
    const ProportionalCommission model{3.5};

    const double first = model.calculate(kPrice, kBuy, kMultiplier);
    static_cast<void>(model.calculate(1e6, -42.0, 100.0));
    const double second = model.calculate(kPrice, kBuy, kMultiplier);

    EXPECT_EQ(first, second);
    EXPECT_EQ(model.bps(), 3.5);
}
