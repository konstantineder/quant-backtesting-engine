#include "market_data/market_data.hpp"
#include <gtest/gtest.h>

/*
- Erfolgreiches Einfügen und Aktualisieren eines Preises.
- getPrice() bei unbekanntem Symbol wirft std::out_of_range.
- updatePrice() lehnt 0, negative Werte, NaN und Infinity ab.
- Leere Symbole werden abgelehnt.
- Nach einer fehlgeschlagenen Aktualisierung bleibt der bisherige Preis unverändert.
Eine kleine zusätzliche Herausforderung: Schreibe den letzten Test so, dass er explizit die Strong Exception Guarantee überprüft. 
*/

TEST(MarketSnapshotTest, UpdatePrice_Success) {
    MarketSnapshot snapshot;
    snapshot.updatePrice("AAPL", 150.0);
    EXPECT_DOUBLE_EQ(snapshot.getPrice("AAPL"), 150.0);

    snapshot.updatePrice("AAPL", 155.0);
    EXPECT_DOUBLE_EQ(snapshot.getPrice("AAPL"), 155.0);
}

TEST(MarketSnapshotTest, GetPrice_UnknownSymbol_Throws) {
    MarketSnapshot snapshot;
    EXPECT_THROW(snapshot.getPrice("UNKNOWN"), std::out_of_range);
}

TEST(MarketSnapshotTest, UpdatePrice_InvalidValues_Throws) {
    MarketSnapshot snapshot;

    EXPECT_THROW(snapshot.updatePrice("AAPL", 0.0), std::invalid_argument);
    EXPECT_THROW(snapshot.updatePrice("AAPL", -10.0), std::invalid_argument);
    EXPECT_THROW(snapshot.updatePrice("AAPL", std::numeric_limits<double>::infinity()), std::invalid_argument);
    EXPECT_THROW(snapshot.updatePrice("AAPL", std::numeric_limits<double>::quiet_NaN()), std::invalid_argument);
}

TEST(MarketSnapshotTest, UpdatePrice_EmptySymbol_Throws) {
    MarketSnapshot snapshot;
    EXPECT_THROW(snapshot.updatePrice("", 150.0), std::invalid_argument);
}

TEST(MarketSnapshotTest, UpdatePrice_StrongExceptionGuarantee) {
    MarketSnapshot snapshot;
    snapshot.updatePrice("AAPL", 150.0);

    EXPECT_THROW(
        snapshot.updatePrice("AAPL", -10.0),
        std::invalid_argument
    );

    EXPECT_DOUBLE_EQ(snapshot.getPrice("AAPL"), 150.0);
}