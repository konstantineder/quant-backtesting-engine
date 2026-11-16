#pragma once

#include <cstddef>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>

// Strongly typed registry key. Wraps the string id used by
// InstrumentRegistry so that an id cannot be confused with a ticker, a
// trade id or any other string flying around the trading domain.
class InstrumentId {
public:
    explicit InstrumentId(std::string value): value_(std::move(value)) {
        if (value_.empty()) {
            throw std::invalid_argument(
                "InstrumentId must not be empty"
            );
        }
    }

    const std::string& str() const noexcept { return value_; }

    friend bool operator==(
        const InstrumentId& lhs,
        const InstrumentId& rhs
    ) noexcept {
        return lhs.value_ == rhs.value_;
    }

    friend auto operator<=>(
        const InstrumentId& lhs,
        const InstrumentId& rhs
    ) noexcept {
        return lhs.value_ <=> rhs.value_;
    }

private:
    std::string value_;
};

// Dense, universe-local position index. Deliberately not implicitly
// convertible to std::size_t: a raw integer must never be mistaken for a
// universe slot, and a slot from one universe must not be used with
// another.
class InstrumentIndex {
public:
    constexpr explicit InstrumentIndex(std::size_t value) noexcept
        : value_(value) {}

    constexpr std::size_t value() const noexcept { return value_; }

    friend constexpr bool operator==(
        InstrumentIndex,
        InstrumentIndex
    ) noexcept = default;

    friend constexpr auto operator<=>(
        InstrumentIndex,
        InstrumentIndex
    ) noexcept = default;

private:
    std::size_t value_;
};

template <>
struct std::hash<InstrumentId> {
    std::size_t operator()(const InstrumentId& id) const noexcept {
        return std::hash<std::string>{}(id.str());
    }
};

template <>
struct std::hash<InstrumentIndex> {
    std::size_t operator()(InstrumentIndex index) const noexcept {
        return std::hash<std::size_t>{}(index.value());
    }
};
