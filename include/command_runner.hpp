#pragma once
#include "order_book.hpp"
#include <span>
#include <stdexcept>
#include <vector>

namespace replay {
enum class Action { Limit, Market, Cancel, Amend };
struct Command {
    Action action;
    OrderId id;
    Side side;
    Price price;
    Quantity quantity;
};
struct CommandResult {
    bool rejected = false;
    bool canceled = false;
    std::vector<Trade> trades;
};

// Works with an ordinary OrderBook. Tests may supply a checked or reference book.
// Only domain rejection is converted to a result; internal failures propagate.
template<class Book>
CommandResult execute(Book& book, const Command& command) {
    CommandResult result;
    try {
        switch (command.action) {
        case Action::Limit:
            result.trades = book.submit({command.id, command.side, command.price, command.quantity});
            break;
        case Action::Market:
            result.trades = book.submit_market(command.id, command.side, command.quantity);
            break;
        case Action::Cancel:
            result.canceled = book.cancel(command.id) != 0;
            break;
        case Action::Amend:
            result.trades = book.amend(command.id, command.price, command.quantity);
            break;
        default:
            throw std::invalid_argument("Unknown command action");
        }
    } catch (const std::invalid_argument&) {
        result.rejected = true;
    }
    return result;
}

// The supplied book is the initial state. Results retain command/trade order.
template<class Book>
std::vector<CommandResult> run(Book& book, std::span<const Command> commands) {
    std::vector<CommandResult> results;
    results.reserve(commands.size());
    for (const auto& command : commands) results.push_back(execute(book, command));
    return results;
}
} // namespace replay
