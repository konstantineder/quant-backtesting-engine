#include "market_data/historical_market_data.hpp"

#include <stdexcept>
#include <unordered_set>

namespace {

void validateNonEmpty(
    const std::vector<Date>& dates,
    const std::vector<std::string>& instrument_ids
) {
    if (dates.empty()) {
        throw std::invalid_argument(
            "HistoricalMarketData requires at least one date"
        );
    }

    if (instrument_ids.empty()) {
        throw std::invalid_argument(
            "HistoricalMarketData requires at least one instrument"
        );
    }
}

void validateInstrumentIds(
    const std::vector<std::string>& instrument_ids
) {
    std::unordered_set<std::string> seen;
    seen.reserve(instrument_ids.size());

    for (const auto& instrument_id : instrument_ids) {
        if (instrument_id.empty()) {
            throw std::invalid_argument(
                "Instrument id must not be empty"
            );
        }

        if (!seen.insert(instrument_id).second) {
            throw std::invalid_argument(
                "Duplicate instrument id: " + instrument_id
            );
        }
    }
}

void validateDatesAscending(
    const std::vector<Date>& dates
) {
    for (std::size_t t = 1; t < dates.size(); ++t) {
        if (!(dates[t - 1] < dates[t])) {
            throw std::invalid_argument(
                "Dates must be strictly ascending"
            );
        }
    }
}

void validateDimensions(
    const OHLCVData& data,
    std::size_t num_dates,
    std::size_t num_instruments
) {
    const auto rows = static_cast<Eigen::Index>(num_dates);
    const auto cols = static_cast<Eigen::Index>(num_instruments);

    const bool dimensions_match =
        data.open.rows() == rows && data.open.cols() == cols &&
        data.high.rows() == rows && data.high.cols() == cols &&
        data.low.rows() == rows && data.low.cols() == cols &&
        data.close.rows() == rows && data.close.cols() == cols &&
        data.volume.rows() == rows && data.volume.cols() == cols;

    if (!dimensions_match) {
        throw std::invalid_argument(
            "OHLCV matrices have inconsistent dimensions"
        );
    }
}

void validateOHLCV(const OHLCVData& data) {
    const bool all_finite =
        data.open.allFinite() &&
        data.high.allFinite() &&
        data.low.allFinite() &&
        data.close.allFinite() &&
        data.volume.allFinite();

    if (!all_finite) {
        throw std::invalid_argument(
            "OHLCV data must consist of finite values"
        );
    }

    const bool prices_positive =
        (data.open.array() > 0.0).all() &&
        (data.high.array() > 0.0).all() &&
        (data.low.array() > 0.0).all() &&
        (data.close.array() > 0.0).all();

    if (!prices_positive) {
        throw std::invalid_argument(
            "OHLC prices must be strictly positive"
        );
    }

    if (!(data.volume.array() >= 0.0).all()) {
        throw std::invalid_argument(
            "Volume must not be negative"
        );
    }

    const auto open_close_max = data.open.array().max(data.close.array());
    if (!(data.high.array() >= open_close_max).all()) {
        throw std::invalid_argument(
            "High must not be below open or close"
        );
    }

    const auto open_close_min = data.open.array().min(data.close.array());
    if (!(data.low.array() <= open_close_min).all()) {
        throw std::invalid_argument(
            "Low must not be above open or close"
        );
    }
}

void checkTimeIndex(
    std::size_t time_index,
    std::size_t num_dates
) {
    if (time_index >= num_dates) {
        throw std::out_of_range("time_index out of range");
    }
}

void checkInstrumentIndex(
    std::size_t instrument_index,
    std::size_t num_instruments
) {
    if (instrument_index >= num_instruments) {
        throw std::out_of_range("instrument_index out of range");
    }
}

} // namespace

HistoricalMarketData::HistoricalMarketData(
    std::vector<Date> dates,
    std::vector<std::string> instrument_ids,
    OHLCVData data
) {
    validateNonEmpty(dates, instrument_ids);
    validateInstrumentIds(instrument_ids);
    validateDatesAscending(dates);
    validateDimensions(data, dates.size(), instrument_ids.size());
    validateOHLCV(data);

    dates_ = std::move(dates);
    instrument_ids_ = std::move(instrument_ids);
    data_ = std::move(data);

    instrument_index_.reserve(instrument_ids_.size());
    for (std::size_t i = 0; i < instrument_ids_.size(); ++i) {
        instrument_index_.emplace(instrument_ids_[i], i);
    }
}

std::size_t HistoricalMarketData::numDates() const noexcept {
    return dates_.size();
}

std::size_t HistoricalMarketData::numInstruments() const noexcept {
    return instrument_ids_.size();
}

Date
HistoricalMarketData::date(std::size_t time_index) const {
    checkTimeIndex(time_index, dates_.size());
    return dates_[time_index];
}

Bar HistoricalMarketData::bar(
    std::size_t time_index,
    std::size_t instrument_index
) const {
    checkTimeIndex(time_index, dates_.size());
    checkInstrumentIndex(instrument_index, instrument_ids_.size());

    const auto t = static_cast<Eigen::Index>(time_index);
    const auto i = static_cast<Eigen::Index>(instrument_index);

    return Bar{
        data_.open(t, i),
        data_.high(t, i),
        data_.low(t, i),
        data_.close(t, i),
        data_.volume(t, i)
    };
}

Bar HistoricalMarketData::bar(
    std::size_t time_index,
    const std::string& instrument_id
) const {
    return bar(time_index, instrumentIndex(instrument_id));
}

double HistoricalMarketData::close(
    std::size_t time_index,
    std::size_t instrument_index
) const {
    checkTimeIndex(time_index, dates_.size());
    checkInstrumentIndex(instrument_index, instrument_ids_.size());

    return data_.close(
        static_cast<Eigen::Index>(time_index),
        static_cast<Eigen::Index>(instrument_index)
    );
}

std::size_t HistoricalMarketData::instrumentIndex(
    const std::string& instrument_id
) const {
    return instrument_index_.at(instrument_id);
}

const std::vector<std::string>&
HistoricalMarketData::instrumentIds() const noexcept {
    return instrument_ids_;
}
