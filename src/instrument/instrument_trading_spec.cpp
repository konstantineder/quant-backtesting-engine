#include "instrument/instrument_trading_spec.hpp"

#include <cmath>
#include <stdexcept>
#include <type_traits>
#include <variant>

InstrumentTradingSpec::InstrumentTradingSpec(
    double contract_multiplier,
    SettlementConvention settlement
): contract_multiplier_(contract_multiplier),
   settlement_(settlement) {
    if (!std::isfinite(contract_multiplier_) ||
        contract_multiplier_ <= 0.0) {
        throw std::invalid_argument(
            "Contract multiplier must be finite and positive"
        );
    }
}

InstrumentTradingSpec InstrumentTradingSpec::forInstrument(
    const Instrument& instrument
) {
    return std::visit(
        [](const auto& inst) -> InstrumentTradingSpec {
            using T = std::decay_t<decltype(inst)>;

            if constexpr (std::is_same_v<T, Equity> ||
                          std::is_same_v<T, EuropeanOption>) {
                return InstrumentTradingSpec{
                    inst.contractMultiplier(),
                    SettlementConvention::PREMIUM
                };
            } else {
                static_assert(
                    std::is_same_v<T, Future>,
                    "Unhandled instrument type in trading spec"
                );
                throw std::invalid_argument(
                    "Margin-settled instruments are not supported: " +
                    inst.instrumentID()
                );
            }
        },
        instrument
    );
}

std::vector<InstrumentTradingSpec> makeTradingSpecs(
    const InstrumentUniverse& universe
) {
    std::vector<InstrumentTradingSpec> specs;
    specs.reserve(universe.size());

    for (std::size_t offset = 0; offset < universe.size(); ++offset) {
        specs.push_back(
            InstrumentTradingSpec::forInstrument(
                universe.instrument(InstrumentIndex{offset})
            )
        );
    }

    return specs;
}

double settlementCashFlow(
    const InstrumentTradingSpec& spec,
    double signed_quantity,
    double unit_price,
    double fee
) {
    if (spec.settlement() != SettlementConvention::PREMIUM) {
        throw std::logic_error(
            "Only premium settlement is implemented"
        );
    }

    if (!std::isfinite(signed_quantity) || signed_quantity == 0.0) {
        throw std::invalid_argument(
            "Settlement quantity must be finite and non-zero"
        );
    }
    if (!std::isfinite(unit_price) || unit_price <= 0.0) {
        throw std::invalid_argument(
            "Settlement price must be finite and positive"
        );
    }
    if (!std::isfinite(fee) || fee < 0.0) {
        throw std::invalid_argument(
            "Settlement fee must be finite and non-negative"
        );
    }

    const double notional =
        signed_quantity * unit_price * spec.contractMultiplier();

    // Buying pays notional out, selling takes it in; the fee always costs.
    const double cash_flow = -notional - fee;

    if (!std::isfinite(cash_flow)) {
        throw std::overflow_error(
            "Settlement cash flow cannot be represented"
        );
    }

    return cash_flow;
}
