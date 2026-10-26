// Runs the standard multi-day experiments M1-M10 and saves measured results.
//   scx_multiday_experiments [--out DIR] [--only M3] [--runs-scale 0.2] [--list]
// Writes per experiment <ID>_runs.csv (one row per run and policy), <ID>_daily.csv (one row per day) and
// <ID>_summary.csv (per setting and policy). Linked with the allocation hooks, so it is statically linked too.
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "experiments/MultiDayExperiments.h"

using namespace scx;

int main(int argc, char** argv) {
    std::string outDir = "experiments/results/multiday", only;
    double runsScale = 1.0;
    bool listOnly = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--out" && i + 1 < argc) outDir = argv[++i];
        else if (a == "--only" && i + 1 < argc) only = argv[++i];
        else if (a == "--runs-scale" && i + 1 < argc) runsScale = std::atof(argv[++i]);
        else if (a == "--list") listOnly = true;
        else { std::cerr << "usage: scx_multiday_experiments [--out DIR] [--only M1-M10] [--runs-scale F] [--list]\n"; return 2; }
    }
    if (!(runsScale > 0.0 && runsScale <= 10.0)) { std::cerr << "--runs-scale must be in (0, 10]\n"; return 2; }
    const std::vector<MultiDayPreset> presets = standardMultiDayExperiments();
    if (listOnly) {
        for (const MultiDayPreset& p : presets) std::cout << p.id << "  " << p.title << "  (" << p.xValues.size() << " settings x " << p.config.runs << " runs)\n    " << p.question << "\n";
        return 0;
    }
    std::filesystem::create_directories(outDir);
    bool ran = false;
    for (const MultiDayPreset& p : presets) {
        if (!only.empty() && only != p.id) continue;
        ran = true;
        std::cout << "Experiment " << p.id << ": " << p.title << " ..." << std::flush;
        const auto started = std::chrono::steady_clock::now();
        std::string runs = multiDayRunsCsvHeader(), daily = multiDayDailyCsvHeader(), summary = multiDaySummaryCsvHeader();
        for (double x : p.xValues) {
            MultiDayExperimentConfig c = applySweep(p, x);
            c.runs = std::max(1, static_cast<int>(std::lround(c.runs * runsScale)));
            const MultiDayExperimentResult r = runMultiDayExperiment(c);
            runs += multiDayRunsCsvRows(p.id, x, r);
            daily += multiDayDailyCsvRows(p.id, x, r);
            summary += multiDaySummaryCsvRows(p.id, x, r);
        }
        std::ofstream(std::filesystem::path(outDir) / (p.id + "_runs.csv"), std::ios::binary) << runs;
        std::ofstream(std::filesystem::path(outDir) / (p.id + "_daily.csv"), std::ios::binary) << daily;
        std::ofstream(std::filesystem::path(outDir) / (p.id + "_summary.csv"), std::ios::binary) << summary;
        std::cout << " done in " << std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count() << " s\n";
    }
    if (!ran) { std::cerr << "no experiment with id '" << only << "'\n"; return 2; }
    std::cout << "Results written to " << outDir << "\n";
    return 0;
}
