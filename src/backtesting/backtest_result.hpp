#pragma once

#include "analytics/performance_metrics.hpp"
#include "common/types.hpp"
#include "execution/execution_log.hpp"
#include "analytics/account_valuation.hpp"

#include <cstddef>
#include <span>
#include <stdexcept>
#include <vector>

// Everything a finished backtest hands back.
//
// The equity curve and the dates are parallel arrays, one entry per
// processed bar; `execution_log` holds every fill in execution order, which
// makes the run replayable into a fresh Account; `final_valuation` is the
// mark-to-market of the last bar.
//
// A plain value type: a worker fills one of these and moves it out, so
// nothing here is shared between parallel backtests.
class BacktestResult {
public:
    BacktestResult() = default;

    BacktestResult(
        std::vector<Date> dates,
        std::vector<double> equity_curve,
        ExecutionLog execution_log,
        AccountValuation final_valuation,
        double bars_per_year = TRADING_DAYS_PER_YEAR
    ): dates_(std::move(dates)),
       equity_curve_(std::move(equity_curve)),
       execution_log_(std::move(execution_log)),
       final_valuation_(final_valuation) {
        if (dates_.size() != equity_curve_.size()) {
            throw std::invalid_argument(
                "Dates and equity curve must have the same length"
            );
        }

        metrics_ = computePerformanceMetrics(
            equity_curve_, bars_per_year
        );
    }

    std::span<const Date> dates() const noexcept { return dates_; }

    std::span<const double> equityCurve() const noexcept {
        return equity_curve_;
    }

    std::span<const Fill> fills() const noexcept {
        return execution_log_.fills();
    }

    const ExecutionLog& executionLog() const noexcept {
        return execution_log_;
    }

    const AccountValuation& finalValuation() const noexcept {
        return final_valuation_;
    }

    const PerformanceMetrics& metrics() const noexcept {
        return metrics_;
    }

    std::size_t numBars() const noexcept {
        return equity_curve_.size();
    }

private:
    std::vector<Date> dates_;
    std::vector<double> equity_curve_;
    ExecutionLog execution_log_;
    AccountValuation final_valuation_{};
    PerformanceMetrics metrics_{};
};
