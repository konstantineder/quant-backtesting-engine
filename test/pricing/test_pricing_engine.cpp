#include "pricing_engine/pricer.hpp"
#include "instrument/instrument_registry.hpp"
#include "market_data/market_context.hpp"
#include "pricing_engine/black_scholes_inputs.hpp"
#include "pricing_engine/black_scholes_pricer.hpp"
#include "volatility/flat_volatility_surface.hpp"
#include "yield_curve/flat_yield_curve.hpp"
#include <gtest/gtest.h>
#include <chrono>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <unordered_map>

/*
Implementiere Tests für drei Fälle:
1. Aktie: Marktpreis 150 → Stückpreis = 150.
2. Nicht unterstütztes Instrument → std::logic_error.
3. Fehlende Marktdaten → std::out_of_range.
*/

TEST(PricingEngineTest, EquityPrice) {
    PricingEngine pricer;
    InstrumentRegistry registry;
    Equity equity("EQ-001", "AAPL");
    MarketSnapshot market;
    market.updatePrice("AAPL", 150.0);

    const MarketContext context{std::chrono::sys_days{}, market, {},
        std::make_shared<FlatYieldCurve>(0.0), {}};
    double price = pricer.price(equity, registry, context);
    EXPECT_DOUBLE_EQ(price, 150.0);
}

TEST(PricingEngineTest, UnsupportedInstrumentThrows) {
    PricingEngine pricer;
    InstrumentRegistry registry;
    Future future("FUT-001", "EQ-001", 100.0,
        std::chrono::sys_days{std::chrono::year{2024}/6/1},
        std::chrono::sys_days{std::chrono::year{2024}/12/1});
    MarketSnapshot market;
    market.updatePrice("EQ-001", 150.0);
    market.updatePrice("FUT-001", 150.0);

    const MarketContext context{std::chrono::sys_days{}, market, {},
        std::make_shared<FlatYieldCurve>(0.0), {}};
    EXPECT_THROW(pricer.price(future, registry, context), std::logic_error);
}

TEST(PricingEngineTest, MissingMarketData) {
    PricingEngine pricer;
    InstrumentRegistry registry;
    Equity equity("EQ-001", "AAPL");
    MarketSnapshot market;

    const MarketContext context{std::chrono::sys_days{}, market, {},
        std::make_shared<FlatYieldCurve>(0.0), {}};
    EXPECT_THROW(pricer.price(equity, registry, context), std::out_of_range);
}

class PricingEngineIntegrationTest : public ::testing::Test {
protected:
    PricingEngine pricer;
    InstrumentRegistry registry;
    MarketSnapshot snapshot;
    std::chrono::sys_days valuation_date{std::chrono::year{2024}/6/1};
    const std::chrono::sys_days start_date{std::chrono::year{2024}/1/1};
    const std::chrono::sys_days expiry_date{std::chrono::year{2025}/3/1};
    std::shared_ptr<const YieldCurve> curve = std::make_shared<FlatYieldCurve>(0.05);
    std::unordered_map<std::string, std::shared_ptr<const VolatilitySurface>> surfaces{
        {"EQ-001", std::make_shared<FlatVolSurface>(0.2)}
    };
    std::unordered_map<std::string, double> dividend_yields{{"EQ-001", 0.02}};

    void SetUp() override {
        registry.registerInstrument(Equity{"EQ-001", "AAPL"});
        snapshot.updatePrice("AAPL", 100.0);
    }

    MarketContext makeContext() const {
        return MarketContext{valuation_date, snapshot, surfaces, curve, dividend_yields};
    }

    EuropeanOption makeOption(
        OptionType type,
        double strike = 100.0,
        const std::string& underlying_id = "EQ-001"
    ) const {
        return EuropeanOption{"OPT-001", underlying_id, strike, start_date, expiry_date, type, 100.};
    }
};

TEST_F(PricingEngineIntegrationTest, EquityPricingUnchanged) {
    surfaces.clear();
    dividend_yields.clear();
    const auto context = makeContext();

    EXPECT_DOUBLE_EQ(pricer.price(registry.getInstrument("EQ-001"), registry, context), 100.0);
}

TEST_F(PricingEngineIntegrationTest, EuropeanCallPricing) {
    const auto option = makeOption(OptionType::CALL);
    const auto context = makeContext();
    const double maturity = static_cast<double>((expiry_date - valuation_date).count()) / 365.0;
    const BlackScholesInputs inputs{100.0, 100.0, maturity, 0.2, std::exp(-0.05 * maturity), 0.02};

    EXPECT_NEAR(pricer.price(option, registry, context),
        BlackScholesPricer::price(OptionType::CALL, inputs), 1e-10);
}

TEST_F(PricingEngineIntegrationTest, EuropeanPutPricing) {
    const auto option = makeOption(OptionType::PUT);
    const auto context = makeContext();
    const double maturity = static_cast<double>((expiry_date - valuation_date).count()) / 365.0;
    const BlackScholesInputs inputs{100.0, 100.0, maturity, 0.2, std::exp(-0.05 * maturity), 0.02};

    EXPECT_NEAR(pricer.price(option, registry, context),
        BlackScholesPricer::price(OptionType::PUT, inputs), 1e-10);
}

TEST_F(PricingEngineIntegrationTest, OptionAtExpiry) {
    valuation_date = expiry_date;
    surfaces.clear();
    dividend_yields.clear();
    const auto context = makeContext();

    EXPECT_DOUBLE_EQ(pricer.price(makeOption(OptionType::CALL, 90.0), registry, context), 10.0);
    EXPECT_DOUBLE_EQ(pricer.price(makeOption(OptionType::PUT, 110.0), registry, context), 10.0);
    EXPECT_DOUBLE_EQ(pricer.price(makeOption(OptionType::CALL), registry, context), 0.0);
    EXPECT_DOUBLE_EQ(pricer.price(makeOption(OptionType::PUT), registry, context), 0.0);
    EXPECT_DOUBLE_EQ(pricer.price(makeOption(OptionType::CALL, 110.0), registry, context), 0.0);
    EXPECT_DOUBLE_EQ(pricer.price(makeOption(OptionType::PUT, 90.0), registry, context), 0.0);
}

TEST_F(PricingEngineIntegrationTest, ExpiredOptionThrows) {
    valuation_date = expiry_date + std::chrono::days{1};
    snapshot = MarketSnapshot{};
    surfaces.clear();
    const auto option = makeOption(OptionType::CALL);
    const auto context = makeContext();

    EXPECT_THROW(pricer.price(option, registry, context), std::invalid_argument);
}

TEST_F(PricingEngineIntegrationTest, UnknownUnderlyingThrows) {
    surfaces.emplace("UNKNOWN", surfaces.at("EQ-001"));
    snapshot.updatePrice("UNKNOWN", 100.0);
    dividend_yields.emplace("UNKNOWN", 0.02);
    const auto option = makeOption(OptionType::CALL, 100.0, "UNKNOWN");
    const auto context = makeContext();

    EXPECT_THROW(pricer.price(option, registry, context), std::out_of_range);
}

TEST_F(PricingEngineIntegrationTest, NonEquityUnderlyingThrows) {
    registry.registerInstrument(Future{"FUT-001", "EQ-001", 100.0, start_date, expiry_date});
    surfaces.emplace("FUT-001", surfaces.at("EQ-001"));
    snapshot.updatePrice("FUT-001", 100.0);
    dividend_yields.emplace("FUT-001", 0.02);
    const auto option = makeOption(OptionType::CALL, 100.0, "FUT-001");
    const auto context = makeContext();

    EXPECT_THROW(pricer.price(option, registry, context), std::invalid_argument);
}

TEST_F(PricingEngineIntegrationTest, MissingVolatilitySurfaceThrows) {
    surfaces.clear();
    const auto option = makeOption(OptionType::CALL);
    const auto context = makeContext();

    EXPECT_THROW(pricer.price(option, registry, context), std::out_of_range);
}

TEST_F(PricingEngineIntegrationTest, MissingSpotThrows) {
    snapshot = MarketSnapshot{};
    const auto option = makeOption(OptionType::CALL);
    const auto context = makeContext();

    EXPECT_THROW(pricer.price(option, registry, context), std::out_of_range);
}

TEST_F(PricingEngineIntegrationTest, FuturePricingThrows) {
    const Future future{"FUT-001", "EQ-001", 100.0, start_date, expiry_date};
    registry.registerInstrument(future);
    snapshot.updatePrice("FUT-001", 100.0);
    snapshot.updatePrice("EQ-001", 100.0);
    const auto context = makeContext();

    try {
        pricer.price(future, registry, context);
        FAIL() << "Expected std::logic_error for unsupported future pricing";
    } catch (const std::logic_error& error) {
        EXPECT_EQ(typeid(error), typeid(std::logic_error));
    }
}
