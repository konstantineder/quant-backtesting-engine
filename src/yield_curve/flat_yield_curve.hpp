#include "yield_curve/yield_curve.hpp"
#include <cmath>
#include <stdexcept>

class FlatYieldCurve final : public YieldCurve {
public:
    explicit FlatYieldCurve(double rate): rate_(rate) {
        if(!std::isfinite(rate_)) {
            throw std::invalid_argument("Rate needs to be finite.");
        }
    }

    double discountFactor(double maturity) const override {
        const double rate = zeroRate(maturity);

        const double df = std::exp(-rate * maturity);

        if (!std::isfinite(df) || df <= 0.0) {
            throw std::overflow_error(
                "Discount factor cannot be represented."
            );
        }

        return df;
    }

    double zeroRate(
        double maturity
    ) const override {
        return rate_;
    };

private:
    const double rate_;
};