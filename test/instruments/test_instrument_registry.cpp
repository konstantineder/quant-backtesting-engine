#include <gtest/gtest.h>
#include "instrument/instrument_registry.hpp"

class InstrumentRegistryTest : public ::testing::Test {
protected:
    InstrumentRegistry registry;
};

TEST_F(InstrumentRegistryTest, AddInstrument) {
    Equity equity("inst", "AAPL");

    registry.registerInstrument(equity);

    EXPECT_EQ(registry.size(), 1);
    EXPECT_TRUE(registry.contains(equity.instrumentID()));

    const auto& inst1 = registry.getInstrument(equity.instrumentID());

    const auto& eq = std::get<Equity>(inst1);

    EXPECT_EQ(eq.instrumentID(), "inst");
    EXPECT_EQ(eq.ticker(), "AAPL");

    std::chrono::sys_days start_date = std::chrono::sys_days{std::chrono::year{2024}/6/1};
    std::chrono::sys_days end_date = std::chrono::sys_days{std::chrono::year{2024}/12/1};
    EuropeanOption european_option(
        "eo", 
        "AAPL", 
        100.0, 
        start_date, 
        end_date,OptionType::CALL, 
        100.0
    );
    registry.registerInstrument(european_option);

    EXPECT_TRUE(registry.size() == 2);
    EXPECT_TRUE(registry.contains(european_option.instrumentID()));

    const auto& inst2 = registry.getInstrument(european_option.instrumentID());
    const auto& eo = std::get<EuropeanOption>(inst2);
    EXPECT_EQ(eo.instrumentID(), european_option.instrumentID());
    EXPECT_EQ(eo.optionType(), european_option.optionType());

    Future future("fut", "AAPL", 100.0, start_date, end_date);
    registry.registerInstrument(future);

    EXPECT_TRUE(registry.size() == 3);
    EXPECT_TRUE(registry.contains(future.instrumentID()));

    const auto& inst3 = registry.getInstrument(future.instrumentID());
    const auto& fut = std::get<Future>(inst3);

    EXPECT_EQ(fut.instrumentID(), future.instrumentID());
    EXPECT_EQ(fut.startDate(), future.startDate());
}

TEST_F(InstrumentRegistryTest, DuplicateDoesNotOverwriteExistingInstrument) {
    registry.registerInstrument(Equity("inst", "AAPL"));

    EXPECT_THROW(
        registry.registerInstrument(Equity("inst", "MSFT")),
        std::runtime_error
    );

    EXPECT_EQ(registry.size(), 1);

    const auto& instrument = registry.getInstrument("inst");
    const auto& equity = std::get<Equity>(instrument);

    EXPECT_EQ(equity.ticker(), "AAPL");
}

TEST_F(InstrumentRegistryTest, UnknownInstrumentThrows) {
    EXPECT_FALSE(registry.contains("unknown"));

    EXPECT_THROW(
        registry.getInstrument("unknown"),
        std::out_of_range
    );
}
