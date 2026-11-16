#pragma once

#include "common/types.hpp"

#include <Eigen/Dense>

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

using MarketMatrix = Eigen::Matrix<
    double,
    Eigen::Dynamic,
    Eigen::Dynamic,
    Eigen::ColMajor
>;

struct Bar {
    double open;
    double high;
    double low;
    double close;
    double volume;
};

struct OHLCVData {
    MarketMatrix open;
    MarketMatrix high;
    MarketMatrix low;
    MarketMatrix close;
    MarketMatrix volume;
};

class HistoricalMarketData {
public:
    HistoricalMarketData(
        std::vector<Date> dates,
        std::vector<std::string> instrument_ids,
        OHLCVData data
    );

    std::size_t numDates() const noexcept;
    std::size_t numInstruments() const noexcept;

    Date date(std::size_t time_index) const;

    Bar bar(
        std::size_t time_index,
        std::size_t instrument_index
    ) const;

    Bar bar(
        std::size_t time_index,
        const std::string& instrument_id
    ) const;

    double close(
        std::size_t time_index,
        std::size_t instrument_index
    ) const;

    std::size_t instrumentIndex(
        const std::string& instrument_id
    ) const;

    const std::vector<std::string>&
    instrumentIds() const noexcept;

private:
    std::vector<Date> dates_;
    std::vector<std::string> instrument_ids_;
    OHLCVData data_;

    std::unordered_map<std::string, std::size_t>
        instrument_index_;
};
