#pragma once

enum class OrderSide {
    BUY,
    SELL
};

enum class OptionType {
    CALL,
    PUT
};

enum class OrderStatus {
    PENDING, 
    PARTIALLY_FILLED, 
    FILLED, 
    CANCELLED
};
