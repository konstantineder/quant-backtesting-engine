#pragma once

#include "account/account.hpp"
#include "market_data/market_context.hpp"
#include "pricing_engine/pricer.hpp"
#include "instrument/instrument_universe.hpp"
#include "analytics/account_valuation.hpp"

#include <span>
#include <vector>

// Marks an Account to market through the existing PricingEngine,
// InstrumentRegistry and MarketContext.
//
// Unit-price convention: PricingEngine::price returns a price per share or
// per option contract unit. The contract multiplier comes from the
// account's InstrumentTradingSpec and is applied here, exactly once --
// never inside the pricer and never a second time on the cash path.
class AccountValuator {
public:
    AccountValuation evaluate(
        const Account& account,
        const InstrumentUniverse& universe,
        const MarketContext& market,
        const PricingEngine& pricer
    ) const;

    // Same valuation from unit prices that were computed elsewhere -- one
    // per universe slot, aligned with InstrumentIndex. Flat slots are
    // never read, so a slot without a usable price is harmless as long as
    // nothing is held in it.
    AccountValuation evaluateWithPrices(
        const Account& account,
        std::span<const double> unit_prices
    ) const;

    // Prices every slot of the universe into `out`, which is resized to
    // the universe size. Intended for per-bar precomputation and caching:
    // call once per bar, then value any number of accounts against it.
    void priceUniverse(
        const InstrumentUniverse& universe,
        const MarketContext& market,
        const PricingEngine& pricer,
        std::vector<double>& out
    ) const;
};
