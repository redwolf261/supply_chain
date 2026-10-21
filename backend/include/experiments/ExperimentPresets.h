#pragma once
// The seven experiments required by the specification (A-G), defined as data so they are inspectable, testable
// and exactly reproducible. Each preset sweeps one value `x` (see ExperimentRunner.h for its meaning) and runs the
// same seeded scenarios for every strategy at every x.

#include <string>
#include <vector>

#include "experiments/ExperimentRunner.h"

namespace scx {

struct ExperimentPreset {
    std::string id;                 // "A".."G"
    std::string title;
    std::string question;           // what the experiment asks, in one sentence
    std::string xLabel;             // what the swept value means
    std::vector<double> xValues;
    ExperimentConfig config;        // template; `x` is replaced for each sweep point
    std::string pairedReference;    // label every other strategy is paired against in reports ("" = none)
};

std::vector<ExperimentPreset> standardExperiments();

// Runs a preset: one ExperimentResult per x value. `runsScale` multiplies the number of runs (at least 1 run
// remains) so a quick smoke run can be made without changing the definition.
struct SweepPoint { double x; ExperimentResult result; };
std::vector<SweepPoint> runPreset(const ExperimentPreset& preset, double runsScale = 1.0);

}  // namespace scx
