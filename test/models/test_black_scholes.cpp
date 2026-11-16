#include "pricing_engine/black_scholes_inputs.hpp"
#include "pricing_engine/black_scholes_pricer.hpp"
#include <gtest/gtest.h>
#include <cmath>

TEST(BlackScholesTest, AtTheMoneyCall) {
    const BlackScholesInputs inputs{100.0, 100.0, 1.0, 0.2, std::exp(-0.05), 0.0};

    const double price = BlackScholesPricer::price(OptionType::CALL, inputs);

    EXPECT_NEAR(price, 10.450583572185565, 1e-10);
}

TEST(BlackScholesTest, AtTheMoneyPut) {
    const BlackScholesInputs inputs{100.0, 100.0, 1.0, 0.2, std::exp(-0.05), 0.0};

    const double price = BlackScholesPricer::price(OptionType::PUT, inputs);

    EXPECT_NEAR(price, 5.573526022256971, 1e-10);
}

TEST(BlackScholesTest, PutCallParity) {
    const BlackScholesInputs inputs{100.0, 105.0, 2.0, 0.25, std::exp(-0.04 * 2.0), 0.02};

    const double call = BlackScholesPricer::price(OptionType::CALL, inputs);
    const double put = BlackScholesPricer::price(OptionType::PUT, inputs);
    const double expected = inputs.spot * std::exp(-inputs.dividend_yield * inputs.maturity)
        - inputs.strike * inputs.discount_factor;

    EXPECT_NEAR(call - put, expected, 1e-10);
}
