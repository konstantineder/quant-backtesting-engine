#pragma once

#include <chrono>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "market_data/market_data.hpp"
#include "volatility/volatility_surface.hpp"
#include "yield_curve/yield_curve.hpp"

class MarketContext {
public:
    MarketContext(
        std::chrono::sys_days valuation_date,
        const MarketSnapshot& snapshot,
        const std::unordered_map<std::string, std::shared_ptr<const VolatilitySurface>>& volatility_surfaces,
        const std::shared_ptr<const YieldCurve>& discount_curve,
        const std::unordered_map<std::string, double>& dividend_yields
    ): valuation_date_(valuation_date),
    snapshot_(snapshot),
    volatility_surfaces_(volatility_surfaces),
    discount_curve_(discount_curve),
    dividend_yields_(dividend_yields) {
        validate();
    }

    double spot(const std::string& ticker) const {
        if (ticker.empty()) {
            throw std::invalid_argument("Ticker must not be empty.");
        }
        return snapshot_.getPrice(ticker);
    }

    double impliedVolatility(
        const std::string& underlying_id,
        double strike,
        double maturity
    ) const {
        validateUnderlyingId(underlying_id);
        if (!std::isfinite(strike) || strike <= 0.0) {
            throw std::invalid_argument("Strike must be finite and positive.");
        }
        validateMaturity(maturity);
        const auto& vola_surface = volatility_surfaces_.at(underlying_id);
        return vola_surface->impliedVolatility(strike, maturity);
    }

    double discountFactor(double maturity) const {
        validateMaturity(maturity);
        return discount_curve_->discountFactor(maturity);
    }

    double zeroRate(double maturity) const {
        validateMaturity(maturity);
        return discount_curve_->zeroRate(maturity);
    }

    double dividendYield(
        const std::string& underlying_id
    ) const {
        validateUnderlyingId(underlying_id);

        const auto it = dividend_yields_.find(underlying_id);

        return it != dividend_yields_.end()
            ? it->second
            : 0.0;
    }

    std::chrono::sys_days valuationDate() const noexcept {
        return valuation_date_;
    }

private:
    static void validateUnderlyingId(const std::string& underlying_id) {
        if (underlying_id.empty()) {
            throw std::invalid_argument("Underlying ID must not be empty.");
        }
    }

    static void validateMaturity(double maturity) {
        if (!std::isfinite(maturity) || maturity < 0.0) {
            throw std::invalid_argument("Maturity must be finite and non-negative.");
        }
    }

    void validate() const {
        if (!discount_curve_) {
            throw std::invalid_argument("Discount curve must not be null.");
        }
        for (const auto& [underlying_id, surface] : volatility_surfaces_) {
            validateUnderlyingId(underlying_id);
            if (!surface) {
                throw std::invalid_argument("Volatility surface must not be null.");
            }
        }
        for (const auto& [underlying_id, yield] : dividend_yields_) {
            validateUnderlyingId(underlying_id);
            if (!std::isfinite(yield)) {
                throw std::invalid_argument("Dividend yield must be finite.");
            }
        }
    }

    const std::chrono::sys_days valuation_date_;
    const MarketSnapshot snapshot_;
    const std::unordered_map<std::string, std::shared_ptr<const VolatilitySurface>> volatility_surfaces_;
    const std::shared_ptr<const YieldCurve> discount_curve_;
    const std::unordered_map<std::string, double> dividend_yields_;
};
