#pragma once

#include <chrono>
#include <stdexcept>
#include <string>
#include <variant>
#include <cmath>

#include "common/enums.hpp"

class Equity {
public:
    explicit Equity(const std::string& id, const std::string& ticker) : instrument_id_(id), ticker_(ticker) { validate(); }
    
    const std::string& instrumentID() const { return instrument_id_; }
    const std::string& ticker() const { return ticker_; }
    double contractMultiplier() const noexcept { return 1.; }
private:
    void validate() const {
        if (instrument_id_.empty()) {
            throw std::invalid_argument("Instrument ID must not be empty.");
        }
        if (ticker_.empty()) {
            throw std::invalid_argument("Ticker must not be empty.");
        }
    }

    std::string instrument_id_;
    std::string ticker_;
};

class EuropeanOption {
public:
    explicit EuropeanOption(
        const std::string& instrument_id,
        const std::string& underlying_id,
        double strike,
        const std::chrono::sys_days& start_date,
        const std::chrono::sys_days& expiry_date,
        OptionType option_type,
        double contract_multiplier
    ) : instrument_id_(instrument_id),
        underlying_id_(underlying_id),
        strike_(strike),
        start_date_(start_date),
        expiry_date_(expiry_date),
        option_type_(option_type),
        contract_multiplier_(contract_multiplier) {
            validate();
    }

    const std::string& instrumentID() const noexcept { return instrument_id_; }
    const std::string& underlyingID() const noexcept { return underlying_id_; }
    double strike() const noexcept { return strike_; }
    std::chrono::sys_days startDate() const noexcept { return start_date_; }
    std::chrono::sys_days expiryDate() const noexcept { return expiry_date_; }
    OptionType optionType() const noexcept { return option_type_; }
    double contractMultiplier() const noexcept { return contract_multiplier_; }

private:
    void validate() const {
        if (instrument_id_.empty()) {
            throw std::invalid_argument("Instrument ID must not be empty.");
        }
        if (underlying_id_.empty()) {
            throw std::invalid_argument("Underlying ID must not be empty.");
        }
        if (strike_ <= 0.0 || !std::isfinite(strike_)) {
            throw std::invalid_argument("Strike price must be positive.");
        }
        if (contract_multiplier_ <= 0.0 || !std::isfinite(contract_multiplier_)) {
            throw std::invalid_argument("Contract multiplier must be positive.");
        }
        if (expiry_date_ <= start_date_) {
            throw std::invalid_argument("Expiry date must be after start date.");
        }
        if (option_type_ != OptionType::CALL && option_type_ != OptionType::PUT) {
            throw std::invalid_argument("Option type must be either CALL or PUT.");
        }
    }

    std::string instrument_id_;
    std::string underlying_id_;
    double strike_;
    std::chrono::sys_days start_date_;
    std::chrono::sys_days expiry_date_;
    OptionType option_type_;
    double contract_multiplier_;
};

class Future {
public:
    explicit Future(
        const std::string& instrument_id,
        const std::string& underlying_id,
        double contract_multiplier,
        const std::chrono::sys_days& start_date,
        const std::chrono::sys_days& expiry_date
    ) : instrument_id_(instrument_id),
        underlying_id_(underlying_id),
        contract_multiplier_(contract_multiplier),
        start_date_(start_date),
        expiry_date_(expiry_date) {
            validate();
    }

    const std::string& instrumentID() const noexcept { return instrument_id_; }
    const std::string& underlyingID() const noexcept { return underlying_id_; }
    double contractMultiplier() const noexcept { return contract_multiplier_; }
    std::chrono::sys_days startDate() const noexcept { return start_date_; }
    std::chrono::sys_days expiryDate() const noexcept { return expiry_date_; }

private:
    void validate() const {
        if (instrument_id_.empty()) {
            throw std::invalid_argument("Instrument ID must not be empty.");
        }
        if (underlying_id_.empty()) {
            throw std::invalid_argument("Underlying ID must not be empty.");
        }
        if (contract_multiplier_ <= 0.0 || !std::isfinite(contract_multiplier_)) {
            throw std::invalid_argument("Contract multiplier must be positive.");
        }
        if (expiry_date_ <= start_date_) {
            throw std::invalid_argument("Expiry date must be after start date.");
        }
    }

    std::string instrument_id_;
    std::string underlying_id_;
    double contract_multiplier_;
    std::chrono::sys_days start_date_;
    std::chrono::sys_days expiry_date_;
};

using Instrument = std::variant<
    Equity,
    EuropeanOption,
    Future
>;
