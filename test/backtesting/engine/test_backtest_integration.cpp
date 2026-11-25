// End-to-end tests of the whole engine: a deterministic strategy drives
// BacktestEngine over hand-built market data, and every figure asserted
// here was worked out on paper first.
//
// Nothing is stubbed and no component is driven directly. A failure means
// the pieces disagree about money somewhere between the signal and the
// equity curve -- order sizing, execution timing, cash settlement,
// position keeping, valuation or the metrics.
//
// The strategy is scripted rather than indicator-based on purpose. With an
// SMA the expected fills would depend on the signal logic too, and a
// broken reconciliation could hide behind a plausible-looking crossover.

#include "account/account.hpp"
#include "analytics/account_valuation.hpp"
#include "analytics/performance_metrics.hpp"
#include "backtesting/backtest_config.hpp"
#include "backtesting/backtest_engine.hpp"
#include "backtesting/backtest_result.hpp"
#include "common/constants.hpp"
#include "execution/fill.hpp"
#include "instrument/instrument.hpp"
#include "instrument/instrument_registry.hpp"
#include "instrument/instrument_universe.hpp"
#include "market_data/historical_market_data.hpp"
#include "strategy/strategy.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr double kEpsilon = 1e-9;
constexpr double kCash = 10'000.0;

// --------------------------------------------------------- market data --

struct Series {
    std::vector<double> open;
    std::vector<double> close;
};

// The TR-010.A price path. Open and close differ on every bar, so a fill
// taken from the wrong one is off by a visible amount rather than by
// rounding.
const Series kSeries{
    /* open  */ {98.0, 102.0, 106.0, 108.0, 109.0},
    /* close */ {100.0, 105.0, 110.0, 107.0, 112.0}
};

std::vector<Date> makeDates(std::size_t n) {
    std::vector<Date> dates;
    dates.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        dates.push_back(
            std::chrono::sys_days{std::chrono::year{2024} / 1 / 1} +
            std::chrono::days{static_cast<int>(i)}
        );
    }
    return dates;
}

HistoricalMarketData makeMarketData(
    const std::vector<Series>& columns,
    const std::vector<std::string>& ids
) {
    const std::size_t bars = columns.front().open.size();
    const auto rows = static_cast<Eigen::Index>(bars);
    const auto cols = static_cast<Eigen::Index>(columns.size());

    OHLCVData data;
    data.open = MarketMatrix::Zero(rows, cols);
    data.close = MarketMatrix::Zero(rows, cols);
    data.high = MarketMatrix::Zero(rows, cols);
    data.low = MarketMatrix::Zero(rows, cols);
    data.volume = MarketMatrix::Constant(rows, cols, 1'000.0);

    for (Eigen::Index col = 0; col < cols; ++col) {
        const Series& series = columns[static_cast<std::size_t>(col)];
        for (Eigen::Index t = 0; t < rows; ++t) {
            const auto bar = static_cast<std::size_t>(t);
            const double open = series.open[bar];
            const double close = series.close[bar];

            data.open(t, col) = open;
            data.close(t, col) = close;
            // HistoricalMarketData requires high >= max(open, close) and
            // low <= min(open, close).
            data.high(t, col) = std::max(open, close) * 1.01;
            data.low(t, col) = std::min(open, close) * 0.99;
        }
    }

    return HistoricalMarketData{makeDates(bars), ids, data};
}

HistoricalMarketData singleInstrumentData(const Series& series = kSeries) {
    return makeMarketData({series}, {"AAPL"});
}

const InstrumentRegistry& registry() {
    static const InstrumentRegistry built = [] {
        InstrumentRegistry reg;
        reg.registerInstrument(Equity{"AAPL", "AAPL"});
        reg.registerInstrument(Equity{"MSFT", "MSFT"});
        return reg;
    }();
    return built;
}

InstrumentUniverse makeUniverse(
    const std::vector<std::string>& ids = {"AAPL"}
) {
    return InstrumentUniverse::fromInstrumentIds(ids, registry());
}

BacktestEngine makeEngine(double cash = kCash) {
    return BacktestEngine{BacktestConfig{cash, TRADING_DAYS_PER_YEAR}};
}

// ----------------------------------------------------------- strategy --

// Emits exactly the weights it was handed, on exactly the bars it was
// handed them for, and stays silent on every other bar. Nothing is derived
// from prices, so the expected fills follow from the price path alone.
//
// A bar with no entry is a HOLD -- no emission at all, which the engine
// must distinguish from an emitted weight of zero.
class ScriptedStrategy final : public Strategy {
public:
    using Script = std::map<std::size_t, std::vector<double>>;

    explicit ScriptedStrategy(Script script) : script_(std::move(script)) {}

    bool onBar(const StrategyContext& context, TargetWeights& out) override {
        const auto entry = script_.find(context.currentIndex());
        if (entry == script_.end()) {
            return false;
        }

        out.assign(context.numInstruments(), 0.0);
        const std::size_t count =
            std::min(entry->second.size(), out.size());
        for (std::size_t slot = 0; slot < count; ++slot) {
            out[slot] = entry->second[slot];
        }
        return true;
    }

    std::unique_ptr<Strategy> clone() const override {
        return std::make_unique<ScriptedStrategy>(script_);
    }

private:
    Script script_;
};

// -------------------------------------------------------- expectations --

// Weight-sized quantity: the engine sizes off equity at the decision
// close, divided by that close and the contract multiplier, which is 1 for
// every instrument used here.
double targetQuantity(double weight, double equity, double close) {
    return weight * equity / close;
}

} // namespace

// ================================================== TR-010.A round trip ==

// The reference case, every figure derived by hand:
//
//   bar 0  close 100  invest 100%   equity 10'000  -> BUY 100
//   bar 1  open  102  filled                          cash -200, pos 100
//   bar 2  close 110  flatten       equity 10'800  -> SELL 100
//   bar 3  open  108  filled                          cash 10'600, pos 0
//   bar 4  no signal                equity 10'600
TEST(BacktestIntegrationTest, DeterministicLongRoundTrip) {
    const BacktestEngine engine = makeEngine();
    ScriptedStrategy strategy{{{0, {1.0}}, {2, {0.0}}}};

    const BacktestResult result =
        engine.run(strategy, makeUniverse(), singleInstrumentData());

    // --- the two fills, and nothing else ---
    ASSERT_EQ(result.fills().size(), 2u);

    const Fill& buy = result.fills()[0];
    EXPECT_EQ(buy.barIndex(), 1u);
    EXPECT_NEAR(buy.quantity(), 100.0, kEpsilon);
    EXPECT_NEAR(buy.price(), 102.0, kEpsilon);

    const Fill& sell = result.fills()[1];
    EXPECT_EQ(sell.barIndex(), 3u);
    EXPECT_NEAR(sell.quantity(), -100.0, kEpsilon);
    EXPECT_NEAR(sell.price(), 108.0, kEpsilon);

    // --- the equity curve, bar by bar ---
    const std::vector<double> expected{
        10'000.0,  // flat
        10'300.0,  // cash -200, 100 @ 105
        10'800.0,  // cash -200, 100 @ 110
        10'600.0,  // sold at 108, flat again
        10'600.0   // nothing left to move it
    };

    ASSERT_EQ(result.numBars(), expected.size());
    for (std::size_t bar = 0; bar < expected.size(); ++bar) {
        EXPECT_NEAR(result.equityCurve()[bar], expected[bar], 1e-6)
            << "bar " << bar;
    }

    // --- the account at the end ---
    const AccountValuation& final_valuation = result.finalValuation();
    EXPECT_NEAR(final_valuation.cash, 10'600.0, 1e-6);
    EXPECT_NEAR(final_valuation.position_market_value, 0.0, 1e-6);
    EXPECT_NEAR(final_valuation.equity, 10'600.0, 1e-6);
    EXPECT_NEAR(final_valuation.realized_pnl, 600.0, 1e-6);
    EXPECT_NEAR(final_valuation.unrealized_pnl, 0.0, 1e-6);
    EXPECT_NEAR(final_valuation.total_fees, 0.0, 1e-6);
    EXPECT_NEAR(final_valuation.net_pnl, 600.0, 1e-6);

    // --- the metrics ---
    EXPECT_NEAR(result.metrics().total_return, 0.06, 1e-9);
    // Peak 10'800 at bar 2, trough 10'600 at bar 3.
    EXPECT_NEAR(result.metrics().max_drawdown, 200.0 / 10'800.0, 1e-9);
    EXPECT_EQ(result.metrics().num_bars, 5u);
}

// The mirror image: sell first, buy back later. Cash goes UP on the
// opening trade and the position carries a negative market value.
TEST(BacktestIntegrationTest, DeterministicShortRoundTrip) {
    const BacktestEngine engine = makeEngine();
    ScriptedStrategy strategy{{{0, {-1.0}}, {2, {0.0}}}};

    const BacktestResult result =
        engine.run(strategy, makeUniverse(), singleInstrumentData());

    ASSERT_EQ(result.fills().size(), 2u);

    EXPECT_EQ(result.fills()[0].barIndex(), 1u);
    EXPECT_NEAR(result.fills()[0].quantity(), -100.0, kEpsilon);
    EXPECT_NEAR(result.fills()[0].price(), 102.0, kEpsilon);

    EXPECT_EQ(result.fills()[1].barIndex(), 3u);
    EXPECT_NEAR(result.fills()[1].quantity(), 100.0, kEpsilon);
    EXPECT_NEAR(result.fills()[1].price(), 108.0, kEpsilon);

    // Short 100 at 102, covered at 108: a 600 loss, the exact inverse of
    // the long round trip.
    const std::vector<double> expected{
        10'000.0,  // flat
        9'700.0,   // cash 20'200, -100 @ 105
        9'200.0,   // cash 20'200, -100 @ 110
        9'400.0,   // covered at 108
        9'400.0
    };

    ASSERT_EQ(result.numBars(), expected.size());
    for (std::size_t bar = 0; bar < expected.size(); ++bar) {
        EXPECT_NEAR(result.equityCurve()[bar], expected[bar], 1e-6)
            << "bar " << bar;
    }

    EXPECT_NEAR(result.finalValuation().cash, 9'400.0, 1e-6);
    EXPECT_NEAR(result.finalValuation().realized_pnl, -600.0, 1e-6);
    EXPECT_NEAR(result.metrics().total_return, -0.06, 1e-9);
}

// Long to short in one decision: the position has to cross zero, so the
// single order is larger than either leg.
TEST(BacktestIntegrationTest, PositionReversal) {
    const BacktestEngine engine = makeEngine();
    ScriptedStrategy strategy{{{0, {1.0}}, {2, {-1.0}}}};

    const BacktestResult result =
        engine.run(strategy, makeUniverse(), singleInstrumentData());

    ASSERT_EQ(result.fills().size(), 2u);

    // Bought 100 at bar 1's open, as in the long round trip.
    EXPECT_NEAR(result.fills()[0].quantity(), 100.0, kEpsilon);

    // At bar 2's close equity is 10'800, so the short target is
    // -10'800 / 110 units. Reaching it from +100 means selling the sum of
    // both legs in one order, not just the new target.
    const double short_target = targetQuantity(-1.0, 10'800.0, 110.0);
    const double expected_sell = short_target - 100.0;

    EXPECT_EQ(result.fills()[1].barIndex(), 3u);
    EXPECT_NEAR(result.fills()[1].quantity(), expected_sell, 1e-9);
    EXPECT_LT(expected_sell, -100.0)
        << "the reversal must exceed the long leg it unwinds";

    // The long leg realized (108 - 102) * 100 when it was sold through.
    EXPECT_NEAR(result.finalValuation().realized_pnl, 600.0, 1e-6);
    EXPECT_LT(result.finalValuation().position_market_value, 0.0);

    // Final equity: the realized 600, plus what the open short lost as the
    // price moved from its fill at 108 to the last close at 112.
    const double short_unrealized = short_target * (112.0 - 108.0);
    EXPECT_LT(short_unrealized, 0.0);
    EXPECT_NEAR(
        result.finalValuation().equity,
        kCash + 600.0 + short_unrealized,
        1e-6
    );
}

TEST(BacktestIntegrationTest, NoSignalProducesFlatEquityCurve) {
    const BacktestEngine engine = makeEngine();
    ScriptedStrategy strategy{{}};  // never emits

    const BacktestResult result =
        engine.run(strategy, makeUniverse(), singleInstrumentData());

    EXPECT_TRUE(result.fills().empty());
    ASSERT_EQ(result.numBars(), kSeries.close.size());

    for (std::size_t bar = 0; bar < result.numBars(); ++bar) {
        EXPECT_NEAR(result.equityCurve()[bar], kCash, kEpsilon)
            << "bar " << bar;
    }

    EXPECT_NEAR(result.finalValuation().cash, kCash, kEpsilon);
    EXPECT_DOUBLE_EQ(result.metrics().total_return, 0.0);
    EXPECT_DOUBLE_EQ(result.metrics().max_drawdown, 0.0);
    EXPECT_DOUBLE_EQ(result.metrics().volatility, 0.0);
}

// HOLD and FLATTEN are different instructions. Staying silent leaves the
// position alone; emitting a zero weight closes it. Conflating the two
// would mean a strategy that either never exits or never holds.
TEST(BacktestIntegrationTest, ZeroTargetClosesPosition) {
    const BacktestEngine engine = makeEngine();

    // Silent after bar 0: the position must survive to the end.
    ScriptedStrategy holding{{{0, {1.0}}}};
    const BacktestResult held =
        engine.run(holding, makeUniverse(), singleInstrumentData());

    ASSERT_EQ(held.fills().size(), 1u);
    EXPECT_NEAR(
        held.finalValuation().position_market_value, 100.0 * 112.0, 1e-6
    );

    // Same opening trade, then an explicit zero.
    ScriptedStrategy flattening{{{0, {1.0}}, {2, {0.0}}}};
    const BacktestResult flat =
        engine.run(flattening, makeUniverse(), singleInstrumentData());

    ASSERT_EQ(flat.fills().size(), 2u);
    EXPECT_NEAR(flat.finalValuation().position_market_value, 0.0, 1e-6);
}

// An order decided on the final close has no bar left to execute on. It
// must NOT be filled at that same bar's open, which would put the
// execution before the decision.
TEST(BacktestIntegrationTest, LastBarOrderIsNotExecuted) {
    const BacktestEngine engine = makeEngine();
    const std::size_t last_bar = kSeries.close.size() - 1;

    ScriptedStrategy strategy{{{last_bar, {1.0}}}};
    const BacktestResult result =
        engine.run(strategy, makeUniverse(), singleInstrumentData());

    EXPECT_TRUE(result.fills().empty())
        << "an order decided on the last bar was executed";
    EXPECT_NEAR(result.finalValuation().cash, kCash, kEpsilon);
    EXPECT_NEAR(result.equityCurve().back(), kCash, kEpsilon);
}

// The execution price is the NEXT bar's open, never the close the decision
// was based on. With open != close on every bar, one wrong lookup moves
// both the quantity and the cash.
TEST(BacktestIntegrationTest, ExecutionUsesNextBarOpen) {
    const BacktestEngine engine = makeEngine();
    ScriptedStrategy strategy{{{1, {1.0}}}};

    const BacktestResult result =
        engine.run(strategy, makeUniverse(), singleInstrumentData());

    ASSERT_EQ(result.fills().size(), 1u);
    const Fill& fill = result.fills().front();

    EXPECT_EQ(fill.barIndex(), 2u);
    EXPECT_NEAR(fill.price(), kSeries.open[2], kEpsilon);
    EXPECT_NE(fill.price(), kSeries.close[1]);
    EXPECT_NE(fill.price(), kSeries.close[2]);
    EXPECT_NE(fill.price(), kSeries.open[1]);

    // Sized off bar 1's close (equity 10'000, price 105) but executed at
    // bar 2's open (106). The gap between the two is exactly what a
    // close-fill model would wrongly hide.
    EXPECT_NEAR(fill.quantity(), kCash / kSeries.close[1], 1e-9);
}

// Two instruments on different price paths: each slot must be sized and
// priced from its own column. Swapped columns would still balance overall,
// so the per-slot figures are what catches it.
TEST(BacktestIntegrationTest, MultipleInstrumentsArePricedCorrectly) {
    const Series other{
        /* open  */ {48.0, 52.0, 56.0, 58.0, 59.0},
        /* close */ {50.0, 55.0, 60.0, 57.0, 62.0}
    };

    const HistoricalMarketData data =
        makeMarketData({kSeries, other}, {"AAPL", "MSFT"});

    const BacktestEngine engine = makeEngine();
    ScriptedStrategy strategy{{{0, {0.5, 0.5}}}};

    const BacktestResult result =
        engine.run(strategy, makeUniverse({"AAPL", "MSFT"}), data);

    ASSERT_EQ(result.fills().size(), 2u);

    // Submission order is slot order, so slot 0 comes first.
    const Fill& first = result.fills()[0];
    EXPECT_EQ(first.instrument(), InstrumentIndex{0});
    EXPECT_EQ(first.barIndex(), 1u);
    EXPECT_NEAR(first.price(), kSeries.open[1], kEpsilon);
    EXPECT_NEAR(
        first.quantity(),
        targetQuantity(0.5, kCash, kSeries.close[0]),
        1e-9
    );

    const Fill& second = result.fills()[1];
    EXPECT_EQ(second.instrument(), InstrumentIndex{1});
    EXPECT_EQ(second.barIndex(), 1u);
    EXPECT_NEAR(second.price(), other.open[1], kEpsilon);
    EXPECT_NEAR(
        second.quantity(),
        targetQuantity(0.5, kCash, other.close[0]),
        1e-9
    );

    // Half the equity each, but at roughly half the price on slot 1, so
    // the quantities must differ -- a swapped column would equalize them.
    EXPECT_GT(second.quantity(), first.quantity());
}

TEST(BacktestIntegrationTest, MisalignedUniverseThrows) {
    const BacktestEngine engine = makeEngine();
    const HistoricalMarketData data = singleInstrumentData();
    ScriptedStrategy strategy{{{0, {1.0}}}};

    // Right size, wrong instrument.
    EXPECT_THROW(
        static_cast<void>(engine.run(strategy, makeUniverse({"MSFT"}), data)),
        std::invalid_argument
    );

    // Wrong size.
    EXPECT_THROW(
        static_cast<void>(
            engine.run(strategy, makeUniverse({"AAPL", "MSFT"}), data)
        ),
        std::invalid_argument
    );
}

// Nothing may carry over between runs of the same engine. A leaked
// Account or order book would show up as a different second result.
TEST(BacktestIntegrationTest, RepeatedRunsAreIndependent) {
    const BacktestEngine engine = makeEngine();
    const HistoricalMarketData data = singleInstrumentData();

    ScriptedStrategy first{{{0, {1.0}}, {2, {0.0}}}};
    ScriptedStrategy second{{{0, {1.0}}, {2, {0.0}}}};

    const BacktestResult a = engine.run(first, makeUniverse(), data);
    const BacktestResult b = engine.run(second, makeUniverse(), data);

    ASSERT_EQ(a.numBars(), b.numBars());
    for (std::size_t bar = 0; bar < a.numBars(); ++bar) {
        EXPECT_DOUBLE_EQ(a.equityCurve()[bar], b.equityCurve()[bar])
            << "bar " << bar;
    }

    ASSERT_EQ(a.fills().size(), b.fills().size());
    for (std::size_t i = 0; i < a.fills().size(); ++i) {
        EXPECT_DOUBLE_EQ(a.fills()[i].quantity(), b.fills()[i].quantity());
        EXPECT_DOUBLE_EQ(a.fills()[i].price(), b.fills()[i].price());
        // Even the ids restart, because the generator is per run.
        EXPECT_EQ(a.fills()[i].orderId(), b.fills()[i].orderId());
    }
}

// The log is an audit trail, not a summary: replaying it into a fresh
// Account with the same starting cash must land on the same numbers.
TEST(BacktestIntegrationTest, ExecutionLogReproducesAccount) {
    const BacktestEngine engine = makeEngine();
    ScriptedStrategy strategy{{{0, {1.0}}, {2, {-1.0}}}};

    const BacktestResult result =
        engine.run(strategy, makeUniverse(), singleInstrumentData());
    ASSERT_FALSE(result.fills().empty());

    const InstrumentUniverse universe = makeUniverse();
    Account replayed = Account::forUniverse(kCash, universe);
    for (const Fill& fill : result.fills()) {
        replayed.applyFill(fill);
    }

    const AccountValuation& expected = result.finalValuation();
    EXPECT_NEAR(replayed.cash(), expected.cash, 1e-6);
    EXPECT_NEAR(replayed.realizedPnL(), expected.realized_pnl, 1e-6);
    EXPECT_NEAR(replayed.totalFees(), expected.total_fees, 1e-6);
    EXPECT_NEAR(
        replayed.position(InstrumentIndex{0}).quantity(),
        // The short left open by the reversal.
        targetQuantity(-1.0, 10'800.0, 110.0),
        1e-9
    );
}

// The metrics describe the curve the run produced, annualized with the
// configured factor -- recomputed here from the round trip's five equity
// points rather than copied from the implementation.
TEST(BacktestIntegrationTest, PerformanceMetricsMatchExpectedValues) {
    const BacktestEngine engine = makeEngine();
    ScriptedStrategy strategy{{{0, {1.0}}, {2, {0.0}}}};

    const BacktestResult result =
        engine.run(strategy, makeUniverse(), singleInstrumentData());

    // Per-bar returns of {10'000, 10'300, 10'800, 10'600, 10'600}.
    const std::vector<double> returns{
        10'300.0 / 10'000.0 - 1.0,
        10'800.0 / 10'300.0 - 1.0,
        10'600.0 / 10'800.0 - 1.0,
        0.0
    };

    double mean = 0.0;
    for (const double r : returns) {
        mean += r;
    }
    mean /= static_cast<double>(returns.size());

    double sum_squared_deviations = 0.0;
    for (const double r : returns) {
        sum_squared_deviations += (r - mean) * (r - mean);
    }
    const double stdev = std::sqrt(
        sum_squared_deviations / static_cast<double>(returns.size() - 1)
    );

    const PerformanceMetrics& metrics = result.metrics();

    EXPECT_NEAR(metrics.total_return, 0.06, 1e-12);
    EXPECT_NEAR(metrics.max_drawdown, 200.0 / 10'800.0, 1e-12);
    EXPECT_NEAR(
        metrics.volatility,
        stdev * std::sqrt(TRADING_DAYS_PER_YEAR),
        1e-9
    );
    EXPECT_NEAR(
        metrics.sharpe_ratio,
        (mean / stdev) * std::sqrt(TRADING_DAYS_PER_YEAR),
        1e-9
    );
    EXPECT_EQ(metrics.num_bars, 5u);
}
