#pragma once

class VolatilitySurface {
public:
    virtual ~VolatilitySurface() = default;

    virtual double impliedVolatility(
        double strike,
        double maturity
    ) const = 0;
};