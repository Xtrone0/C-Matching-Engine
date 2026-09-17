#pragma once
#include "order.hpp"
#include <algorithm>
#include <cassert>
#include <deque>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <set>
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
class OrderBook
{
    std::map<Price, std::list<Order>, std::greater<Price>> bids;
    std::map<Price, std::list<Order>> asks;
    std::unordered_set<OrderId> usedids;
    Priority priorityCounter = 0;
    // check
    template <class T>
    bool cancelSide(T &type, OrderId id);

    template <class Levels>
    std::vector<Trade> match(Order &incoming, Levels &opposite, std::optional<Price> limit);
    // optional<Trade> execute(OrderId id);
    template <class T>
    void removeEmpty(T &type);
    void rest(Order order);
    template <class T>
    std::vector<SnapshotLevel> snapside(const T &side) const;
    void validateOrder(const Order &order) const;
    template <class T>
    void assert_invariantsLevel(const T &side, const Side type, std::unordered_set<OrderId> &observed) const;

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
};
