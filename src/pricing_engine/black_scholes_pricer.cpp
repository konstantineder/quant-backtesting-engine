#include "pricing_engine/black_scholes_pricer.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace {

void validate(OptionType type, const BlackScholesInputs& inputs) {
    if (!std::isfinite(inputs.spot) || inputs.spot <= 0.0) {
        throw std::invalid_argument("Spot must be finite and positive.");
    }
    if (!std::isfinite(inputs.strike) || inputs.strike <= 0.0) {
        throw std::invalid_argument("Strike must be finite and positive.");
    }
    if (!std::isfinite(inputs.maturity) || inputs.maturity < 0.0) {
        throw std::invalid_argument("Maturity must be finite and non-negative.");
    }
    if (!std::isfinite(inputs.volatility) || inputs.volatility <= 0.0) {
        throw std::invalid_argument("Volatility must be finite and positive.");
    }
    if (!std::isfinite(inputs.discount_factor) || inputs.discount_factor <= 0.0) {
        throw std::invalid_argument("Discount factor must be finite and positive.");
    }
    if (!std::isfinite(inputs.dividend_yield)) {
        throw std::invalid_argument("Dividend yield must be finite.");
    }
    if (type != OptionType::CALL && type != OptionType::PUT) {
        throw std::invalid_argument("Option type must be CALL or PUT.");
    }
}

double normalCdf(double value) {
    return 0.5 * std::erfc(-value / std::numbers::sqrt2);
}

} // namespace

double BlackScholesPricer::price(OptionType type, const BlackScholesInputs& inputs) {
    validate(type, inputs);

    if (inputs.maturity == 0.0) {
        constexpr double discount_tolerance = 1e-12;
        if (std::abs(inputs.discount_factor - 1.0) > discount_tolerance) {
            throw std::invalid_argument("Discount factor at expiry must be 1.0.");
        }
        const double intrinsic = type == OptionType::CALL
            ? inputs.spot - inputs.strike
            : inputs.strike - inputs.spot;
        return std::max(intrinsic, 0.0);
    }

    const double standard_deviation = inputs.volatility * std::sqrt(inputs.maturity);
    const double carry = std::log(inputs.spot) - std::log(inputs.strike)
        - std::log(inputs.discount_factor) - inputs.dividend_yield * inputs.maturity;
    const double d1 = carry / standard_deviation + 0.5 * standard_deviation;
    const double d2 = d1 - standard_deviation;
    const double discounted_spot = inputs.spot
        * std::exp(-inputs.dividend_yield * inputs.maturity);
    const double discounted_strike = inputs.strike * inputs.discount_factor;

    const double result = type == OptionType::CALL
        ? discounted_spot * normalCdf(d1) - discounted_strike * normalCdf(d2)
        : discounted_strike * normalCdf(-d2) - discounted_spot * normalCdf(-d1);

    if (!std::isfinite(result)) {
        throw std::overflow_error("Black-Scholes price cannot be represented.");
    }
    return std::max(result, 0.0);
}
