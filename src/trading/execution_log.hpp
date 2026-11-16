#pragma once

#include "trading/fill.hpp"

#include <cstddef>
#include <span>
#include <stdexcept>
#include <vector>

// Append-only record of every fill, kept deliberately independent of the
// Account: trade analytics, debugging and deterministic replay read the log
// without touching account state, and replaying the log into a fresh
// Account must reproduce it exactly.
//
// One log per worker, like the Account it accompanies.
class ExecutionLog {
public:
    void reserve(std::size_t capacity) { fills_.reserve(capacity); }

    void record(const Fill& fill) { fills_.push_back(fill); }

    std::span<const Fill> fills() const noexcept { return fills_; }

    const Fill& at(std::size_t index) const {
        if (index >= fills_.size()) {
            throw std::out_of_range("Fill index out of range");
        }
        return fills_[index];
    }

    std::size_t size() const noexcept { return fills_.size(); }
    bool empty() const noexcept { return fills_.empty(); }
    void clear() noexcept { fills_.clear(); }

private:
    std::vector<Fill> fills_;
};
