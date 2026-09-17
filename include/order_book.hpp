#pragma once
#include "order.hpp"
#include <algorithm>
#include <deque>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>
using std::optional;
struct Trade
{
    OrderId buy_id, sell_id;
    Price price;
    Quantity quantity;
};
struct SnapshotLevel
{
    Price price;
    Quantity quantity;
    std::vector<Order> orders;
};
struct Snapshot
{
    std::vector<SnapshotLevel> bids;
    std::vector<SnapshotLevel> asks;
};
struct PriceCompare
{
    bool descending;
    bool operator()(Price a, Price b) const
    {
        return descending ? a > b : a < b;
    }
};
using Orders = std::list<Order>;
using Levels = std::map<Price, Orders, PriceCompare>;
struct Location
{
    Side side;
    Levels::iterator level;
    Orders::iterator order;
};
class OrderBook
{
    // Defined only by regression tests to exercise corrupted internal states.
    friend struct OrderBookTestAccess;
    Levels bids{PriceCompare{true}};
    Levels asks{PriceCompare{false}};
    std::unordered_map<OrderId, Location> active;

    Priority priorityCounter = 0;
    template <class Levels>
    std::vector<Trade> match(Order &incoming, Levels &opposite, std::optional<Price> limit);
    // optional<Trade> execute(OrderId id);
    template <class T>
    void removeEmpty(T &type);
    template <class T>
    void rest(Order order, T &side);
    template <class T>
    std::vector<SnapshotLevel> snapside(const T &side) const;
    void validateOrder(const Order &order) const;
    template <class T>
    void assert_invariantsLevel(const T &side, const Side type, std::unordered_set<OrderId> &observed) const;
    static Quantity checkedInvariantTotal(Quantity total, Quantity quantity);

public:
    optional<Price> best_bid() const;
    optional<Price> best_ask() const;
    bool cancel(OrderId id);
    std::vector<Trade> submit(Order order);
    Snapshot snapshot() const;
    void assert_invariants() const;

    std::vector<Trade> submit_market(
        OrderId id,
        Side side,
        Quantity quantity);
    OrderBook() = default;

    OrderBook(const OrderBook &) = delete;
    OrderBook &operator=(const OrderBook &) = delete;

    OrderBook(OrderBook &&) = delete;
    OrderBook &operator=(OrderBook &&) = delete;
};
