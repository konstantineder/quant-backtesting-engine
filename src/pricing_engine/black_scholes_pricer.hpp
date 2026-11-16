#pragma once

#include "common/enums.hpp"
#include "pricing_engine/black_scholes_inputs.hpp"

class BlackScholesPricer {
public:
    static double price(OptionType type, const BlackScholesInputs& inputs);
};
