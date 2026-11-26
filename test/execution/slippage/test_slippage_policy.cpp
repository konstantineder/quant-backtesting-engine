#include "execution/slippage/fixed_bps_slippage.hpp"
#include "execution/slippage/no_slippage.hpp"
#include "execution/slippage/slippage_policy.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace {

constexpr double kBuy = 100.0;
constexpr double kSell = -100.0;
constexpr double kPrice = 200.0;

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

// ------------------------------------------- near misses on the contract --
//
// The concept earns its keep only if it also REJECTS things. Each of these
// is wrong in exactly one way, so a contract loosened by accident shows up
// as one of the negative assertions below flipping.

// Returns the wrong type.
struct FloatReturning {
    float executionPrice(double, double) const { return 0.0f; }
};

// Cannot be called on a const model, which is how ExecutionModel holds it.
struct NonConst {
    double executionPrice(double, double) { return 0.0; }
};

// Wrong arity: no direction to apply the adjustment against.
struct PriceOnly {
    double executionPrice(double price) const { return price; }
};

// Right shape, wrong name.
struct Misnamed {
    double execution_price(double, double) const { return 0.0; }
};

// executionPrice is [[nodiscard]] and EXPECT_THROW evaluates its argument
// as a statement, so a call expected to throw has to discard the result
// explicitly. There is none to read when it does throw.
template <SlippagePolicy Policy>
void price(const Policy& model, double reference, double quantity) {
    static_cast<void>(model.executionPrice(reference, quantity));
}

} // namespace

// ================================================== compile-time contract ==

static_assert(SlippagePolicy<NoSlippage>);
static_assert(SlippagePolicy<FixedBpsSlippage>);

static_assert(!SlippagePolicy<int>);
static_assert(!SlippagePolicy<double>);
static_assert(!SlippagePolicy<FloatReturning>);
static_assert(!SlippagePolicy<NonConst>);
static_assert(!SlippagePolicy<PriceOnly>);
static_assert(!SlippagePolicy<Misnamed>);

// Keep the frictionless policy empty so that [[no_unique_address]] on
// ExecutionModel's member CAN eliminate its storage. The attribute permits
// the saving; it does not guarantee it, and the layout stays
// compiler-dependent. What this assertion protects is the precondition:
// give NoSlippage a data member and the saving is off the table.
static_assert(std::is_empty_v<NoSlippage>);

// ================================================================ NoSlippage

TEST(NoSlippageTest, ReturnsTheReferencePriceUnchanged) {
    const NoSlippage model;

    // Exact, not within a few ULPs: no arithmetic is supposed to happen
    // at all, so EXPECT_DOUBLE_EQ would accept a transformation this
    // model must not be making.
    EXPECT_EQ(model.executionPrice(kPrice, kBuy), kPrice);
    EXPECT_EQ(model.executionPrice(kPrice, kSell), kPrice);
}

TEST(NoSlippageTest, IsIndifferentToSizeAndDirection) {
    const NoSlippage model;

    for (const double quantity : {1e-9, 1.0, 1e9, -1e-9, -1.0, -1e9}) {
        EXPECT_EQ(model.executionPrice(kPrice, quantity), kPrice)
            << "quantity " << quantity;
    }
}

// =========================================================== FixedBpsSlippage

TEST(FixedBpsSlippageTest, ABuyPaysMoreAndASellReceivesLess) {
    const FixedBpsSlippage model{50.0};  // 0.5%

    EXPECT_DOUBLE_EQ(model.executionPrice(kPrice, kBuy), 201.0);
    EXPECT_DOUBLE_EQ(model.executionPrice(kPrice, kSell), 199.0);
}

// Slippage is a cost in both directions: the adverse side is never the
// cheaper one. A sign error would make one direction profitable.
TEST(FixedBpsSlippageTest, IsAdverseInBothDirections) {
    const FixedBpsSlippage model{25.0};

    EXPECT_GT(model.executionPrice(kPrice, kBuy), kPrice);
    EXPECT_LT(model.executionPrice(kPrice, kSell), kPrice);
}

// The adjustment is symmetric around the reference price.
TEST(FixedBpsSlippageTest, DeviatesEquallyOnBothSides) {
    const FixedBpsSlippage model{37.5};

    const double buy = model.executionPrice(kPrice, kBuy);
    const double sell = model.executionPrice(kPrice, kSell);

    EXPECT_NEAR(buy - kPrice, kPrice - sell, 1e-12);
    EXPECT_NEAR((buy + sell) / 2.0, kPrice, 1e-12);
}

// Proportional, not absolute: one rate must describe a cheap and an
// expensive instrument without rescaling.
TEST(FixedBpsSlippageTest, ScalesWithTheReferencePrice) {
    const FixedBpsSlippage model{100.0};  // 1%

    EXPECT_DOUBLE_EQ(model.executionPrice(5.0, kBuy), 5.05);
    EXPECT_DOUBLE_EQ(model.executionPrice(5'000.0, kBuy), 5'050.0);

    // The relative cost is the same in both cases.
    EXPECT_NEAR(
        model.executionPrice(5.0, kBuy) / 5.0,
        model.executionPrice(5'000.0, kBuy) / 5'000.0,
        1e-15
    );
}

// Only the sign of the quantity matters, never its size -- this is a
// fixed-rate model, so a block trade pays the same rate as an odd lot.
TEST(FixedBpsSlippageTest, DependsOnTheSignOfTheQuantityOnly) {
    const FixedBpsSlippage model{10.0};

    // Exact: the size does not enter the computation, so these are the
    // identical multiplication and must agree bit for bit.
    const double buy = model.executionPrice(kPrice, 1.0);
    for (const double quantity : {1e-9, 1.0, 1e6, 1e9}) {
        EXPECT_EQ(model.executionPrice(kPrice, quantity), buy)
            << "quantity " << quantity;
    }

    const double sell = model.executionPrice(kPrice, -1.0);
    for (const double quantity : {-1e-9, -1.0, -1e6, -1e9}) {
        EXPECT_EQ(model.executionPrice(kPrice, quantity), sell)
            << "quantity " << quantity;
    }

    EXPECT_NE(buy, sell);
}

// Zero bps is the baseline a parameter sweep starts from, and it must
// agree with NoSlippage exactly rather than approximately.
TEST(FixedBpsSlippageTest, ZeroBpsMatchesNoSlippage) {
    const FixedBpsSlippage model{0.0};
    const NoSlippage reference;

    // EXPECT_EQ, not EXPECT_DOUBLE_EQ: the claim is that the baseline is
    // the frictionless model, not that it is within four ULPs of it.
    for (const double quantity : {kBuy, kSell}) {
        EXPECT_EQ(
            model.executionPrice(kPrice, quantity),
            reference.executionPrice(kPrice, quantity)
        ) << "quantity " << quantity;
    }
}

TEST(FixedBpsSlippageTest, ExposesItsRate) {
    EXPECT_DOUBLE_EQ(FixedBpsSlippage{12.5}.bps(), 12.5);
    EXPECT_DOUBLE_EQ(FixedBpsSlippage{0.0}.bps(), 0.0);
}

TEST(FixedBpsSlippageTest, ComputesKnownRatesCorrectly) {
    struct Case {
        double bps;
        double price;
        double expected_buy;
        double expected_sell;
    };

    const std::vector<Case> cases{
        {1.0, 100.0, 100.01, 99.99},
        {10.0, 100.0, 100.1, 99.9},
        {100.0, 100.0, 101.0, 99.0},
        {5'000.0, 100.0, 150.0, 50.0},
        {2.5, 40.0, 40.01, 39.99}
    };

    for (const Case& test_case : cases) {
        const FixedBpsSlippage model{test_case.bps};

        EXPECT_NEAR(
            model.executionPrice(test_case.price, kBuy),
            test_case.expected_buy,
            1e-9
        ) << test_case.bps << " bps, buy";

        EXPECT_NEAR(
            model.executionPrice(test_case.price, kSell),
            test_case.expected_sell,
            1e-9
        ) << test_case.bps << " bps, sell";
    }
}

// ------------------------------------------------------ rate validation --

TEST(FixedBpsSlippageTest, RejectsANonFiniteOrNegativeRate) {
    EXPECT_THROW(FixedBpsSlippage{-1.0}, std::invalid_argument);
    EXPECT_THROW(FixedBpsSlippage{-0.001}, std::invalid_argument);
    EXPECT_THROW(FixedBpsSlippage{kNaN}, std::invalid_argument);
    EXPECT_THROW(FixedBpsSlippage{kInf}, std::invalid_argument);
    EXPECT_THROW(FixedBpsSlippage{-kInf}, std::invalid_argument);
}

// At 10'000 bps a sell executes at exactly zero, and beyond it at a
// negative price. The rate is the error, so it is caught here.
TEST(FixedBpsSlippageTest, RejectsARateThatWouldZeroOutASellPrice) {
    EXPECT_THROW(FixedBpsSlippage{10'000.0}, std::invalid_argument);
    EXPECT_THROW(FixedBpsSlippage{20'000.0}, std::invalid_argument);

    // The bound is exclusive, so the largest representable rate below it
    // is still a valid configuration. nextafter pins the actual boundary
    // rather than a round number near it.
    const double max_valid_bps = std::nextafter(10'000.0, 0.0);
    EXPECT_LT(max_valid_bps, 10'000.0);
    EXPECT_NO_THROW(FixedBpsSlippage{max_valid_bps});
    EXPECT_NO_THROW(FixedBpsSlippage{9'999.0});
}

// ----------------------------------------------------- input validation --

// Both models must accept and reject exactly the same inputs: swapping a
// frictionless model for a realistic one may change prices, never which
// quotes a backtest tolerates.
template <SlippagePolicy Policy>
void expectSharedInputValidation(const Policy& model) {
    // Prices. Negative zero compares equal to zero, so it has to be
    // rejected by the same `<= 0.0` test rather than slip through as a
    // "positive" price that prices every trade at nothing.
    EXPECT_THROW(price(model, 0.0, kBuy), std::invalid_argument);
    EXPECT_THROW(price(model, -0.0, kBuy), std::invalid_argument);
    EXPECT_THROW(price(model, -1.0, kBuy), std::invalid_argument);
    EXPECT_THROW(price(model, kNaN, kBuy), std::invalid_argument);
    EXPECT_THROW(price(model, kInf, kBuy), std::invalid_argument);
    EXPECT_THROW(price(model, -kInf, kBuy), std::invalid_argument);

    // Quantities. Negative zero again: std::signbit would read it as a
    // sell, so a model that only checked the sign and not the magnitude
    // would silently apply a direction to an order that has none.
    EXPECT_THROW(price(model, kPrice, 0.0), std::invalid_argument);
    EXPECT_THROW(price(model, kPrice, -0.0), std::invalid_argument);
    EXPECT_THROW(price(model, kPrice, kNaN), std::invalid_argument);
    EXPECT_THROW(price(model, kPrice, kInf), std::invalid_argument);
    EXPECT_THROW(price(model, kPrice, -kInf), std::invalid_argument);

    EXPECT_NO_THROW(price(model, kPrice, kBuy));
}

TEST(SlippageValidationTest, NoSlippageValidatesItsInputs) {
    expectSharedInputValidation(NoSlippage{});
}

TEST(SlippageValidationTest, FixedBpsSlippageValidatesItsInputs) {
    expectSharedInputValidation(FixedBpsSlippage{10.0});
}

// ------------------------------------------------- numeric edge handling --

// A buy adjustment multiplies the price up, so an extreme reference price
// can overflow. That must not travel on as an infinite Fill price.
TEST(SlippageValidationTest, RejectsAnOverflowingBuyPrice) {
    const FixedBpsSlippage model{5'000.0};  // x1.5

    EXPECT_THROW(
        price(model, std::numeric_limits<double>::max(), kBuy),
        std::overflow_error
    );

    // The same price on the sell side shrinks, so it stays representable.
    EXPECT_NO_THROW(
        price(model, std::numeric_limits<double>::max(), kSell)
    );
}

// The mirror image: a sell adjustment multiplies the price down, so a
// subnormal reference price can underflow to zero, which is not a
// tradable price either.
//
// 7'500 bps rather than 5'000 on purpose. A factor of 0.5 puts
// denorm_min exactly on the midpoint between zero and the smallest
// subnormal, so whether it reaches zero depends on the rounding mode;
// ties-to-even happens to give zero, but the test would then be asserting
// the mode rather than the model. A factor of 0.25 lands clear of any tie.
//
// Still assumed: round-to-nearest and hardware support for subnormals.
// Both hold everywhere this engine is built, and neither is something the
// model could defend itself against anyway.
TEST(SlippageValidationTest, RejectsAnUnderflowingSellPrice) {
    const FixedBpsSlippage model{7'500.0};  // x0.25

    EXPECT_THROW(
        price(model, std::numeric_limits<double>::denorm_min(), kSell),
        std::overflow_error
    );
}

// NoSlippage cannot overflow -- it returns what it was given -- so the
// extremes that break the proportional model are fine here.
TEST(SlippageValidationTest, NoSlippageHandlesTheNumericExtremes) {
    const NoSlippage model;

    EXPECT_EQ(
        model.executionPrice(std::numeric_limits<double>::max(), kBuy),
        std::numeric_limits<double>::max()
    );
    EXPECT_EQ(
        model.executionPrice(
            std::numeric_limits<double>::denorm_min(), kSell
        ),
        std::numeric_limits<double>::denorm_min()
    );
}
