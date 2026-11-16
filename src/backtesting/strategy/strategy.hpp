#pragma once

#include "backtesting/strategy/strategy_context.hpp"

#include <cstddef>
#include <initializer_list>
#include <memory>
#include <vector>
#include <variant>
#include <optional>
#include <utility>

template<typename Tag>
class TargetVector {
public:
    TargetVector() = default;

    TargetVector(std::initializer_list<double> values)
        : values_(values) {}

    explicit TargetVector(std::vector<double> values)
        : values_(std::move(values)) {}

    const double& operator[](std::size_t idx) const {
        return values_[idx];
    }

    double& operator[](std::size_t idx) {
        return values_[idx];
    }

    std::size_t size() const noexcept {
        return values_.size();
    }

    bool empty() const noexcept {
        return values_.empty();
    }

    void assign(std::size_t size, double value) {
        values_.assign(size, value);
    }

private:
    std::vector<double> values_;
};

struct WeightTag {};
struct QuantityTag {};

using TargetWeights = TargetVector<WeightTag>;
using TargetQuantities = TargetVector<QuantityTag>;

using PortfolioTarget = std::variant<
    TargetWeights,
    TargetQuantities
>;

using StrategyDecision = std::optional<PortfolioTarget>;

class Strategy {
public:
    virtual ~Strategy() = default;

    virtual bool onBar(
        const StrategyContext& context,
        TargetWeights& out
    ) = 0;

    virtual std::unique_ptr<Strategy> clone() const = 0;
};