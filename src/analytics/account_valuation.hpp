#pragma once

// Result of marking an Account to market.
//
// Conventions, chosen so that nothing is counted twice:
//   * `position_market_value` already includes contract multipliers and is
//     signed -- a short position contributes negatively.
//   * `realized_pnl` is GROSS of fees; `total_fees` carries them once.
//   * `net_pnl` = realized + unrealized - fees, which satisfies
//     `equity - initialCash() == net_pnl` exactly.
struct AccountValuation {
    double cash = 0.0;
    double position_market_value = 0.0;
    double equity = 0.0;
    double realized_pnl = 0.0;
    double unrealized_pnl = 0.0;
    double total_fees = 0.0;
    double net_pnl = 0.0;
};
