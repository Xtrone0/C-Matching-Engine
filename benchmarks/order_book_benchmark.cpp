#include "order_book.hpp"
#include "scan_order_book.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <vector>
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
// Book must keep its concrete type: ScanOrderBook::cancel is not virtual.
template <class Book>
double time_cancellations(const std::vector<Order> &orders,
                          const std::vector<OrderId> &ids)
{
    Book book;
    for (const auto &order : orders)
    {
        if (!book.submit(order).empty())
            throw std::logic_error("Cancellation setup unexpectedly matched orders");
    }
    book.assert_invariants();

    std::size_t canceled = 0;
    const auto start = std::chrono::steady_clock::now();

    for (OrderId id : ids)
        canceled += book.cancel(id);

    const auto finish = std::chrono::steady_clock::now();

    // Verify after stopping the clock; these checks also keep the result used.
    book.assert_invariants();
    const auto remaining = book.snapshot();
    const std::unordered_set<OrderId> canceledIds(ids.begin(), ids.end());
    std::size_t remainingCount = 0;
    auto checkSide = [&](const auto &levels)
    {
        for (const auto &level : levels)
        {
            for (const auto &order : level.orders)
            {
                if (canceledIds.contains(order.id))
                    throw std::logic_error("Canceled order is still resting");
                // Setup uses consecutive IDs in arrival order.
                const auto &expected = orders.at(order.id - 1);
                if (order.side != expected.side || order.price != expected.price ||
                    order.quantity != expected.quantity || order.priority != order.id)
                    throw std::logic_error("Cancellation changed a surviving order");
                ++remainingCount;
            }
        }
    };
    checkSide(remaining.bids);
    checkSide(remaining.asks);
    if (canceled != ids.size() || remainingCount != orders.size() - ids.size())
        throw std::logic_error("Cancellation benchmark has incorrect order counts");

    const double elapsed =
        std::chrono::duration<double, std::nano>(finish - start).count();
    return elapsed / static_cast<double>(ids.size());
}
void benchmark_cancellations(std::size_t largeCancels)
{
    constexpr int RUNS = 5;
    constexpr OrderId sizes[] = {1'000, 10'000, 100'000};

    std::cout << "\nCancellation benchmark: shuffled IDs, both sides\n"
              << "One warm-up pair, then " << RUNS << " measured pairs per workload\n"
              << "100,000-order books use " << largeCancels << " cancellations per sample\n"
              << "Medians in ns/cancel; speedup = scan / indexed\n\n"
              << std::setw(10) << "Orders"
              << std::setw(12) << "Cancels"
              << std::setw(14) << "Levels/side"
              << std::setw(16) << "Scan"
              << std::setw(16) << "Indexed"
              << std::setw(12) << "Speedup" << '\n';

    for (OrderId count : sizes)
    {
        for (Price levels : {1, 100})
        {
            std::vector<Order> orders;
            std::vector<OrderId> ids;
            orders.reserve(count);
            ids.reserve(count);
            for (OrderId id = 1; id <= count; ++id)
            {
                const Side side = id % 2 ? Side::Buy : Side::Sell;
                const Price offset = static_cast<Price>(((id - 1) / 2) % levels);
                const Price price = side == Side::Buy ? 1'000 - offset : 2'000 + offset;
                orders.push_back({id, side, price, 1});
                ids.push_back(id);
            }
            std::mt19937_64 random(7);
            std::shuffle(ids.begin(), ids.end(), random);
            if (count > 10'000)
                ids.resize(largeCancels);

            std::array<double, RUNS> scanTimes{}, indexedTimes{};
            for (int run = 0; run <= RUNS; ++run)
            {
                double scan, indexed;
                // Alternate execution order; each call populates a fresh book.
                if (run % 2 == 0)
                {
                    scan = time_cancellations<ScanOrderBook>(orders, ids);
                    indexed = time_cancellations<OrderBook>(orders, ids);
                }
                else
                {
                    indexed = time_cancellations<OrderBook>(orders, ids);
                    scan = time_cancellations<ScanOrderBook>(orders, ids);
                }
                if (run == 0)
                    continue; // Discard the warm-up measurements.
                scanTimes[run - 1] = scan;
                indexedTimes[run - 1] = indexed;
            }
            std::sort(scanTimes.begin(), scanTimes.end());
            std::sort(indexedTimes.begin(), indexedTimes.end());
            const double scan = scanTimes[RUNS / 2];
            const double indexed = indexedTimes[RUNS / 2];
            std::cout << std::fixed << std::setprecision(2)
                      << std::setw(10) << count
                      << std::setw(12) << ids.size()
                      << std::setw(14) << levels
                      << std::setw(16) << scan
                      << std::setw(16) << indexed
                      << std::setw(11) << scan / indexed << "x" << std::endl;
        }
    }
}
int main(int argc, char **argv)
{
    const char *usage =
        "Usage: order_book_benchmark [--large-cancels 1000|10000]\n"
        "Default: 1000 cancellations for each 100,000-order workload.\n";
    std::size_t largeCancels = 1'000;
    if (argc == 2 && std::string_view(argv[1]) == "--help")
    {
        std::cout << usage;
        return 0;
    }
    if (argc != 1)
    {
        if (argc != 3 || std::string_view(argv[1]) != "--large-cancels" ||
            (std::string_view(argv[2]) != "1000" && std::string_view(argv[2]) != "10000"))
        {
            std::cerr << usage;
            return 2;
        }
        if (std::string_view(argv[2]) == "10000") largeCancels = 10'000;
    }
    try
    {
        benchmark_10000_random_orders();
        benchmark_cancellations(largeCancels);
    }
    catch (const std::exception &error)
    {
        std::cerr << "Benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
