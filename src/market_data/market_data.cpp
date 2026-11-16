#include "market_data/market_data.hpp"
#include <cmath>
#include <stdexcept>
#include <string>

void MarketSnapshot::updatePrice(
    const std::string& symbol,
    double price
) {
    if (symbol.empty()) {
        throw std::invalid_argument(
            "Market data symbol must not be empty"
        );
    }

    if (!std::isfinite(price) || price <= 0.0) {
        throw std::invalid_argument(
            "Market price must be positive and finite"
        );
    }

    prices_.insert_or_assign(symbol, price);
}

double MarketSnapshot::getPrice(const std::string& symbol) const {
    return prices_.at(symbol);
}

bool MarketSnapshot::hasPrice(const std::string& symbol) const {
    return prices_.find(symbol) != prices_.end();
}
