#include "../include/order_book.hpp"
using std::nullopt;
using std::vector;
// Returns the price of the best bid
optional<Price> OrderBook::best_bid() const
{
    if (!bids.size())
        return nullopt;
    return begin(bids)->first;
}
// Returns the price of the best ask
optional<Price> OrderBook::best_ask() const
{
    if (!asks.size())
        return nullopt;
    return begin(asks)->first;
}
// Tries to find the order with ID from the given side
// Returns true if it was successfully found and false otherwise.
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
        if (orders.empty())
            side.erase(price);
        return true;
    }
    return false;
}
// Cancels an order by the order ID
// Returns true if it was successfully canceled and false otherwise.
bool OrderBook::cancel(OrderId id)
{
    if (cancelSide(bids, id) or cancelSide(asks, id))
        return true;
    return false;
}
// Removes the best price from the given side if there are no longer any orders at that price
template <class T>
void OrderBook::removeEmpty(T &side)
{
    if (side.size())
    {
        auto &orders = begin(side)->second;
        if (orders[0].quantity == 0)
        {
            orders.erase(begin(orders));
        }
        if (orders.size() == 0)
        {
            side.erase(begin(side));
        }
    }
}
// Executes a trade if the best bid is currently bigger than or equal to the best ask
// Returns a trade if what was successfully executed, and null otherwise.
optional<Trade> OrderBook::execute()
{
    auto bestbid = best_bid(), bestask = best_ask();
    if (bestbid and bestask)
    {
        if (*bestbid >= *bestask)
        {
            auto &bid = begin(bids)->second[0];
            auto &ask = begin(asks)->second[0];
            Quantity share = std::min(bid.quantity, ask.quantity);
            bid.quantity -= share;
            ask.quantity -= share;
            Trade trade{bid.id, ask.id,
                        bid.id > ask.id ? ask.price : bid.price, share};
            removeEmpty(bids);
            removeEmpty(asks);
            return trade;
        }
    }
    return nullopt;
}
// Submits an order and executes all available trades
// Returns the vector of the trades that were executed after submitting this order.
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