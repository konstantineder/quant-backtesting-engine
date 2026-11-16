#pragma once

#include "backtesting/market_data/historical_market_data.hpp"

#include <cstddef>
#include <string>
#include <vector>

class StrategyContext {
public:
    StrategyContext(
        const HistoricalMarketData& data,
        std::size_t current_index
    );

    Date currentDate() const;

    std::size_t currentIndex() const noexcept;
    std::size_t numInstruments() const noexcept;

    const std::vector<std::string>&
    instrumentIds() const noexcept;

    std::size_t instrumentIndex(
        const std::string& instrument_id
    ) const;

    Bar bar(
        std::size_t instrument_index,
        std::size_t lookback = 0
    ) const;

    Bar bar(
        const std::string& instrument_id,
        std::size_t lookback = 0
    ) const;

    double close(
        std::size_t instrument_index,
        std::size_t lookback = 0
    ) const;

private:
    std::size_t historicalIndex(
        std::size_t lookback
    ) const;

    const HistoricalMarketData& data_;
    std::size_t current_index_;
};
