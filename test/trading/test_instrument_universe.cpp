#include "instrument/instrument_universe.hpp"

#include "market_data/historical_market_data.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

namespace {

InstrumentRegistry makeRegistry() {
    InstrumentRegistry registry;
    registry.registerInstrument(Equity{"AAPL", "AAPL"});
    registry.registerInstrument(Equity{"MSFT", "MSFT"});
    registry.registerInstrument(EuropeanOption{
        "AAPL-CALL", "AAPL", 100.0,
        std::chrono::sys_days{std::chrono::year{2024} / 1 / 1},
        std::chrono::sys_days{std::chrono::year{2024} / 6 / 1},
        OptionType::CALL, 25.0
    });
    return registry;
}

std::vector<InstrumentId> makeIds(
    const std::vector<std::string>& raw
) {
    std::vector<InstrumentId> ids;
    ids.reserve(raw.size());
    for (const std::string& id : raw) {
        ids.emplace_back(id);
    }
    return ids;
}

} // namespace

TEST(InstrumentIdTest, RejectsEmptyId) {
    EXPECT_THROW(InstrumentId{""}, std::invalid_argument);
    EXPECT_NO_THROW(InstrumentId{"AAPL"});
}

TEST(InstrumentIdTest, ComparesByValue) {
    EXPECT_EQ(InstrumentId{"AAPL"}, InstrumentId{"AAPL"});
    EXPECT_NE(InstrumentId{"AAPL"}, InstrumentId{"MSFT"});
    EXPECT_LT(InstrumentId{"AAPL"}, InstrumentId{"MSFT"});
    EXPECT_EQ(
        std::hash<InstrumentId>{}(InstrumentId{"AAPL"}),
        std::hash<InstrumentId>{}(InstrumentId{"AAPL"})
    );
}

TEST(InstrumentIndexTest, IsNotImplicitlyConvertible) {
    static_assert(!std::is_convertible_v<std::size_t, InstrumentIndex>);
    static_assert(!std::is_convertible_v<InstrumentIndex, std::size_t>);

    EXPECT_EQ(InstrumentIndex{3}.value(), 3u);
    EXPECT_EQ(InstrumentIndex{3}, InstrumentIndex{3});
    EXPECT_LT(InstrumentIndex{1}, InstrumentIndex{2});
}

TEST(InstrumentUniverseTest, MapsIdsToDenseIndices) {
    const InstrumentRegistry registry = makeRegistry();
    const InstrumentUniverse universe{
        makeIds({"AAPL", "MSFT", "AAPL-CALL"}), registry
    };

    ASSERT_EQ(universe.size(), 3u);
    EXPECT_EQ(universe.index(InstrumentId{"AAPL"}), InstrumentIndex{0});
    EXPECT_EQ(universe.index(InstrumentId{"MSFT"}), InstrumentIndex{1});
    EXPECT_EQ(
        universe.index(InstrumentId{"AAPL-CALL"}),
        InstrumentIndex{2}
    );
    EXPECT_EQ(universe.id(InstrumentIndex{1}), InstrumentId{"MSFT"});
}

TEST(InstrumentUniverseTest, ResolvesInstrumentsWithoutStringLookup) {
    const InstrumentRegistry registry = makeRegistry();
    const InstrumentUniverse universe{
        makeIds({"AAPL", "AAPL-CALL"}), registry
    };

    const Instrument& equity = universe.instrument(InstrumentIndex{0});
    ASSERT_NE(std::get_if<Equity>(&equity), nullptr);
    EXPECT_EQ(std::get<Equity>(equity).ticker(), "AAPL");

    const Instrument& option = universe.instrument(InstrumentIndex{1});
    ASSERT_NE(std::get_if<EuropeanOption>(&option), nullptr);
    EXPECT_DOUBLE_EQ(
        std::get<EuropeanOption>(option).contractMultiplier(), 25.0
    );

    // The resolved reference is the registry's own object, not a copy.
    EXPECT_EQ(&equity, &registry.getInstrument("AAPL"));
}

TEST(InstrumentUniverseTest, RejectsEmptyUniverse) {
    const InstrumentRegistry registry = makeRegistry();
    EXPECT_THROW(
        (InstrumentUniverse{{}, registry}),
        std::invalid_argument
    );
}

TEST(InstrumentUniverseTest, RejectsDuplicateIds) {
    const InstrumentRegistry registry = makeRegistry();
    EXPECT_THROW(
        (InstrumentUniverse{makeIds({"AAPL", "AAPL"}), registry}),
        std::invalid_argument
    );
}

TEST(InstrumentUniverseTest, RejectsUnregisteredInstruments) {
    const InstrumentRegistry registry = makeRegistry();
    EXPECT_THROW(
        (InstrumentUniverse{makeIds({"AAPL", "TSLA"}), registry}),
        std::out_of_range
    );
}

TEST(InstrumentUniverseTest, RejectsUnknownLookups) {
    const InstrumentRegistry registry = makeRegistry();
    const InstrumentUniverse universe{makeIds({"AAPL"}), registry};

    EXPECT_FALSE(universe.contains(InstrumentId{"MSFT"}));
    EXPECT_TRUE(universe.contains(InstrumentId{"AAPL"}));
    EXPECT_THROW(universe.index(InstrumentId{"MSFT"}), std::out_of_range);
    EXPECT_THROW(universe.id(InstrumentIndex{1}), std::out_of_range);
    EXPECT_THROW(
        universe.instrument(InstrumentIndex{1}), std::out_of_range
    );
}

TEST(InstrumentUniverseTest, AlignsWithHistoricalMarketData) {
    const InstrumentRegistry registry = makeRegistry();

    const std::vector<std::string> columns{"AAPL", "MSFT"};
    const InstrumentUniverse universe =
        InstrumentUniverse::fromInstrumentIds(columns, registry);

    ASSERT_EQ(universe.size(), 2u);
    EXPECT_EQ(universe.index(InstrumentId{"AAPL"}), InstrumentIndex{0});
    EXPECT_EQ(universe.index(InstrumentId{"MSFT"}), InstrumentIndex{1});
    EXPECT_NO_THROW(universe.validateAlignment(columns));
}

TEST(InstrumentUniverseTest, DetectsMisalignedMarketData) {
    const InstrumentRegistry registry = makeRegistry();
    const InstrumentUniverse universe{
        makeIds({"AAPL", "MSFT"}), registry
    };

    const std::vector<std::string> swapped{"MSFT", "AAPL"};
    EXPECT_THROW(
        universe.validateAlignment(swapped), std::invalid_argument
    );

    const std::vector<std::string> shorter{"AAPL"};
    EXPECT_THROW(
        universe.validateAlignment(shorter), std::invalid_argument
    );
}

TEST(InstrumentUniverseTest, IsImmutable) {
    static_assert(!std::is_copy_assignable_v<InstrumentUniverse>);
    static_assert(!std::is_move_assignable_v<InstrumentUniverse>);
    static_assert(std::is_copy_constructible_v<InstrumentUniverse>);
}
