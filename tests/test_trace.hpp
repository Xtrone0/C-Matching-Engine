#pragma once
#include "order_book.hpp"
#include <charconv>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace test_trace {
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

inline const char* name(Action action) {
    switch (action) {
    case Action::Limit: return "NEW";
    case Action::Market: return "MARKET";
    case Action::Cancel: return "CANCEL";
    case Action::Amend: return "AMEND";
    }
    throw std::runtime_error("Unknown trace action");
}

// Every field is retained, including ignored fields and invalid domain values.
// This is a test trace format, separate from the eventual public replay grammar.
inline void write(std::ostream& out, const std::vector<Command>& commands) {
    out << "LOB-TEST-TRACE 1\nINITIAL EMPTY\nLIMITS 1000000000 1000000000\n";
    std::size_t sequence = 0;
    for (const auto& command : commands)
        out << ++sequence << ' ' << name(command.action) << ' ' << command.id << ' '
            << static_cast<int>(command.side) << ' ' << command.price << ' ' << command.quantity << '\n';
    if (!out) throw std::runtime_error("Cannot write test trace");
}

template<class Number>
Number number(const std::string& token) {
    Number value{};
    const auto [end, error] = std::from_chars(token.data(), token.data() + token.size(), value);
    if (error != std::errc{} || end != token.data() + token.size())
        throw std::runtime_error("Invalid or overflowing numeric token: " + token);
    return value;
}

inline std::vector<Command> read(std::istream& in) {
    std::vector<Command> commands;
    std::string line;
    std::size_t lineNumber = 0, header = 0;
    while (std::getline(in, line)) {
        ++lineNumber;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.find_first_not_of(" \t") == std::string::npos) continue;
        try {
            if (header < 3) {
                const char* expected[] = {"LOB-TEST-TRACE 1", "INITIAL EMPTY", "LIMITS 1000000000 1000000000"};
                if (line != expected[header]) throw std::runtime_error("Unsupported trace header or configuration");
                ++header;
                continue;
            }
            std::istringstream fields(line);
            std::string sequence, action, id, side, price, quantity, extra;
            if (!(fields >> sequence >> action >> id >> side >> price >> quantity) || fields >> extra)
                throw std::runtime_error("Expected six command fields");
            if (number<std::size_t>(sequence) != commands.size() + 1)
                throw std::runtime_error("Command sequences must start at 1 and be contiguous");
            Action kind;
            if (action == "NEW") kind = Action::Limit;
            else if (action == "MARKET") kind = Action::Market;
            else if (action == "CANCEL") kind = Action::Cancel;
            else if (action == "AMEND") kind = Action::Amend;
            else throw std::runtime_error("Unknown command: " + action);
            commands.push_back({kind, number<OrderId>(id), static_cast<Side>(number<int>(side)),
                                number<Price>(price), number<Quantity>(quantity)});
        } catch (const std::exception& error) {
            throw std::runtime_error("Trace line " + std::to_string(lineNumber) + ": " + error.what());
        }
    }
    if (in.bad()) throw std::runtime_error("Cannot read test trace");
    if (header != 3) throw std::runtime_error("Incomplete trace header");
    return commands;
}

inline std::vector<Command> load(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Cannot open trace: " + path.string());
    return read(input);
}
inline void save(const std::filesystem::path& path, const std::vector<Command>& commands) {
    std::ofstream output(path);
    if (!output) throw std::runtime_error("Cannot create trace: " + path.string());
    write(output, commands);
    output.flush();
    if (!output) throw std::runtime_error("Cannot flush trace: " + path.string());
}

inline void describe(std::ostream& out, const CommandResult& result) {
    out << "rejected=" << result.rejected << " canceled=" << result.canceled << '\n';
    for (const auto& trade : result.trades)
        out << "TRADE " << trade.buy_id << ' ' << trade.sell_id << ' ' << trade.price << ' ' << trade.quantity << '\n';
}
inline void describe(std::ostream& out, const Snapshot& snapshot) {
    auto levels = [&](const auto& values, const char* side) {
        for (const auto& level : values) {
            out << side << " LEVEL " << level.price << ' ' << level.quantity << '\n';
            for (const auto& order : level.orders)
                out << "ORDER " << order.id << ' ' << static_cast<int>(order.side) << ' '
                    << order.price << ' ' << order.quantity << ' ' << order.priority << '\n';
        }
    };
    levels(snapshot.bids, "BUY");
    levels(snapshot.asks, "SELL");
}

// Predicate always replays from a fresh book and checks a specific mismatch.
// Greedy chunk deletion and monotonic field shrinking; not a global-minimum proof.
template<class Predicate>
std::vector<Command> minimize(std::vector<Command> commands, Predicate reproduces) {
    if (!reproduces(commands)) throw std::runtime_error("Input does not reproduce the selected failure");
    auto removeChunks = [&] {
        bool any = false;
        for (std::size_t width = commands.size(); width > 0; width /= 2) {
            for (std::size_t start = 0; start < commands.size();) {
                auto candidate = commands;
                const auto finish = std::min(start + width, candidate.size());
                candidate.erase(candidate.begin() + static_cast<std::ptrdiff_t>(start),
                                candidate.begin() + static_cast<std::ptrdiff_t>(finish));
                if (reproduces(candidate)) { commands = std::move(candidate); any = true; }
                else start += width;
            }
        }
        return any;
    };
    bool changed;
    do {
        changed = removeChunks();
        for (std::size_t i = 0; i < commands.size(); ++i) {
            // Rename an ID throughout the trace so its cancellation/amendment can survive.
            const auto oldId = commands[i].id;
            for (OrderId id : {OrderId{1}, oldId / 2}) {
                if (id == 0 || id >= commands[i].id) continue;
                auto candidate = commands;
                const auto original = commands[i].id;
                for (auto& command : candidate) if (command.id == original) command.id = id;
                if (reproduces(candidate)) { commands = std::move(candidate); changed = true; }
            }
            const auto oldPrice = commands[i].price;
            for (Price price : {Price{0}, Price{1}, oldPrice / 2}) {
                const auto current = commands[i].price;
                // Move toward zero without abs(INT64_MIN) or signed overflow.
                if (current >= 0 ? (price < 0 || price >= current) : (price > 0 || price <= current)) continue;
                auto candidate = commands;
                candidate[i].price = price;
                if (reproduces(candidate)) { commands = std::move(candidate); changed = true; }
            }
            const auto oldQuantity = commands[i].quantity;
            for (Quantity quantity : {Quantity{0}, Quantity{1}, oldQuantity / 2}) {
                if (quantity >= commands[i].quantity) continue;
                auto candidate = commands;
                candidate[i].quantity = quantity;
                if (reproduces(candidate)) { commands = std::move(candidate); changed = true; }
            }
        }
    } while (changed);
    return commands;
}
} // namespace test_trace
