#pragma once
#include "order_book.hpp"
#include <iterator>
#include <stdexcept>
#include <utility>
// Narrow white-box access for invariant rejection tests; no layout casts or
// private/public macros, and no public production mutation API.
struct OrderBookTestAccess
{
    static auto &orders(OrderBook &book, Side side, Price price)
    {
        return side == Side::Buy ? book.bids.at(price) : book.asks.at(price);
    }
    static Order &order(OrderBook &book, Side side, Price price, std::size_t offset = 0)
    {
        return *std::next(orders(book, side, price).begin(), offset);
    }
    static auto &ids(OrderBook &book)
    {
        return book.usedids;
    }
    static Priority &counter(OrderBook &book)
    {
        return book.priorityCounter;
    }
    static Quantity add(Quantity total, Quantity quantity)
    {
        return OrderBook::checkedInvariantTotal(total, quantity);
    }
    static void move_ask_level(OrderBook &book, Price from, Price to)
    {
        auto level = book.asks.extract(from);
        if (level.empty())
            throw std::logic_error("Missing test price level");
        level.key() = to;
        for (auto &order : level.mapped())
            order.price = to;
        book.asks.insert(std::move(level));
    }
};
