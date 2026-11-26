#pragma once

#include "execution/commission/commission_policy.hpp"
#include "execution/commission/no_commission.hpp"
#include "execution/fill.hpp"
#include "execution/slippage/no_slippage.hpp"
#include "execution/slippage/slippage_policy.hpp"
#include "instrument/instrument_trading_spec.hpp"
#include "instrument/instrument_universe.hpp"
#include "market_data/historical_market_data.hpp"
#include "order/order.hpp"
#include "order/pending_order_book.hpp"

#include <concepts>
#include <cstddef>
#include <utility>
#include <vector>

// Answers, for one bar, what the market would have given the orders that
// are working -- composed at compile time from a slippage and a commission
// model.
//
// TIMING. A market order submitted on bar i executes at the open of bar
// i + 1 at the earliest, never on the bar whose data the strategy has just
// seen. Filling on the submission bar would let a strategy trade on a
// price it could not have acted on in time, which is the classic
// look-ahead bias; the bar guard is the core of this model, not a detail.
//
// COSTS. The reference price is that open. Slippage turns it into the
// execution price, and the commission is charged on THAT price, not on the
// reference -- the order is: reference -> slippage -> execution price ->
// commission. Only the commission becomes the Fill's fee. Slippage is
// already carried by the execution price, so putting it in the fee as well
// would count it twice and double the reported trading cost.
//
// The quantity is never touched by either policy: a cost model changes
// what an execution costs, not how much of it happens.
//
// WHY POLICIES RATHER THAN A VARIANT. Both models are template parameters,
// so every call is resolved at compile time, a stateless model costs
// nothing at run time, and a new model needs no change here at all -- it
// only has to satisfy the concept. A closed std::variant would have to
// grow an alternative for each one, and would put a dispatch in the
// innermost loop of the simulation. Choosing a model at run time is still
// possible; it just happens once per backtest, outside this loop.
//
// Still deliberately NOT modelled: volume caps, liquidity and partial
// fills. The whole remaining quantity executes at one price.
//
// Pure: it reads the book and the market data and returns the fills it
// would produce, touching neither the book nor the Account. Applying them
// is the engine's job, which leaves a bar's fills inspectable, loggable or
// discardable before anything has moved.
//
// PRECONDITION: universe slots and market data columns are aligned -- what
// InstrumentUniverse::fromInstrumentIds and validateAlignment establish --
// because an InstrumentIndex is used directly as a column index.
template<
    SlippagePolicy S = NoSlippage,
    CommissionPolicy C = NoCommission
>
class ExecutionModel {
public:
    // Compile-time dependency injection: the template arguments fix WHICH
    // models are used, the constructors supply the configured instances.
    // A rate is therefore a run-time value, so a sweep over rates
    // instantiates this template exactly once.
    //
    // Three constructors rather than one with `= S{}` default arguments,
    // which look equivalent and are not: a default argument is
    // instantiated where it is used, and the failure to default-construct
    // a stateful policy there is a hard error rather than a substitution
    // failure. Asking is_default_constructible_v of a composition holding
    // a stateful policy would then not answer false -- it would fail to
    // compile. Constraining the
    // constructors instead makes the question answerable, which is what
    // lets a caller (or a static_assert) ask it.
    ExecutionModel()
        requires std::default_initializable<S> &&
                 std::default_initializable<C>
    = default;

    explicit ExecutionModel(S slippage)
        requires std::default_initializable<C>
        : slippage_(std::move(slippage)) {}

    ExecutionModel(S slippage, C commission)
        : slippage_(std::move(slippage)),
          commission_(std::move(commission)) {}

    [[nodiscard]]
    std::vector<Fill> execute(
        const PendingOrderBook& pending_orders,
        const HistoricalMarketData& market_data,
        const InstrumentUniverse& universe,
        std::size_t bar_index
    ) const;

    [[nodiscard]]
    const S& slippage() const noexcept { return slippage_; }

    [[nodiscard]]
    const C& commission() const noexcept { return commission_; }

private:
    // An empty policy costs no storage here. The attribute permits the
    // overlap; whether the compiler takes it is up to it, and the layout
    // stays implementation-defined. What this expresses is the intent --
    // a frictionless model should be free, not merely cheap.
    [[no_unique_address]] S slippage_;
    [[no_unique_address]] C commission_;
};

// The definition has to stay visible to every translation unit that
// instantiates the template, so it lives in a .tpp included here rather
// than in a separately compiled .cpp. Splitting it keeps this header
// readable as an interface.
#include "execution/execution_model.tpp"
