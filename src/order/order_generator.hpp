#pragma once

#include "account/account.hpp"
#include "strategy/strategy.hpp"
#include "instrument/instrument_id.hpp"
#include "order/order.hpp"
#include "order/order_id.hpp"
#include "order/order_instruction.hpp"
#include "order/pending_order_book.hpp"
#include "analytics/account_valuator.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <variant>
#include <vector>

// Turns a strategy's portfolio target into the plan that would get the
// account there: per slot it reconciles the target against both the current
// position and whatever is still working in the PendingOrderBook, and emits
// cancellations and new orders accordingly.
//
// Prices are passed in as unit prices, one per universe slot, aligned with
// InstrumentIndex -- the same span AccountValuator::priceUniverse fills, so
// a bar prices its universe once and every consumer reads that one result.
// A slot that is both flat and not targeted never has its price read, which
// keeps a missing quote on an untouched instrument from failing the bar.
//
// The plan is pure: nothing here mutates the Account or the order book. Its
// contract is that applying it in order leaves every slot at
//
//     position + pending == target
//
// within the numeric tolerance, with already executed fills untouched.
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

    std::vector<OrderInstruction> generate(
        const PortfolioTarget& target,
        const Account& account,
        std::span<const double> unit_prices,
        const PendingOrderBook& pending_orders
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

        // Cancellations are collected apart from submissions so that every
        // cancel precedes every submit in the returned plan. Applying a
        // replacement while the order it replaces is still working would
        // be rejected by the book as a conflicting order.
        std::vector<OrderInstruction> cancellations;
        std::vector<OrderInstruction> submissions;
        submissions.reserve(slots);

        // Ids are drawn from a copy of the sequence and committed only
        // once every order has been built, so a throwing generate() leaves
        // the generator exactly as it was.
        OrderIdSequence ids = order_ids_;

        for (std::size_t slot = 0; slot < slots; ++slot) {
            const auto index = InstrumentIndex{slot};

            const double target_quantity = target_quantities[slot];
            const double position_quantity =
                account.position(index).quantity();
            const double pending_quantity =
                pending_orders.pendingQuantity(index);

            // What the target still needs beyond the executed position ...
            const double required_quantity =
                target_quantity - position_quantity;
            // ... and what it needs beyond the orders already working.
            const double additional_quantity =
                required_quantity - pending_quantity;

            // Case A: position plus pending already meets the target.
            if (!isTradable(additional_quantity, target_quantity)) {
                continue;
            }

            // Case B: nothing working, or what is working points the same
            // way as what is still missing -- top it up with one order.
            if (pending_quantity == 0.0 ||
                std::signbit(additional_quantity) ==
                    std::signbit(pending_quantity)) {
                submissions.push_back(SubmitOrder{
                    MarketOrder{ids.next(), index, additional_quantity}
                });
                continue;
            }

            // Case C: the working orders overshoot the target or point the
            // wrong way. They cannot be trimmed, so they are withdrawn and
            // replaced by a single order for the whole remaining need --
            // required_quantity, which already excludes what has been
            // filled, not additional_quantity.
            for (const OrderState& state : pending_orders.orders()) {
                if (state.isTerminal() || state.instrument() != index) {
                    continue;
                }
                cancellations.push_back(CancelOrder{state.orderId()});
            }

            // Cancelling alone may already be the whole plan: a slot whose
            // position is on target only needs its working orders gone.
            if (isTradable(required_quantity, target_quantity)) {
                submissions.push_back(SubmitOrder{
                    MarketOrder{ids.next(), index, required_quantity}
                });
            }
        }

        // The merge allocates, so it has to succeed before the sequence is
        // committed: handing out ids for a plan the caller never receives
        // would leave a gap no rollback could close. OrderIdSequence
        // assignment is noexcept, so this order makes generate() all or
        // nothing with respect to the generator's state.
        cancellations.reserve(cancellations.size() + submissions.size());
        cancellations.insert(
            cancellations.end(),
            std::make_move_iterator(submissions.begin()),
            std::make_move_iterator(submissions.end())
        );

        order_ids_ = ids;

        return cancellations;
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
