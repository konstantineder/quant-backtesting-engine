

#pragma once

#include <cmath>
#include <stdexcept>

#include "volatility/volatility_surface.hpp"

class FlatVolSurface final : public VolatilitySurface {
public:
    explicit FlatVolSurface(double volatility)
        : volatility_(volatility)
    {
        if (!std::isfinite(volatility_) || volatility_ <= 0.0) {
            throw std::invalid_argument(
                "Volatility must be finite and positive."
            );
        }
    }

    double impliedVolatility(
        double strike,
        double maturity
    ) const override
    {
        if (!std::isfinite(strike) || strike <= 0.0) {
            throw std::invalid_argument("Invalid strike.");
        }

        if (!std::isfinite(maturity) || maturity < 0.0) {
            throw std::invalid_argument("Invalid maturity.");
        }

        return volatility_;
    }

private:
    const double volatility_;
};
