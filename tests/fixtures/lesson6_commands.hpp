#pragma once
#include "order.hpp"
#include <array>

namespace testfixtures {
enum class Action { Limit, Market, Cancel };
struct Command {
    Action action;
    OrderId id;
    Side side;
    Price price;
    Quantity quantity;
};

// Concrete inputs retained for L11 replay. Cancel ignores side/price/quantity;
// Market ignores price. Expected trades/rejections live independently in tests.
inline constexpr std::array<Command, 20> lesson6_commands{{
    {Action::Limit, 1, Side::Buy, 99, 4},
    {Action::Limit, 2, Side::Buy, 99, 6},
    {Action::Limit, 3, Side::Buy, 98, 8},
    {Action::Limit, 4, Side::Sell, 101, 3},
    {Action::Limit, 5, Side::Sell, 102, 5},
    {Action::Limit, 2, Side::Sell, 98, 2}, // Reject duplicate before crossing.
    {Action::Limit, 6, Side::Buy, 102, 5}, // Sweep leaving a partial maker.
    {Action::Cancel, 1, Side::Buy, 0, 0},
    {Action::Market, 7, Side::Sell, 0, 5},
    {Action::Cancel, 2, Side::Buy, 0, 0},
    {Action::Limit, 2, Side::Buy, 98, 2}, // Reused ID joins behind ID 3.
    {Action::Market, 8, Side::Sell, 0, 20}, // Ten units expire.
    {Action::Cancel, 8, Side::Buy, 0, 0}, // Expired order never rested.
    {Action::Limit, 8, Side::Sell, 103, 1},
    {Action::Cancel, 999, Side::Buy, 0, 0},
    {Action::Limit, 9, Side::Buy, 100, 0},
    {Action::Market, 0, Side::Buy, 0, 1},
    {Action::Market, 10, Side::Buy, 0, 10}, // Six expire; book empties.
    {Action::Limit, 1, Side::Buy, 100, 1},
    {Action::Cancel, 1, Side::Buy, 0, 0}
}};
}
