#include "order_book.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
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

int main()
{
    benchmark_10000_random_orders();
}

