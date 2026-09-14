#include "../include/order_book.hpp"
using std::nullopt;
using std::vector;
optional<Price> OrderBook::best_bid() const
{
    if (!bids.size())
        return nullopt;
    return begin(bids)->first;
}
optional<Price> OrderBook::best_ask() const
{
    if (!asks.size())
        return nullopt;
    return begin(asks)->first;
}
template <class T>
bool OrderBook::cancelSide(T &side, OrderId id)
{
    for (auto &[price, orders] : side)
    {
        auto it = std::find_if(begin(orders), end(orders),
                               [id](const Order &ord)
                               {
                                   return ord.id == id;
                               }

        );
        if (it == end(orders))
            continue;
        orders.erase(it);
        if (!orders.size())
            side.erase(price);

        return true;
    }
    return false;
}
bool OrderBook::cancel(OrderId id)
{
    if (cancelSide(bids, id) or cancelSide(asks, id))
        return true;
    return false;
}
template <class T>
void OrderBook::removeEmpty(T &type)
{
    if (type.size())
    {
        auto &orders = begin(type)->second;
        if (orders[0].quantity == 0)
        {
            orders.erase(begin(orders));
        }
        if (orders.size() == 0)
        {
            type.erase(begin(type));
        }
    }
}
optional<Trade> OrderBook::execute()
{
    auto bestbid = best_bid(), bestask = best_ask();
    if (bestbid and bestask)
    {
        if (*bestbid >= *bestask)
        {
            auto &bid = begin(bids)->second[0], ask = begin(asks)->second[0];
            Quantity share = std::min(bid.quantity, ask.quantity);
            bid.quantity -= share;
            ask.quantity -= share;
            Trade trade{bid.id, ask.id,
                        bid.id < ask.id ? ask.price : bid.price, share};
            removeEmpty(bids);
            removeEmpty(asks);
            return trade;
        }
    }
    return nullopt;
}
vector<Trade> OrderBook::submit(Order order)
{
    if (order.side == Side::Buy)
        bids[order.price].push_back(order);
    else
        asks[order.price].push_back(order);
    vector<Trade> trades;
    auto trade = execute();
    while (trade)
    {
        trades.push_back(*trade);
        trade = execute();
    };
    return trades;
}