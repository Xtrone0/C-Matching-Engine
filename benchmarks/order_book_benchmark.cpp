#include "benchmark_support.hpp"
#include "scan_order_book.hpp"
#include "benchmark_build_info.hpp"
#include <array>
#include <limits>
#include <numeric>
#include <unordered_set>
using namespace benchmark;
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

struct Options {
    std::filesystem::path output = "build/benchmark-run", workload;
    std::string suite = "all";
    std::size_t commands = 10'000, initial = 1'000, largeCancels = 1'000, setup = 0, maximum = 100'000;
    int runs = 5;
    bool latency = true;
};
void command_benchmarks(const Options& options, Output& output) {
    std::vector<Workload> workloads;
    if (!options.workload.empty()) {
        auto commands = test_trace::load(options.workload);
        if (options.setup >= commands.size() || options.setup > 100'000 || commands.size() - options.setup > 100'000)
            throw std::invalid_argument("Invalid setup or measured command count");
        workloads.push_back({"custom", std::move(commands), options.setup});
    } else {
        for (auto family : {Family::Submissions, Family::Mixed, Family::CancellationHeavy, Family::Deep, Family::SinglePrice})
            workloads.push_back(generate(family, options.commands, options.initial, 7));
    }
    std::ofstream latencySummary(output.directory / "latency-summary.csv");
    if (!latencySummary) throw std::runtime_error("Cannot create latency summary");
    latencySummary << "workload,action,samples,p50_ns,p95_ns,p99_ns,timer_pair_median_ns,minimum_positive_timer_pair_ns,sampled_batch_overhead_ratio\n";
    latencySummary << std::setprecision(17);
    for (auto& workload : workloads) {
        const auto file = output.directory / "workloads" / (workload.name + ".trace");
        test_trace::save(file, workload.commands);
        workload.commands = test_trace::load(file); // Actual saved input is used by every measured run.
        const auto expected = verify(workload);
        const auto count = workload.commands.size() - workload.setup;
        output.workloads << workload.name << ",workloads/" << file.filename().string() << ',' << workload.setup << ',' << count
            << ',' << expected.start << ',' << expected.finish << ',' << expected.low << ',' << expected.high << ',' << expected.levels;
        for (const auto amount : expected.actions) output.workloads << ',' << amount;
        output.workloads << ',' << expected.counts.trades << ',' << expected.counts.canceled << ',' << expected.counts.rejected << '\n';
        const bool sampled = options.latency && (workload.name.starts_with("mixed-") || workload.name == "custom");
        (void)measure(workload, expected, false); // Discard disposable warm-up book.
        if (sampled) (void)measure(workload, expected, true);
        std::vector<double> batchCosts, sampledCosts, observations;
        std::array<std::vector<double>, 4> perAction;
        for (int run = 0; run < options.runs; ++run) {
            auto record = [&](bool sample) {
                const auto result = measure(workload, expected, sample);
                output.row(workload.name, sample ? "sampled" : "batch", run + 1, count, result);
                (sample ? sampledCosts : batchCosts).push_back(result.elapsed / static_cast<double>(count));
                if (sample) {
                    for (std::size_t i = 0; i < result.samples.size(); ++i) {
                        const auto action = workload.commands[workload.setup + i].action;
                        output.latency << workload.name << ',' << run + 1 << ',' << i + 1 << ',' << test_trace::name(action) << ',' << result.samples[i] << '\n';
                        observations.push_back(result.samples[i]);
                        perAction.at(static_cast<std::size_t>(action)).push_back(result.samples[i]);
                    }
                }
            };
            if (sampled && run % 2 == 0) { record(true); record(false); }
            else { record(false); if (sampled) record(true); }
        }
        output.report(workload.name, "batch", batchCosts);
        if (sampled) {
            output.report(workload.name, "sampled", sampledCosts);
            std::vector<double> overhead(count);
            for (auto& value : overhead) {
                const auto before = Clock::now(); const auto after = Clock::now(); value = ns(after - before);
            }
            double minimum = std::numeric_limits<double>::infinity();
            for (std::size_t i = 0; i < overhead.size(); ++i) {
                output.overhead << i + 1 << ',' << overhead[i] << '\n';
                if (overhead[i] > 0) minimum = std::min(minimum, overhead[i]);
            }
            if (!std::isfinite(minimum)) minimum = 0;
            const auto perturbation = median(sampledCosts) / median(batchCosts);
            auto summarize = [&](const char* action, const std::vector<double>& samples) {
                if (samples.empty()) return;
                latencySummary << workload.name << ',' << action << ',' << samples.size() << ','
                    << quantile(samples, .5) << ',' << quantile(samples, .95) << ',' << quantile(samples, .99) << ','
                    << median(overhead) << ',' << minimum << ',' << perturbation << '\n';
            };
            summarize("ALL", observations);
            for (std::size_t i = 0; i < perAction.size(); ++i)
                summarize(test_trace::name(static_cast<replay::Action>(i)), perAction[i]);
            std::cout << workload.name << " latency: " << observations.size() << " samples, p50=" << quantile(observations, .5)
                      << " ns, p99=" << quantile(observations, .99) << " ns; timer pair median=" << median(overhead)
                      << " ns; sampled/batch cost=" << perturbation << "x\n";
        }
    }
    latencySummary.flush();
    if (!latencySummary) throw std::runtime_error("Cannot write latency summary");
}

void cancellation_benchmarks(const Options& options, Output& output) {
    for (OrderId count : {OrderId{1'000}, OrderId{10'000}, OrderId{100'000}}) {
        if (count > options.maximum) continue;
        for (Price levels : {1, 100}) {
            std::vector<Order> orders;
            std::vector<OrderId> ids;
            for (OrderId id = 1; id <= count; ++id) {
                const Side side = id % 2 ? Side::Buy : Side::Sell;
                const Price offset = static_cast<Price>(((id - 1) / 2) % levels);
                orders.push_back({id, side, side == Side::Buy ? 1'000 - offset : 2'000 + offset, 1});
                ids.push_back(id);
            }
            std::mt19937_64 rng(7);
            std::shuffle(ids.begin(), ids.end(), rng);
            if (count > 10'000) ids.resize(options.largeCancels);
            const auto name = "cancel-" + std::to_string(count) + "-" + std::to_string(ids.size()) + "-levels-" + std::to_string(levels);
            // Save the exact populated book and cancellation order for reproduction.
            std::vector<replay::Command> commands;
            for (const auto& order : orders)
                commands.push_back({replay::Action::Limit, order.id, order.side, order.price, order.quantity});
            for (auto id : ids) commands.push_back({replay::Action::Cancel, id, Side::Buy, 0, 0});
            test_trace::save(output.directory / "workloads" / (name + ".trace"), commands);
            // Both candidates receive the same concrete saved IDs and setup.
            commands = test_trace::load(output.directory / "workloads" / (name + ".trace"));
            for (std::size_t i = 0; i < orders.size(); ++i)
                orders[i] = {commands[i].id, commands[i].side, commands[i].price, commands[i].quantity};
            for (std::size_t i = 0; i < ids.size(); ++i) ids[i] = commands[orders.size() + i].id;
            output.workloads << name << ",workloads/" << name << ".trace," << count << ',' << ids.size() << ',' << count << ','
                << count - ids.size() << ',' << count - ids.size() << ',' << count << ',' << 2 * levels << ",0,0," << ids.size()
                << ",0,0," << ids.size() << ",0\n";
            std::vector<double> scanCosts, indexedCosts;
            for (int run = 0; run <= options.runs; ++run) {
                auto sample = [&](bool scan) {
                    const auto cost = scan ? time_cancellations<ScanOrderBook>(orders, ids) : time_cancellations<OrderBook>(orders, ids);
                    if (run == 0) return;
                    (scan ? scanCosts : indexedCosts).push_back(cost);
                    Measurement result{cost * static_cast<double>(ids.size()), {}, {}};
                    result.counts.canceled = ids.size();
                    output.row(name, scan ? "scan" : "indexed", run, ids.size(), result);
                };
                if (run % 2 == 0) { sample(true); sample(false); }
                else { sample(false); sample(true); }
            }
            output.report(name, "scan", scanCosts);
            output.report(name, "indexed", indexedCosts);
            std::cout << "  scan/indexed median ratio=" << median(scanCosts) / median(indexedCosts) << "x\n";
        }
    }
}
int main(int argc, char** argv) {
    try {
        Options options;
        for (int i = 1; i < argc; ++i) {
            const std::string flag(argv[i]);
            if (flag == "--help") {
                std::cout << "Usage: order_book_benchmark [--out DIRECTORY] [--suite all|commands|cancel]\n"
                          << "  [--commands 1..100000] [--initial 0..100000] [--runs 3..50]\n"
                          << "  [--large-cancels 1000|10000] [--max-orders 1000|10000|100000] [--no-latency]\n"
                          << "  [--workload FILE --setup COUNT] (command suite; saved setup prefix is untimed)\n";
                return 0;
            }
            if (flag == "--no-latency") { options.latency = false; continue; }
            if (i + 1 >= argc) throw std::invalid_argument("Missing value for " + flag);
            const std::string value(argv[++i]);
            if (flag == "--out") options.output = value;
            else if (flag == "--suite") options.suite = value;
            else if (flag == "--workload") options.workload = value;
            else if (flag == "--commands") options.commands = test_trace::number<std::size_t>(value);
            else if (flag == "--initial") options.initial = test_trace::number<std::size_t>(value);
            else if (flag == "--setup") options.setup = test_trace::number<std::size_t>(value);
            else if (flag == "--runs") options.runs = test_trace::number<int>(value);
            else if (flag == "--large-cancels") options.largeCancels = test_trace::number<std::size_t>(value);
            else if (flag == "--max-orders") options.maximum = test_trace::number<std::size_t>(value);
            else throw std::invalid_argument("Unknown option: " + flag);
        }
        if (options.suite != "all" && options.suite != "commands" && options.suite != "cancel")
            throw std::invalid_argument("Unknown suite");
        if ((!options.workload.empty() && options.suite == "cancel") ||
            (options.workload.empty() && options.setup != 0))
            throw std::invalid_argument("workload/setup options require a saved command workload");
        if (options.runs < 3 || options.runs > 50 || options.commands == 0 || options.commands > 100'000 || options.initial > 100'000)
            throw std::invalid_argument("Invalid runs or workload size");
        if (options.largeCancels != 1000 && options.largeCancels != 10000)
            throw std::invalid_argument("large-cancels must be 1000 or 10000");
        if (options.maximum != 1000 && options.maximum != 10000 && options.maximum != 100000)
            throw std::invalid_argument("max-orders must be 1000, 10000 or 100000");
#if !defined(NDEBUG) || defined(_GLIBCXX_DEBUG)
        throw std::runtime_error("Performance measurements require a Release build without checked containers");
#endif
        Output output(options.output);
        std::ofstream buildInfo(options.output / "build.txt");
        buildInfo << "compiler=" << __VERSION__ << "\nflags=" << BENCHMARK_FLAGS << "\nconfiguration=" << BENCHMARK_CONFIGURATION
                  << "\nclock=steady_clock\nclock_period=" << Clock::period::num << '/' << Clock::period::den
                  << " seconds (nominal tick, not measured accuracy)\n";
        buildInfo.flush();
        if (!buildInfo) throw std::runtime_error("Cannot write build information");
        if (options.suite != "cancel") command_benchmarks(options, output);
        if (options.suite != "commands") cancellation_benchmarks(options, output);
        output.finish();
        std::cout << "Saved verified measurements to " << std::filesystem::absolute(options.output).string() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
