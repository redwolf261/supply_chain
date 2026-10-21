#include "experiments/Statistics.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace scx {

double tCritical95(int degreesOfFreedom) {
    static const double table[] = {
        12.706, 4.303, 3.182, 2.776, 2.571, 2.447, 2.365, 2.306, 2.262, 2.228,
        2.201, 2.179, 2.160, 2.145, 2.131, 2.120, 2.110, 2.101, 2.093, 2.086,
        2.080, 2.074, 2.069, 2.064, 2.060, 2.056, 2.052, 2.048, 2.045, 2.042};
    if (degreesOfFreedom < 1) throw std::invalid_argument("degrees of freedom must be at least 1");
    return degreesOfFreedom <= 30 ? table[degreesOfFreedom - 1] : 1.96;
}

Summary summarize(const std::vector<double>& values) {
    Summary s;
    s.n = static_cast<int>(values.size());
    if (s.n == 0) return s;
    s.min = *std::min_element(values.begin(), values.end());
    s.max = *std::max_element(values.begin(), values.end());
    double sum = 0.0;
    for (double v : values) sum += v;
    s.mean = sum / s.n;
    if (s.n >= 2) {
        double squares = 0.0;
        for (double v : values) squares += (v - s.mean) * (v - s.mean);
        s.stddev = std::sqrt(squares / (s.n - 1));
        s.ci95HalfWidth = tCritical95(s.n - 1) * s.stddev / std::sqrt(static_cast<double>(s.n));
    }
    return s;
}

PairedSummary pairedCompare(const std::vector<double>& a, const std::vector<double>& b) {
    if (a.size() != b.size()) throw std::invalid_argument("paired samples must have the same length");
    std::vector<double> differences;
    PairedSummary result;
    for (size_t i = 0; i < a.size(); ++i) {
        differences.push_back(a[i] - b[i]);
        if (a[i] < b[i]) ++result.firstBetter;
        else if (b[i] < a[i]) ++result.secondBetter;
        else ++result.ties;
    }
    const Summary s = summarize(differences);
    result.n = s.n;
    result.meanDifference = s.mean;
    result.stddev = s.stddev;
    result.ci95HalfWidth = s.ci95HalfWidth;
    return result;
}

}  // namespace scx
