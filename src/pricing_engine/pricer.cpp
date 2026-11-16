#include "pricing_engine/pricer.hpp"
#include "pricing_engine/black_scholes_pricer.hpp"
#include <algorithm>
#include <chrono>
#include <type_traits>
#include <variant>
#include <stdexcept>

double PricingEngine::price(
    const Instrument& instrument,
    const InstrumentRegistry& registry,
    const MarketContext& market
) const {
    return std::visit(
        [&](const auto& inst) -> double {
            using T = std::decay_t<decltype(inst)>;

            if constexpr (std::is_same_v<T, Equity>) {
                return priceEquity(inst, market);
            }
            else if constexpr (std::is_same_v<T, EuropeanOption>) {
                return priceEuropeanOption(inst, registry, market);
            }
            else {
                throw std::logic_error(
                    "Pricing not implemented for this instrument."
                );
            }
        },
        instrument
    );
}

double PricingEngine::priceEquity(
    const Equity& equity,
    const MarketContext& market
) const {
    return market.spot(equity.ticker());
}

double PricingEngine::priceEuropeanOption(
    const EuropeanOption& option,
    const InstrumentRegistry& registry,
    const MarketContext& market
) const {
    const auto valuation_date = market.valuationDate();
    const auto expiry = option.expiryDate();

    if (expiry < valuation_date) {
        throw std::invalid_argument("Option has expired.");
    }

    // Resolve underlying instrument
    const Instrument& underlying =
        registry.getInstrument(option.underlyingID());

    const auto* equity = std::get_if<Equity>(&underlying);

    if (!equity) {
        throw std::invalid_argument(
            "Option underlying must be an equity."
        );
    }

    const double spot = market.spot(equity->ticker());

    // At expiry, the option is worth its intrinsic value.
    if (expiry == valuation_date) {
        switch (option.optionType()) {
            case OptionType::CALL:
                return std::max(spot - option.strike(), 0.0);

            case OptionType::PUT:
                return std::max(option.strike() - spot, 0.0);

            default:
                throw std::invalid_argument("Invalid option type.");
        }
    }

    // ACT/365 Fixed
    const auto days_to_expiry = expiry - valuation_date;

    const double maturity =
        static_cast<double>(days_to_expiry.count()) / 365.0;

    const BlackScholesInputs inputs{
        .spot = spot,
        .strike = option.strike(),
        .maturity = maturity,
        .volatility = market.impliedVolatility(
            option.underlyingID(),
            option.strike(),
            maturity
        ),
        .discount_factor = market.discountFactor(maturity),
        .dividend_yield = market.dividendYield(
            option.underlyingID()
        )
    };

    return BlackScholesPricer::price(
        option.optionType(),
        inputs
    );
}

