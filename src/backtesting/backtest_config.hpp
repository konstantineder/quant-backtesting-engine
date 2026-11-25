#pragma once

#include "common/constants.hpp"

// What a run needs beyond the strategy, the universe and the market data.
//
// `bars_per_year` is not a property of the data but a statement about how
// to read it: it is the annualization factor for volatility and Sharpe, so
// a run over hourly bars must say so here or its risk figures come out
// scaled by the wrong square root. It lives next to the cash because both
// are decisions about the run, not about the market.
struct BacktestConfig {
    double initial_cash = 100'000.0;
    double bars_per_year = TRADING_DAYS_PER_YEAR;
};
