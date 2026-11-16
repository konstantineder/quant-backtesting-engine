#pragma once

class YieldCurve {
public:
    virtual ~YieldCurve() = default;

    virtual double discountFactor(
        double maturity
    ) const = 0;

    virtual double zeroRate(
        double maturity
    ) const = 0;
};