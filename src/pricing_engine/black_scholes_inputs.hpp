#pragma once

struct BlackScholesInputs {
    double spot;
    double strike;
    double maturity;
    double volatility;
    double discount_factor;
    double dividend_yield;
};
