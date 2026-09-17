#pragma once
#include "order_book.hpp"
#include <stdexcept>

// Scenario tests use this wrapper; the benchmark still uses the plain core.
// Check successful commands and ordinary validation rejection. Allocation
// failures do not promise rollback or a reusable engine instance.
class CheckedOrderBook : public OrderBook {
    template<class Action>
    auto checked(Action action) {
        try {
            auto result = action();
            assert_invariants();
            return result;
        } catch (const std::invalid_argument&) {
            assert_invariants();
            throw;
        }
    }

public:
    CheckedOrderBook() { assert_invariants(); }
    std::vector<Trade> submit(Order order) {
        return checked([&] { return OrderBook::submit(order); });
    }
    std::vector<Trade> submit_market(OrderId id, Side side, Quantity quantity) {
        return checked([&] { return OrderBook::submit_market(id, side, quantity); });
    }
    bool cancel(OrderId id) {
        return checked([&] { return OrderBook::cancel(id); });
    }
    std::vector<Trade> amend(OrderId id, Price newPrice, Quantity newRemaining) {
        return checked([&] { return OrderBook::amend(id, newPrice, newRemaining); });
    }
};
