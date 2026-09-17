#include "checked_order_book.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <optional>
#include <random>
#include <vector>

#define CHECK(expr)                                                     \
    do                                                                  \
    { /*  */                                                            \
        if (!(expr))                                                    \
        {                                                               \
            std::cerr << "CHECK failed: " #expr                         \
                      << " at " << __FILE__ << ':' << __LINE__ << '\n'; \
            std::abort();                                               \
        }                                                               \
    } while (false)
Order buy(OrderId id, Price price, Quantity quantity)
{
    return {id, Side::Buy, price, quantity};
}
Order sell(OrderId id, Price price, Quantity quantity)
{
    return {id, Side::Sell, price, quantity};
}
void check_trade(
    const Trade &t,
    OrderId buy_id,
    OrderId sell_id,
    Price price,
    Quantity quantity)
{
    CHECK(t.buy_id == buy_id);
    CHECK(t.sell_id == sell_id);
    CHECK(t.price == price);
    CHECK(t.quantity == quantity);
}
void check_best(
    const CheckedOrderBook &book,
    std::optional<Price> bid,
    std::optional<Price> ask)
{
    CHECK(book.best_bid() == bid);
    CHECK(book.best_ask() == ask);

    if (bid && ask)
        CHECK(*bid < *ask);
}
// ------------------------------------------------------------
// Empty book
// ------------------------------------------------------------
void test_empty_book()
{
    CheckedOrderBook book;

    CHECK(!book.best_bid());
    CHECK(!book.best_ask());
}
// ------------------------------------------------------------
// Resting orders
// ------------------------------------------------------------
void test_resting_buy()
{
    CheckedOrderBook book;

    auto trades = book.submit(buy(1, 100, 5));

    CHECK(trades.empty());
    check_best(book, 100, std::nullopt);
}
void test_resting_sell()
{
    CheckedOrderBook book;

    auto trades = book.submit(sell(1, 100, 5));

    CHECK(trades.empty());
    check_best(book, std::nullopt, 100);
}
void test_non_crossing_orders()
{
    CheckedOrderBook book;

    CHECK(book.submit(buy(1, 100, 5)).empty());
    CHECK(book.submit(sell(2, 101, 5)).empty());

    check_best(book, 100, 101);
}
void test_one_tick_outside_cross()
{
    CheckedOrderBook book;

    book.submit(sell(1, 101, 5));

    auto trades = book.submit(buy(2, 100, 5));

    CHECK(trades.empty());
    check_best(book, 100, 101);
}
// ------------------------------------------------------------
// Exact fills
// ------------------------------------------------------------
void test_exact_fill_buy()
{
    CheckedOrderBook book;

    book.submit(sell(1, 100, 5));

    auto trades = book.submit(buy(2, 100, 5));

    CHECK(trades.size() == 1);
    check_trade(trades[0], 2, 1, 100, 5);

    check_best(book, std::nullopt, std::nullopt);
}
void test_exact_fill_sell()
{
    CheckedOrderBook book;

    book.submit(buy(1, 100, 5));

    auto trades = book.submit(sell(2, 100, 5));

    CHECK(trades.size() == 1);
    check_trade(trades[0], 1, 2, 100, 5);

    check_best(book, std::nullopt, std::nullopt);
}
void test_quantity_one()
{
    CheckedOrderBook book;

    book.submit(sell(1, 100, 1));

    auto trades = book.submit(buy(2, 100, 1));

    CHECK(trades.size() == 1);
    check_trade(trades[0], 2, 1, 100, 1);

    check_best(book, std::nullopt, std::nullopt);
}
// ------------------------------------------------------------
// Partial fills
// ------------------------------------------------------------
void test_partial_resting_fill()
{
    CheckedOrderBook book;

    book.submit(sell(1, 100, 10));

    auto trades = book.submit(buy(2, 100, 3));

    CHECK(trades.size() == 1);
    check_trade(trades[0], 2, 1, 100, 3);

    check_best(book, std::nullopt, 100);

    // Verify exactly 7 remain.
    trades = book.submit(buy(3, 100, 7));

    CHECK(trades.size() == 1);
    check_trade(trades[0], 3, 1, 100, 7);

    check_best(book, std::nullopt, std::nullopt);
}
void test_partial_incoming_fill()
{
    CheckedOrderBook book;

    book.submit(sell(1, 100, 3));

    auto trades = book.submit(buy(2, 100, 10));

    CHECK(trades.size() == 1);
    check_trade(trades[0], 2, 1, 100, 3);

    // Incoming BUY should rest with quantity 7.
    check_best(book, 100, std::nullopt);

    trades = book.submit(sell(3, 100, 7));

    CHECK(trades.size() == 1);
    check_trade(trades[0], 2, 3, 100, 7);

    check_best(book, std::nullopt, std::nullopt);
}
void test_partial_fill_then_cancel()
{
    CheckedOrderBook book;

    book.submit(sell(1, 100, 10));

    auto trades = book.submit(buy(2, 100, 3));

    CHECK(trades.size() == 1);
    check_trade(trades[0], 2, 1, 100, 3);

    CHECK(book.cancel(1));

    check_best(book, std::nullopt, std::nullopt);
}
// ------------------------------------------------------------
// Execution price
//
// v0.1 rule:
// trade executes at the RESTING order's price.
// ------------------------------------------------------------
void test_resting_sell_determines_execution_price()
{
    CheckedOrderBook book;

    book.submit(sell(1, 98, 5));

    auto trades = book.submit(buy(2, 100, 5));

    CHECK(trades.size() == 1);
    check_trade(trades[0], 2, 1, 98, 5);
}
void test_resting_buy_determines_execution_price()
{
    CheckedOrderBook book;

    book.submit(buy(1, 102, 5));

    auto trades = book.submit(sell(2, 100, 5));

    CHECK(trades.size() == 1);
    check_trade(trades[0], 1, 2, 102, 5);
}
// ------------------------------------------------------------
// FIFO / time priority
// ------------------------------------------------------------
void test_fifo()
{
    CheckedOrderBook book;

    book.submit(buy(1, 100, 5));
    book.submit(buy(2, 100, 5));

    auto trades = book.submit(sell(3, 100, 7));

    CHECK(trades.size() == 2);

    check_trade(trades[0], 1, 3, 100, 5);
    check_trade(trades[1], 2, 3, 100, 2);

    // Order 2 should have 3 remaining.
    check_best(book, 100, std::nullopt);

    trades = book.submit(sell(4, 100, 3));

    CHECK(trades.size() == 1);
    check_trade(trades[0], 2, 4, 100, 3);

    check_best(book, std::nullopt, std::nullopt);
}
// ------------------------------------------------------------
// Price priority
// ------------------------------------------------------------
void test_ask_price_priority()
{
    CheckedOrderBook book;

    book.submit(sell(1, 101, 5));
    book.submit(sell(2, 99, 5));
    book.submit(sell(3, 100, 5));

    auto trades = book.submit(buy(4, 101, 12));

    CHECK(trades.size() == 3);

    check_trade(trades[0], 4, 2, 99, 5);
    check_trade(trades[1], 4, 3, 100, 5);
    check_trade(trades[2], 4, 1, 101, 2);

    // SELL 1 should have 3 left.
    check_best(book, std::nullopt, 101);

    trades = book.submit(buy(5, 101, 3));

    CHECK(trades.size() == 1);
    check_trade(trades[0], 5, 1, 101, 3);
}
void test_bid_price_priority()
{
    CheckedOrderBook book;

    book.submit(buy(1, 99, 5));
    book.submit(buy(2, 101, 5));
    book.submit(buy(3, 100, 5));

    auto trades = book.submit(sell(4, 99, 12));

    CHECK(trades.size() == 3);

    check_trade(trades[0], 2, 4, 101, 5);
    check_trade(trades[1], 3, 4, 100, 5);
    check_trade(trades[2], 1, 4, 99, 2);

    check_best(book, 99, std::nullopt);

    trades = book.submit(sell(5, 99, 3));

    CHECK(trades.size() == 1);
    check_trade(trades[0], 1, 5, 99, 3);
}
// ------------------------------------------------------------
// Multiple fills and multiple levels
// ------------------------------------------------------------
void test_multiple_fills_same_price()
{
    CheckedOrderBook book;

    book.submit(sell(1, 100, 3));
    book.submit(sell(2, 100, 4));
    book.submit(sell(3, 100, 8));

    auto trades = book.submit(buy(4, 100, 10));

    CHECK(trades.size() == 3);

    check_trade(trades[0], 4, 1, 100, 3);
    check_trade(trades[1], 4, 2, 100, 4);
    check_trade(trades[2], 4, 3, 100, 3);

    check_best(book, std::nullopt, 100);

    // SELL 3 should have exactly 5 remaining.
    trades = book.submit(buy(5, 100, 5));

    CHECK(trades.size() == 1);
    check_trade(trades[0], 5, 3, 100, 5);
}
void test_multi_level_sweep_with_residual()
{
    CheckedOrderBook book;

    book.submit(sell(1, 98, 2));
    book.submit(sell(2, 99, 3));
    book.submit(sell(3, 100, 4));

    auto trades = book.submit(buy(4, 100, 12));

    CHECK(trades.size() == 3);

    check_trade(trades[0], 4, 1, 98, 2);
    check_trade(trades[1], 4, 2, 99, 3);
    check_trade(trades[2], 4, 3, 100, 4);

    // 12 - 2 - 3 - 4 = 3 should rest as BUY @100.
    check_best(book, 100, std::nullopt);

    trades = book.submit(sell(5, 100, 3));

    CHECK(trades.size() == 1);
    check_trade(trades[0], 4, 5, 100, 3);

    check_best(book, std::nullopt, std::nullopt);
}
// ------------------------------------------------------------
// Price-level removal / best-price updates
// ------------------------------------------------------------
void test_price_level_removed_after_fill()
{
    CheckedOrderBook book;

    book.submit(sell(1, 100, 5));
    book.submit(sell(2, 101, 5));

    CHECK(book.best_ask() == 100);

    auto trades = book.submit(buy(3, 100, 5));

    CHECK(trades.size() == 1);
    check_trade(trades[0], 3, 1, 100, 5);

    // 100 level must disappear.
    CHECK(book.best_ask() == 101);
}
void test_best_bid_updates()
{
    CheckedOrderBook book;

    book.submit(buy(1, 99, 1));
    book.submit(buy(2, 101, 1));
    book.submit(buy(3, 100, 1));

    CHECK(book.best_bid() == 101);

    CHECK(book.cancel(2));
    CHECK(book.best_bid() == 100);

    CHECK(book.cancel(3));
    CHECK(book.best_bid() == 99);

    CHECK(book.cancel(1));
    CHECK(!book.best_bid());
}
void test_best_ask_updates()
{
    CheckedOrderBook book;

    book.submit(sell(1, 101, 1));
    book.submit(sell(2, 99, 1));
    book.submit(sell(3, 100, 1));

    CHECK(book.best_ask() == 99);

    CHECK(book.cancel(2));
    CHECK(book.best_ask() == 100);

    CHECK(book.cancel(3));
    CHECK(book.best_ask() == 101);

    CHECK(book.cancel(1));
    CHECK(!book.best_ask());
}
// ------------------------------------------------------------
// Cancellation
// ------------------------------------------------------------
void test_cancel_only_order()
{
    CheckedOrderBook book;

    book.submit(buy(1, 100, 5));

    CHECK(book.cancel(1));
    CHECK(!book.best_bid());
}
void test_cancel_nonexistent_order()
{
    CheckedOrderBook book;

    book.submit(buy(1, 100, 5));

    CHECK(!book.cancel(999));
    CHECK(book.best_bid() == 100);
}
void test_cancel_already_filled_order()
{
    CheckedOrderBook book;

    book.submit(sell(1, 100, 5));
    book.submit(buy(2, 100, 5));

    CHECK(!book.cancel(1));
    CHECK(!book.cancel(2));
}
void test_cancel_first_order_at_price()
{
    CheckedOrderBook book;

    book.submit(buy(1, 100, 5));
    book.submit(buy(2, 100, 5));

    CHECK(book.cancel(1));

    auto trades = book.submit(sell(3, 100, 5));

    CHECK(trades.size() == 1);
    check_trade(trades[0], 2, 3, 100, 5);
}
void test_cancel_middle_order_at_price()
{
    CheckedOrderBook book;

    book.submit(buy(1, 100, 2));
    book.submit(buy(2, 100, 2));
    book.submit(buy(3, 100, 2));

    CHECK(book.cancel(2));

    auto trades = book.submit(sell(4, 100, 4));

    CHECK(trades.size() == 2);

    check_trade(trades[0], 1, 4, 100, 2);
    check_trade(trades[1], 3, 4, 100, 2);

    check_best(book, std::nullopt, std::nullopt);
}
void test_cancel_last_order_at_price()
{
    CheckedOrderBook book;

    book.submit(buy(1, 100, 2));
    book.submit(buy(2, 100, 2));
    book.submit(buy(3, 100, 2));

    CHECK(book.cancel(3));

    auto trades = book.submit(sell(4, 100, 4));

    CHECK(trades.size() == 2);

    check_trade(trades[0], 1, 4, 100, 2);
    check_trade(trades[1], 2, 4, 100, 2);
}
void test_cancel_removes_best_price_level()
{
    CheckedOrderBook book;

    book.submit(buy(1, 101, 5));
    book.submit(buy(2, 100, 5));

    CHECK(book.best_bid() == 101);

    CHECK(book.cancel(1));

    CHECK(book.best_bid() == 100);
}
// ------------------------------------------------------------
// Largest permitted price and quantity
// ------------------------------------------------------------
void test_large_price_and_quantity()
{
    CheckedOrderBook book;

    const Price price = 1'000'000'000;
    const Quantity quantity = 1'000'000'000;

    book.submit(sell(1, price, quantity));

    auto trades = book.submit(buy(2, price, quantity));

    CHECK(trades.size() == 1);
    check_trade(trades[0], 2, 1, price, quantity);

    check_best(book, std::nullopt, std::nullopt);
}
// ------------------------------------------------------------
// Many orders at one price
// ------------------------------------------------------------
void test_many_orders_same_price()
{
    constexpr int N = 1000;

    CheckedOrderBook book;

    for (int i = 1; i <= N; ++i)
        book.submit(buy(i, 100, 1));

    auto trades = book.submit(sell(N + 1, 100, N));

    CHECK(trades.size() == N);

    for (int i = 0; i < N; ++i)
        check_trade(trades[i], i + 1, N + 1, 100, 1);

    check_best(book, std::nullopt, std::nullopt);
}
// ------------------------------------------------------------
// Many price levels
// ------------------------------------------------------------
void test_many_price_levels()
{
    constexpr int N = 100;

    CheckedOrderBook book;

    for (int i = 0; i < N; ++i)
        book.submit(sell(i + 1, 100 + i, 1));

    auto trades = book.submit(buy(1000, 100 + N - 1, N));

    CHECK(trades.size() == N);

    for (int i = 0; i < N; ++i)
        check_trade(
            trades[i],
            1000,
            i + 1,
            100 + i,
            1);

    check_best(book, std::nullopt, std::nullopt);
}
// ------------------------------------------------------------
// Longer deterministic sequence
// ------------------------------------------------------------
void test_long_sequence()
{
    CheckedOrderBook book;

    CHECK(book.submit(buy(1, 99, 5)).empty());
    check_best(book, 99, std::nullopt);

    CHECK(book.submit(buy(2, 100, 3)).empty());
    check_best(book, 100, std::nullopt);

    CHECK(book.submit(sell(3, 102, 7)).empty());
    check_best(book, 100, 102);

    auto trades = book.submit(sell(4, 100, 2));

    CHECK(trades.size() == 1);
    check_trade(trades[0], 2, 4, 100, 2);

    check_best(book, 100, 102);

    trades = book.submit(buy(5, 103, 10));

    CHECK(trades.size() == 1);
    check_trade(trades[0], 5, 3, 102, 7);

    // BUY 5 has 3 remaining and should now be best bid.
    check_best(book, 103, std::nullopt);

    // Order 3 was completely filled.
    CHECK(!book.cancel(3));

    // Sweep all remaining bids:
    // B5: 3 @103
    // B2: 1 @100
    // B1: 5 @99
    trades = book.submit(sell(6, 99, 9));

    CHECK(trades.size() == 3);

    check_trade(trades[0], 5, 6, 103, 3);
    check_trade(trades[1], 2, 6, 100, 1);
    check_trade(trades[2], 1, 6, 99, 5);

    check_best(book, std::nullopt, std::nullopt);
}
// ------------------------------------------------------------
// Reproducible randomized invariant smoke test
//
// This is NOT a substitute for deterministic tests.
// It mainly checks that submit/cancel sequences never leave a crossed book.
// ------------------------------------------------------------
void test_randomized_invariants()
{
    constexpr int EVENTS = 10000;

    std::mt19937_64 rng(0x123456789ABCDEFULL);

    std::uniform_int_distribution<int> action_dist(0, 99);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<int> price_dist(90, 110);
    std::uniform_int_distribution<Quantity> quantity_dist(1, 20);

    CheckedOrderBook book;

    std::vector<OrderId> submitted_ids;
    submitted_ids.reserve(EVENTS);

    OrderId next_id = 1;

    for (int event = 0; event < EVENTS; ++event)
    {
        const int action = action_dist(rng);

        if (action < 20 && !submitted_ids.empty())
        {
            std::uniform_int_distribution<std::size_t> id_dist(
                0, submitted_ids.size() - 1);

            book.cancel(submitted_ids[id_dist(rng)]);
        }
        else
        {
            Order order{
                next_id++,
                side_dist(rng) ? Side::Buy : Side::Sell,
                price_dist(rng),
                quantity_dist(rng),
            };

            submitted_ids.push_back(order.id);
            book.submit(order);
        }

        const auto bid = book.best_bid();
        const auto ask = book.best_ask();

        if (bid && ask)
            CHECK(*bid < *ask);
    }
}
void benchmark_10000_random_orders()
{
    constexpr int N = 10'000;
    constexpr int RUNS = 5;

    std::mt19937_64 rng(0x123456789ABCDEFULL);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<Price> price_dist(90, 110);
    std::uniform_int_distribution<Quantity> quantity_dist(1, 20);

    std::vector<Order> orders;
    orders.reserve(N);

    for (OrderId id = 1; id <= N; ++id)
    {
        orders.push_back({id,
                          side_dist(rng) ? Side::Buy : Side::Sell,
                          price_dist(rng),
                          quantity_dist(rng)});
    }

    std::array<double, RUNS> times{};

    for (int run = 0; run < RUNS; ++run)
    {
        OrderBook book;

        const auto start = std::chrono::steady_clock::now();

        for (const Order &order : orders)
            book.submit(order);

        const auto end = std::chrono::steady_clock::now();

        times[run] =
            std::chrono::duration<double, std::micro>(end - start).count();
    }

    const double total =
        std::accumulate(times.begin(), times.end(), 0.0);

    const double average = total / RUNS;
    const double low = *std::min_element(times.begin(), times.end());
    const double high = *std::max_element(times.begin(), times.end());

    std::cout << std::fixed << std::setprecision(2)
              << "\nBenchmark: " << N << " random orders\n"
              << "Runs:    " << RUNS << '\n'
              << "Average: " << average << " us\n"
              << "Low:     " << low << " us\n"
              << "High:    " << high << " us\n"
              << "Average: " << N / (average / 1'000'000.0)
              << " orders/sec\n";
}
// ------------------------------------------------------------
// Test runner
// ------------------------------------------------------------
int main()
{
    test_empty_book();

    test_resting_buy();
    test_resting_sell();
    test_non_crossing_orders();
    test_one_tick_outside_cross();

    test_exact_fill_buy();
    test_exact_fill_sell();
    test_quantity_one();

    test_partial_resting_fill();
    test_partial_incoming_fill();
    test_partial_fill_then_cancel();

    test_resting_sell_determines_execution_price();
    test_resting_buy_determines_execution_price();

    test_fifo();

    test_ask_price_priority();
    test_bid_price_priority();

    test_multiple_fills_same_price();
    test_multi_level_sweep_with_residual();

    test_price_level_removed_after_fill();
    test_best_bid_updates();
    test_best_ask_updates();

    test_cancel_only_order();
    test_cancel_nonexistent_order();
    test_cancel_already_filled_order();
    test_cancel_first_order_at_price();
    test_cancel_middle_order_at_price();
    test_cancel_last_order_at_price();
    test_cancel_removes_best_price_level();

    test_large_price_and_quantity();

    test_many_orders_same_price();
    test_many_price_levels();

    test_long_sequence();

    test_randomized_invariants();

    std::cout << "All order book tests passed.\n";
    benchmark_10000_random_orders();
}
