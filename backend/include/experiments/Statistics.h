#pragma once
// Descriptive statistics used to report experiments honestly: every figure carries its sample size and spread.

#include <vector>

namespace scx {

struct Summary {
    int n = 0;
    double mean = 0.0;
    double stddev = 0.0;          // sample standard deviation (divides by n - 1); 0 when n < 2
    double min = 0.0;
    double max = 0.0;
    double ci95HalfWidth = 0.0;   // 95% confidence interval of the mean = mean +/- this (Student t); 0 when n < 2
};

// Two-sided 95% critical value of Student's t distribution (exact table for 1..30 degrees of freedom,
// the normal value 1.96 beyond that). Throws std::invalid_argument for degreesOfFreedom < 1.
double tCritical95(int degreesOfFreedom);

Summary summarize(const std::vector<double>& values);

// Paired comparison of two measurements taken on the SAME scenarios (a[i] and b[i] come from run i).
// difference = a - b, so a negative mean means `a` is cheaper on average. "Better" means lower.
struct PairedSummary {
    int n = 0;
    double meanDifference = 0.0;
    double stddev = 0.0;
    double ci95HalfWidth = 0.0;     // an interval that excludes 0 is evidence of a real difference
    int firstBetter = 0;            // runs where a < b
    int secondBetter = 0;           // runs where b < a
    int ties = 0;
};

// Throws std::invalid_argument if the vectors differ in length.
PairedSummary pairedCompare(const std::vector<double>& a, const std::vector<double>& b);

}  // namespace scx
