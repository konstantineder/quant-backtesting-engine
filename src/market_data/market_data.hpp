#pragma once

#include <string>
#include <unordered_map>

class MarketSnapshot {
public:
    void updatePrice(const std::string& symbol, double price);
    double getPrice(const std::string& symbol) const;
    bool hasPrice(const std::string& symbol) const;

private:
    std::unordered_map<std::string, double> prices_;
};

