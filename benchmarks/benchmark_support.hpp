#pragma once
#include "workloads.hpp"
#include "metrics.hpp"
#include "../tests/test_trace.hpp"
#include <array>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>

namespace benchmark {
using Clock = std::chrono::steady_clock;
inline double ns(Clock::duration value) { return std::chrono::duration<double, std::nano>(value).count(); }
inline std::string snapshot_text(const Snapshot& snapshot) {
    std::ostringstream out;
    test_trace::describe(out, snapshot);
    return out.str();
}
struct Counts {
    std::uint64_t trades = 0, quantity = 0, canceled = 0, rejected = 0, fingerprint = 0;
    void add(const replay::CommandResult& result) {
        canceled += result.canceled;
        rejected += result.rejected;
        trades += result.trades.size();
        for (const auto& trade : result.trades) {
            quantity += trade.quantity;
            // Observable ordered result consumption, included in batch timing.
            for (auto field : {trade.buy_id, trade.sell_id, static_cast<std::uint64_t>(trade.price), trade.quantity})
                fingerprint = (fingerprint ^ field) * 1099511628211ULL;
        }
    }
    bool operator==(const Counts&) const = default;
};
inline void require_same(const replay::CommandResult& actual, const replay::CommandResult& expected) {
    if (actual.rejected != expected.rejected || actual.canceled != expected.canceled ||
        actual.trades.size() != expected.trades.size()) throw std::logic_error("Benchmark/reference outcome mismatch");
    for (std::size_t i = 0; i < expected.trades.size(); ++i) {
        const auto& a = actual.trades[i]; const auto& e = expected.trades[i];
        if (a.buy_id != e.buy_id || a.sell_id != e.sell_id || a.price != e.price || a.quantity != e.quantity)
            throw std::logic_error("Benchmark/reference trade mismatch");
    }
}
struct Verified {
    Counts counts;
    std::string final;
    std::size_t start = 0, finish = 0, low = 0, high = 0, levels = 0;
    std::array<std::size_t, 4> actions{};
};
inline Verified verify(const Workload& workload) {
    OrderBook actual;
    test_support::ReferenceBook reference;
    Verified result;
    for (std::size_t i = 0; i < workload.commands.size(); ++i) {
        if (i == workload.setup) {
            result.start = result.low = result.high = reference.resting.size();
            const auto initial = actual.snapshot();
            result.levels = initial.bids.size() + initial.asks.size();
        }
        const auto expected = replay::execute(reference, workload.commands[i]);
        const auto observed = replay::execute(actual, workload.commands[i]);
        require_same(observed, expected);
        if (i >= workload.setup) {
            result.counts.add(expected);
            ++result.actions.at(static_cast<std::size_t>(workload.commands[i].action));
            result.low = std::min(result.low, reference.resting.size());
            result.high = std::max(result.high, reference.resting.size());
        }
    }
    actual.assert_invariants();
    result.final = snapshot_text(reference.snapshot());
    if (snapshot_text(actual.snapshot()) != result.final) throw std::logic_error("Benchmark/reference snapshot mismatch");
    result.finish = reference.resting.size();
    if (result.high > 100'000) throw std::logic_error("Benchmark exceeded live-order limit");
    return result;
}
struct Measurement { double elapsed; Counts counts; std::vector<double> samples; };
inline Measurement measure(const Workload& workload, const Verified& expected, bool sampled) {
    OrderBook book;
    for (std::size_t i = 0; i < workload.setup; ++i)
        (void)replay::execute(book, workload.commands[i]);
    book.assert_invariants();
    const auto size = workload.commands.size() - workload.setup;
    Measurement result{0, {}, sampled ? std::vector<double>(size) : std::vector<double>{}};
    const auto start = Clock::now();
    for (std::size_t i = 0; i < size; ++i) {
        if (sampled) {
            const auto before = Clock::now();
            const auto output = replay::execute(book, workload.commands[workload.setup + i]);
            const auto after = Clock::now();
            result.samples[i] = ns(after - before);
            result.counts.add(output);
        } else {
            result.counts.add(replay::execute(book, workload.commands[workload.setup + i]));
        }
    }
    const auto stop = Clock::now();
    result.elapsed = ns(stop - start);
    if (result.elapsed <= 0) throw std::runtime_error("Batch too short for timer resolution; increase command count");
    // Validation, snapshots and output are outside both timing boundaries.
    book.assert_invariants();
    if (!(result.counts == expected.counts) || snapshot_text(book.snapshot()) != expected.final)
        throw std::logic_error("Measured run disagrees with reference verification");
    return result;
}
struct Output {
    std::filesystem::path directory;
    std::ofstream raw, summary, workloads, latency, overhead;
    explicit Output(const std::filesystem::path& path) : directory(path) {
        if (std::filesystem::exists(path) && !std::filesystem::is_empty(path))
            throw std::invalid_argument("Output directory must be new or empty");
        std::filesystem::create_directories(path / "workloads");
        raw.open(path / "repetitions.csv"); summary.open(path / "summary.csv");
        workloads.open(path / "workloads.csv"); latency.open(path / "latency-samples.csv");
        overhead.open(path / "timer-overhead.csv");
        if (!raw || !summary || !workloads || !latency || !overhead) throw std::runtime_error("Cannot create benchmark output");
        raw << "workload,mode,repetition,commands,elapsed_ns,ns_per_command,commands_per_second,trades,executed_quantity,canceled,rejected,fingerprint\n";
        summary << "workload,mode,repetitions,median_ns_per_command,min_ns_per_command,max_ns_per_command,median_commands_per_second\n";
        workloads << "workload,file,setup_commands,measured_commands,start_orders,end_orders,min_orders,max_orders,start_levels,new,market,cancel,amend,trades,canceled,rejected\n";
        latency << "workload,repetition,command,action,elapsed_ns\n";
        overhead << "sample,elapsed_ns\n";
        raw << std::setprecision(17); summary << std::setprecision(17);
        latency << std::setprecision(17); overhead << std::setprecision(17);
    }
    void row(const std::string& name, const char* mode, int repetition, std::size_t count, const Measurement& value) {
        raw << name << ',' << mode << ',' << repetition << ',' << count << ',' << value.elapsed << ','
            << value.elapsed / static_cast<double>(count) << ',' << static_cast<double>(count) * 1e9 / value.elapsed << ','
            << value.counts.trades << ',' << value.counts.quantity << ',' << value.counts.canceled << ','
            << value.counts.rejected << ',' << value.counts.fingerprint << '\n';
    }
    void report(const std::string& name, const char* mode, const std::vector<double>& costs) {
        const auto med = median(costs);
        const auto rate = median_throughput(costs);
        summary << name << ',' << mode << ',' << costs.size() << ',' << med << ','
                << *std::min_element(costs.begin(), costs.end()) << ',' << *std::max_element(costs.begin(), costs.end())
                << ',' << rate << '\n';
        std::cout << name << " " << mode << ": median " << med << " ns/command, " << rate << " commands/s\n";
    }
    void finish() {
        for (auto* file : {&raw, &summary, &workloads, &latency, &overhead}) {
            file->flush(); if (!*file) throw std::runtime_error("Failed to write benchmark results");
        }
    }
};
} // namespace benchmark
