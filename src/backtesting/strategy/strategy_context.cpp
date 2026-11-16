#include "backtesting/strategy/strategy_context.hpp"

#include <stdexcept>

StrategyContext::StrategyContext(
    const HistoricalMarketData& data,
    std::size_t current_index
): data_(data), current_index_(current_index) {
    if (current_index_ >= data_.numDates()) {
        throw std::out_of_range(
            "current_index out of range"
        );
    }
}

std::size_t StrategyContext::historicalIndex(
    std::size_t lookback
) const {
    if (lookback > current_index_) {
        throw std::out_of_range(
            "lookback exceeds available history"
        );
    }

    return current_index_ - lookback;
}

Date StrategyContext::currentDate() const {
    return data_.date(currentIndex());
}

std::size_t StrategyContext::currentIndex() const noexcept {
    return current_index_;
}

std::size_t StrategyContext::numInstruments() const noexcept {
    return data_.numInstruments();
}

const std::vector<std::string>&
StrategyContext::instrumentIds() const noexcept {
    return data_.instrumentIds();
}

std::size_t StrategyContext::instrumentIndex(
    const std::string& instrument_id
) const {
    return data_.instrumentIndex(instrument_id);
}

Bar StrategyContext::bar(
    std::size_t instrument_index,
    std::size_t lookback
) const {
    if (lookback > current_index_) {
        throw std::out_of_range(
            "lookback exceeds available history"
        );
    }

    return data_.bar(current_index_ - lookback, instrument_index);
}

Bar StrategyContext::bar(
    const std::string& instrument_id,
    std::size_t lookback
) const {
    if (lookback > current_index_) {
        throw std::out_of_range(
            "lookback exceeds available history"
        );
    }

    return data_.bar(current_index_ - lookback, instrument_id);
}

double StrategyContext::close(
    std::size_t instrument_index,
    std::size_t lookback
) const {
    if (lookback > current_index_) {
        throw std::out_of_range(
            "lookback exceeds available history"
        );
    }

    return data_.close(current_index_ - lookback, instrument_index);
}
