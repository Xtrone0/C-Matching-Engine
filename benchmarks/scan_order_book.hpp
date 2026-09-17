#pragma once
#include "order_book.hpp"
#include <algorithm>
// Cancellation baseline recovered from previon commit
// Everything else is inherited
class ScanOrderBook : public OrderBook
{
    // Tries to find the order with ID from the given side.
    template <class T>
    bool cancelSide(T &side, OrderId id)
    {
        for (auto &[price, orders] : side)
        {
            auto it = std::find_if(begin(orders), end(orders),
                                   [id](const Order &ord)
                                   {
                                       return ord.id == id;
                                   });
            if (it == end(orders))
                continue;
            active.erase(it->id);
            orders.erase(it);
            if (orders.empty())
                side.erase(price);
            return true;
        }
        return false;
    }

public:
    bool cancel(OrderId id)
    {
        if (cancelSide(bids, id) or cancelSide(asks, id))
            return true;
        return false;
    }
};
