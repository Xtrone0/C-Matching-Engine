#pragma once
#include "order.hpp"
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <vector>
using std::optional;
struct Trade
{
    OrderId buy_id, sell_id;
    Price price;
    Quantity quantity;
};
class OrderBook
{
    std::map<Price, std::deque<Order>, std::greater<Price>> bids;
    std::map<Price, std::deque<Order>> asks;

public:
    optional<Price> best_bid() const;
    optional<Price> best_ask() const;
    bool cancel(OrderId id);
    std::vector<Trade> submit(Order order);
};
