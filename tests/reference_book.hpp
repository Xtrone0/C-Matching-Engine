#pragma once
#include "order_book.hpp"
#include <algorithm>
#include <stdexcept>
namespace test_support {
inline constexpr Price max_price = 1'000'000'000;
inline constexpr Quantity max_quantity = 1'000'000'000;
inline Order order(OrderId id, Side side, Price price, Quantity quantity) { return {id, side, price, quantity}; }
inline Trade execution(Side side, OrderId taker, OrderId maker, Price price, Quantity quantity) {
    return side == Side::Buy ? Trade{taker, maker, price, quantity} : Trade{maker, taker, price, quantity};
}
// Flat storage and independent maker selection. Shared by tests and untimed benchmark verification.
    class ReferenceBook
    {
        Priority next_priority = 0;

    public:
        std::vector<Order> resting;
        std::vector<Trade> submit(Order incoming)
        {
            return process(incoming, false);
        }
        std::vector<Trade> submit_market(OrderId id, Side side, Quantity quantity)
        {
            return process(order(id, side, 0, quantity), true);
        }
        std::vector<Trade> process(Order incoming, bool is_market)
        {
            if (incoming.id == 0 || (incoming.side != Side::Buy && incoming.side != Side::Sell) || incoming.quantity == 0 || incoming.quantity > max_quantity || (!is_market && (incoming.price < 1 || incoming.price > max_price)))
                throw std::invalid_argument("Invalid reference input");
            if (std::any_of(resting.begin(), resting.end(), [&](const auto &item)
                            {
                                return item.id == incoming.id;
                            }))
                throw std::invalid_argument("Duplicate reference ID");
            std::vector<Trade> result;
            while (incoming.quantity > 0)
            {
                auto best = resting.end();
                for (auto it = resting.begin(); it != resting.end(); ++it)
                {
                    if (it->side == incoming.side)
                        continue;
                    const bool crosses = is_market || (incoming.side == Side::Buy ? incoming.price >= it->price : incoming.price <= it->price);
                    if (!crosses)
                        continue;
                    if (best == resting.end() || (incoming.side == Side::Buy ? it->price < best->price : it->price > best->price))
                        best = it;
                }
                if (best == resting.end())
                    break;
                const Quantity q = std::min(incoming.quantity, best->quantity);
                result.push_back(execution(incoming.side, incoming.id, best->id, best->price, q));
                incoming.quantity -= q;
                best->quantity -= q;
                if (best->quantity == 0)
                    resting.erase(best);
            }
            if (!is_market && incoming.quantity > 0)
            {
                incoming.priority = ++next_priority;
                resting.push_back(incoming);
            }
            return result;
        }
        Snapshot snapshot() const
        {
            auto sorted = resting;
            std::stable_sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b)
                             {
                                 if (a.side != b.side)
                                     return a.side == Side::Buy;
                                 return a.side == Side::Buy ? a.price > b.price : a.price < b.price;
                             });
            Snapshot result;
            for (const auto &item : sorted)
            {
                auto &levels = item.side == Side::Buy ? result.bids : result.asks;
                if (levels.empty() || levels.back().price != item.price)
                    levels.push_back({item.price, 0, {}});
                levels.back().quantity += item.quantity; // Bounded test workloads.
                levels.back().orders.push_back(item);
            }
            return result;
        }
        Quantity cancel(OrderId id)
        {
            const auto it = std::find_if(resting.begin(), resting.end(),
                                         [id](const Order &item)
                                         {
                                             return item.id == id;
                                         });
            if (it == resting.end())
                return 0;
            const Quantity q = it->quantity;
            resting.erase(it);
            return q;
        }
        std::vector<Trade> amend(OrderId id, Price newPrice, Quantity newRemaining)
        {
            if (newPrice < 1 || newPrice > max_price || newRemaining == 0 || newRemaining > max_quantity)
                throw std::invalid_argument("Invalid reference amendment");
            const auto it = std::find_if(resting.begin(), resting.end(),
                [id](const Order& item) { return item.id == id; });
            if (it == resting.end()) throw std::invalid_argument("Unknown reference ID");
            if (newPrice == it->price && newRemaining <= it->quantity) {
                it->quantity = newRemaining;
                return {};
            }
            Order replacement{id, it->side, newPrice, newRemaining};
            resting.erase(it);
            return process(replacement, false);
        }
        std::optional<Price> best(Side side) const
        {
            std::optional<Price> result;
            for (const auto &item : resting)
            {
                if (item.side != side)
                    continue;
                if (!result || (side == Side::Buy ? item.price > *result : item.price < *result))
                    result = item.price;
            }
            return result;
        }
    };

} // namespace test_support
