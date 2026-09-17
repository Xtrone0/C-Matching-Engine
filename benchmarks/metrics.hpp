#pragma once
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <vector>

namespace benchmark {
inline double quantile(std::vector<double> values, double fraction) {
    if (values.empty() || !(fraction > 0 && fraction <= 1))
        throw std::invalid_argument("Quantile requires samples and a fraction in (0, 1]");
    std::sort(values.begin(), values.end());
    const auto index = static_cast<std::size_t>(std::ceil(fraction * static_cast<double>(values.size()))) - 1;
    return values.at(index);
}
inline double median(std::vector<double> values) {
    if (values.empty()) throw std::invalid_argument("Median requires samples");
    std::sort(values.begin(), values.end());
    const auto middle = values.size() / 2;
    return values.size() % 2 ? values[middle] : (values[middle - 1] + values[middle]) / 2;
}
inline double median_throughput(const std::vector<double>& costs) {
    std::vector<double> rates;
    rates.reserve(costs.size());
    for (double cost : costs) {
        if (!(cost > 0)) throw std::invalid_argument("Command cost must be positive");
        rates.push_back(1e9 / cost);
    }
    return median(std::move(rates));
}
} // namespace benchmark
