#pragma once
#include "command_runner.hpp"
#include "../tests/reference_book.hpp"
#include <random>
#include <string>

namespace benchmark {
enum class Family { Submissions, Mixed, CancellationHeavy, Deep, SinglePrice };
inline const char* name(Family family) {
    switch (family) {
    case Family::Submissions: return "submissions";
    case Family::Mixed: return "mixed";
    case Family::CancellationHeavy: return "cancellation-heavy";
    case Family::Deep: return "deep-noncrossing";
    case Family::SinglePrice: return "single-price";
    }
    throw std::invalid_argument("Unknown workload family");
}
struct Workload {
    std::string name;
    std::vector<replay::Command> commands; // setup prefix followed by measured commands
    std::size_t setup = 0;
};

// The reference supplies active IDs only during generation, outside all timing.
inline Workload generate(Family family, std::size_t count, std::size_t initial, std::uint64_t seed) {
    if (count == 0 || count > 100'000 || initial > 100'000)
        throw std::invalid_argument("Workload counts must fit the 100,000-order bound");
    Workload workload{std::string(name(family)) + "-" + std::to_string(seed), {}, 0};
    test_support::ReferenceBook state;
    std::mt19937_64 rng(seed);
    OrderId nextId = 1;
    auto append = [&](replay::Command command) {
        workload.commands.push_back(command);
        (void)replay::execute(state, command);
    };
    auto passivePrice = [&](Side side, OrderId offset) -> Price {
        if (family == Family::Deep)
            return side == Side::Buy ? 100'000 - static_cast<Price>(offset % 90'000)
                                     : 200'000 + static_cast<Price>(offset % 90'000);
        if (family == Family::SinglePrice) return 100;
        return side == Side::Buy ? 99 : 101;
    };
    if (family == Family::Submissions) initial = 0;
    for (std::size_t i = 0; i < initial; ++i) {
        const auto side = family == Family::SinglePrice || i % 2 == 0 ? Side::Buy : Side::Sell;
        append({replay::Action::Limit, nextId++, side, passivePrice(side, i), 10});
    }
    workload.setup = workload.commands.size();
    for (std::size_t i = 0; i < count; ++i) {
        auto side = rng() % 2 ? Side::Buy : Side::Sell;
        if (family == Family::SinglePrice) side = Side::Buy;
        const auto p = family == Family::Mixed || family == Family::Submissions
            ? 97 + static_cast<Price>(rng() % 7) : passivePrice(side, nextId);
        replay::Command command{replay::Action::Limit, nextId++, side, p, 1 + rng() % 20};
        const auto choice = rng() % 100;
        const auto active = !state.resting.empty();
        const Order target = active ? state.resting[rng() % state.resting.size()] : Order{};
        int cancelEnd = 0, amendEnd = 0, marketEnd = 0;
        switch (family) {
        case Family::Submissions: break;
        case Family::CancellationHeavy: cancelEnd = 70; amendEnd = 80; marketEnd = 80; break;
        case Family::Deep: cancelEnd = 25; amendEnd = 50; marketEnd = 50; break;
        case Family::SinglePrice: cancelEnd = 15; amendEnd = 35; marketEnd = 50; break;
        case Family::Mixed: cancelEnd = 20; amendEnd = 40; marketEnd = 55; break;
        }
        if (choice < static_cast<unsigned>(cancelEnd)) {
            command.action = replay::Action::Cancel;
            command.id = active && rng() % 10 != 0 ? target.id : nextId++;
        } else if (choice < static_cast<unsigned>(amendEnd)) {
            command.action = replay::Action::Amend;
            command.id = active ? target.id : nextId++;
            command.price = active ? target.price : p;
            command.quantity = active ? target.quantity : 1;
            if (active) {
                switch (rng() % 4) {
                case 0: break;
                case 1: command.quantity = target.quantity > 1 ? target.quantity - 1 : 1; break;
                case 2: command.quantity += 2; break;
                case 3:
                    if (family == Family::Mixed)
                        command.price = target.price == 103 ? 97 : target.price + 1;
                    else if (family == Family::Deep)
                        command.price = passivePrice(target.side, nextId);
                    else command.quantity += 1;
                    break;
                }
            }
        } else if (choice < static_cast<unsigned>(marketEnd)) {
            command.action = replay::Action::Market;
            if (family == Family::SinglePrice) command.side = Side::Sell;
        } else if (family == Family::Mixed && choice >= 95) {
            command.quantity = 0;
        }
        // Never exceed the agreed maximum live book size, including growing workloads.
        if (state.resting.size() >= 100'000 && command.action == replay::Action::Limit) {
            command.action = replay::Action::Cancel;
            command.id = state.resting.front().id;
        }
        append(command);
    }
    return workload;
}
} // namespace benchmark
