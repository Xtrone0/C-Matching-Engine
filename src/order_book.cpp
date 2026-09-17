#include "../include/order_book.hpp"
#include <limits>
#include <stdexcept>
#include <utility>
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
// Cancels an order by the order ID
// Returns true if it was successfully canceled and false otherwise.
bool OrderBook::cancel(OrderId id)
{
    if (!active.contains(id))
        return false;
    auto location = active[id];
    active.erase(id);
    auto &levels = location.side == Side::Buy ? bids : asks;
    location.level->second.erase(location.order);
    if ((location.level)->second.empty())
        levels.erase(location.level);
    return true;
}
// Removes the best price from the given side if there are no longer any orders at that price
template <class T>
void OrderBook::removeEmpty(T &side)
{
    if (side.size())
    {
        auto &orders = begin(side)->second;
        if (orders.front().quantity == 0)
            cancel(orders.front().id);
    }
}
// Executes a trade if the best bid is currently bigger than or equal to the best ask
// Returns a trade if what was successfully executed, and null otherwise.
// id is the id of the incoming order
template <class Levels>
std::vector<Trade> OrderBook::match(Order &incoming, Levels &opposite, std::optional<Price> limit)
{
    vector<Trade> trades;
    while (not opposite.empty() and incoming.quantity > 0)
    {
        auto &maker = begin(opposite)->second.front();
        const Price makerprice = maker.price;
        if (limit)
        {
            const bool cross =
                incoming.side == Side::Buy ? *limit >= makerprice : *limit <= makerprice;
            if (not cross)
                break;
        }
        Quantity share = std::min(incoming.quantity, maker.quantity);
        incoming.quantity -= share;
        maker.quantity -= share;
        Trade trade{
            incoming.id, maker.id,
            makerprice, share};
        if (incoming.side == Side::Sell)
            std::swap(trade.buy_id, trade.sell_id);
        removeEmpty(opposite);
        trades.push_back(trade);
    }
    return trades;
}
// add a resting order
template <class T>
void OrderBook::rest(Order order, T &side)
{
    order.priority = ++priorityCounter;
    side[order.price].push_back(order);
    active[order.id] = {
        order.side,
        side.find(order.price),
        prev(end(side[order.price]))};
}
// Submits an order and executes all available trades
// Returns the vector of the trades that were executed after submitting this order.
void OrderBook::validateOrder(const Order &order) const
{
    if (order.id == 0)
        throw std::invalid_argument("Order ID must be nonzero");

    if (order.side != Side::Buy and order.side != Side::Sell)
        throw std::invalid_argument("Invalid side");

    if (order.price < 1 or order.price > 1'000'000'000)
        throw std::invalid_argument("Price out of range");

    if (order.quantity == 0 or order.quantity > 1'000'000'000)
        throw std::invalid_argument("Quantity out of range");
    if (active.contains(order.id))
        throw std::invalid_argument("Order ID must be unique");
}
vector<Trade> OrderBook::submit(Order order)
{
    validateOrder(order);
    auto trades = order.side == Side::Buy ? match(order, asks, order.price) : match(order, bids, order.price);
    if (order.quantity > 0)
    {
        order.side == Side::Buy ? rest(order, bids) : rest(order, asks);
    }
    return trades;
}
std::vector<Trade> OrderBook::submit_market(
    OrderId id,
    Side side,
    Quantity quantity)
{
    // validate The market order
    Order order{id, side, 1, quantity};
    validateOrder(order);
    auto trades = order.side == Side::Buy ? match(order, asks, nullopt) : match(order, bids, nullopt);
    return trades;
}
template <class T>
vector<SnapshotLevel> OrderBook::snapside(const T &side) const
{
    vector<SnapshotLevel> snap;
    for (const auto &[price, orders] : side)
    {
        SnapshotLevel level{};
        level.price = price;
        level.orders = vector<Order>(begin(orders), end(orders));
        for (const auto &order : orders)
        {
            if (order.quantity >
                std::numeric_limits<Quantity>::max() - level.quantity)
                throw std::overflow_error("Snapshot quantity overflow");
            level.quantity += order.quantity;
        }
        snap.push_back(level);
    }
    return snap;
}
Snapshot OrderBook::snapshot() const
{
    Snapshot result;
    result.bids = snapside(bids);
    result.asks = snapside(asks);
    return result;
}
template <class T>
void OrderBook::assert_invariantsLevel(const T &side, const Side type, std::unordered_set<OrderId> &observed) const
{
    for (const auto &[price, orders] : side)
    {
        if (orders.empty())
            throw std::logic_error("Empty price level");
        Priority prev = 0;
        Quantity total = 0;
        for (const auto &order : orders)
        {
            if (order.id == 0)
                throw std::logic_error("Zero resting ID");
            if (order.price < 1 or order.price > 1'000'000'000)
                throw std::logic_error("Resting price out of range");
            if (order.quantity == 0 or order.quantity > 1'000'000'000)
                throw std::logic_error("Resting quantity out of range");
            if (order.side != type)
                throw std::logic_error("Resting order on wrong side");
            if (price != order.price)
                throw std::logic_error("Resting order at wrong price level");
            if (!observed.insert(order.id).second)
                throw std::logic_error("Duplicate resting ID");
            if (!active.contains(order.id))
                throw std::logic_error("Resting ID missing from active index");
            if (order.priority == 0)
                throw std::logic_error("Zero arrival priority");
            if (order.priority <= prev)
                throw std::logic_error("FIFO priorities not increasing");
            if (order.priority > priorityCounter)
                throw std::logic_error("Arrival priority exceeds counter");
            total = checkedInvariantTotal(total, order.quantity);
            prev = order.priority;
        }
    }
}
Quantity OrderBook::checkedInvariantTotal(Quantity total, Quantity quantity)
{
    if (quantity > std::numeric_limits<Quantity>::max() - total)
        throw std::logic_error("Price level quantity overflow");
    return total + quantity;
}
void OrderBook::assert_invariants() const
{
    const auto bid = best_bid();
    const auto ask = best_ask();
    if (bid and ask and *bid >= *ask)
        throw std::logic_error("Crossed resting book");
    std::unordered_set<OrderId> seen;
    assert_invariantsLevel(bids, Side::Buy, seen);
    assert_invariantsLevel(asks, Side::Sell, seen);
    if (seen.size() != active.size())
        throw std::logic_error("Active ID index disagrees with book");

    auto checkLocations = [&](const Levels &levels, Side type)
    {
        for (const auto &level : levels)
        {
            for (const auto &order : level.second)
            {
                const auto &location = active.at(order.id);

                if (location.side != type)
                    throw std::logic_error("Location side mismatch");

                if (location.level->first != level.first)
                    throw std::logic_error("Location price mismatch");

                if (location.order->id != order.id)
                    throw std::logic_error("Location order mismatch");
            }
        }
    };
    checkLocations(bids, Side::Buy);
    checkLocations(asks, Side::Sell);
}
std::vector<Trade> OrderBook::amend(
    OrderId id,
    Price newPrice,
    Quantity newRemaining)
{
    if (newRemaining == 0 or newRemaining > 1'000'000'000)
        throw std::invalid_argument("Quantity out of range");

    if (not active.contains(id))
        throw std::invalid_argument("Id not found");
    if (newPrice < 1 or newPrice > 1'000'000'000)
        throw std::invalid_argument("Price out of range");
    Order order = *(active[id].order);
    std::vector<Trade> trades;
    if (newPrice == order.price and newRemaining <= order.quantity)
        active[id].order->quantity = newRemaining;
    else
    {
        cancel(id);
        order.price = newPrice;
        order.quantity = newRemaining;
        trades = submit(order);
    }
    return trades;
}
