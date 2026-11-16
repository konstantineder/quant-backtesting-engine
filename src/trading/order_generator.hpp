#pragma once

#include "backtesting/account/account.hpp"
#include "backtesting/strategy/strategy.hpp"
#include "trading/instrument_id.hpp"
#include "trading/order.hpp"
#include "trading/order_id.hpp"
#include "valuation/account_valuator.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <variant>
#include <vector>

// Turns a strategy's portfolio target into the orders that would get the
// account there: it diffs the target against the current positions and
// emits one MarketOrder per slot that is off target.
//
// Prices are passed in as unit prices, one per universe slot, aligned with
// InstrumentIndex -- the same span AccountValuator::priceUniverse fills, so
// a bar prices its universe once and every consumer reads that one result.
// A slot that is both flat and not targeted never has its price read, which
// keeps a missing quote on an untouched instrument from failing the bar.
//
// PRECONDITION: no orders are outstanding for this account. The delta is
// target minus current position, which double-counts anything still working
// in the PendingOrderBook. Netting against pending quantity is a separate
// concern and not implemented here.
//
// Stateful by design -- it hands out the OrderId sequence, so one generator
// per worker, never shared between parallel backtests.
class OrderGenerator {
public:
    OrderGenerator() = default;

    // Resumes an id sequence, so a restarted run does not reissue ids that
    // an earlier one already handed out.
    explicit OrderGenerator(OrderIdSequence order_ids)
        : order_ids_(order_ids) {}

    // One generator per worker: copying one would fork its id sequence and
    // hand out the same OrderId twice. Declaring these also suppresses the
    // implicit moves, which is intended -- a generator stays where it is.
    OrderGenerator(const OrderGenerator&) = delete;
    OrderGenerator& operator=(const OrderGenerator&) = delete;

    std::vector<Order> generate(
        const PortfolioTarget& target,
        const Account& account,
        std::span<const double> unit_prices
    ) {
        const std::size_t slots = account.size();

        if (unit_prices.size() != slots) {
            throw std::invalid_argument(
                "Unit prices and account differ in size"
            );
        }

        // Up front, before anything is read per slot: a target that does
        // not cover the account exactly, or that carries NaN or infinity,
        // must never reach the conversion loop.
        validateTarget(target, slots);

        const TargetQuantities target_quantities =
            resolve(target, account, unit_prices);

        std::vector<Order> orders;
        orders.reserve(slots);

        // Ids are drawn from a copy of the sequence and committed only
        // once every order has been built, so a throwing generate() leaves
        // the generator exactly as it was.
        OrderIdSequence ids = order_ids_;

        for (std::size_t slot = 0; slot < slots; ++slot) {
            const auto index = InstrumentIndex{slot};

            const double target_quantity = target_quantities[slot];
            const double current_quantity = account.position(index).quantity();
            const double quantity_delta = target_quantity - current_quantity;

            // A slot already on target produces no order. This is not an
            // optimization: MarketOrder rejects a zero quantity, so
            // emitting one here would throw.
            if (!isTradable(quantity_delta, target_quantity)) {
                continue;
            }

            orders.push_back(
                MarketOrder{ids.next(), index, quantity_delta}
            );
        }

        order_ids_ = ids;

        return orders;
    }

private:
    // Relative floor below which a delta is rounding noise rather than a
    // position to trade. Purely numeric -- the smallest quantity an
    // instrument may actually trade in is a venue rule that belongs in
    // InstrumentTradingSpec, not here. Note the consequence: on a target of
    // 1e9 units this suppresses deltas below 1e-3.
    static constexpr double kNegligibleDelta = 1e-12;

    static void validateTarget(
        const PortfolioTarget& target,
        std::size_t slots
    ) {
        std::visit(
            [&](const auto& values) {
                if (values.size() != slots) {
                    throw std::invalid_argument(
                        "Portfolio target does not cover every account slot"
                    );
                }

                for (std::size_t i = 0; i < values.size(); ++i) {
                    if (!std::isfinite(values[i])) {
                        throw std::invalid_argument(
                            "Portfolio target must be finite in every slot"
                        );
                    }
                }
            },
            target
        );
    }

    static bool isTradable(double delta, double scale) {
        if (!std::isfinite(delta) || !std::isfinite(scale)) {
            throw std::invalid_argument(
                "Target quantity does not yield a finite order quantity"
            );
        }
        return std::abs(delta) >
               kNegligibleDelta * std::max(1.0, std::abs(scale));
    }

    // Expects an already validated target.
    static TargetQuantities resolve(
        const PortfolioTarget& target,
        const Account& account,
        std::span<const double> unit_prices
    ) {
        return std::visit(
            [&](const auto& target_values) -> TargetQuantities {
                using T = std::decay_t<decltype(target_values)>;

                if constexpr (std::is_same_v<T, TargetQuantities>) {
                    return target_values;
                } else {
                    const std::size_t slots = target_values.size();

                    // Weights are shares of the whole portfolio, so they
                    // size off equity -- cash plus the marked-to-market
                    // positions -- not off cash alone. A fully invested
                    // account holds almost no cash, and sizing off that
                    // would liquidate it on every bar. The valuation comes
                    // from AccountValuator so the engine keeps exactly one
                    // definition of equity.
                    const double equity =
                        AccountValuator{}
                            .evaluateWithPrices(account, unit_prices)
                            .equity;

                    TargetQuantities target_quantities;
                    target_quantities.assign(slots, 0.0);

                    for (std::size_t i = 0; i < slots; ++i) {
                        const double weight = target_values[i];

                        // A zero weight is zero units whatever the price
                        // is, so an unpriceable slot must not fail here.
                        if (weight == 0.0) {
                            continue;
                        }

                        const auto index = InstrumentIndex{i};
                        const double spot = unit_prices[i];

                        if (!std::isfinite(spot) || spot <= 0.0) {
                            throw std::invalid_argument(
                                "A non-zero target weight needs a finite, "
                                "positive price"
                            );
                        }

                        target_quantities[i] =
                            weight * equity /
                            (spot * account.spec(index).contractMultiplier());
                    }

                    return target_quantities;
                }
            },
            target
        );
    }

    OrderIdSequence order_ids_;
};
