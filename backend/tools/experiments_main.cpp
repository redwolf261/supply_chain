// Runs the standard experiments A-G and saves measured results.
//   scx_experiments [--out DIR] [--only A] [--runs-scale 0.2] [--list]
// Writes, per experiment: <ID>_summary.csv (one row per x and strategy), <ID>_runs.csv (every single run), and one
// all_results.json. Linked with the allocation hooks so peak heap per run is measured.
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "experiments/ExperimentIO.h"
#include "experiments/ExperimentPresets.h"

using namespace scx;

int main(int argc, char** argv) {
    std::string outDir = "experiments/results";
    std::string only;
    double runsScale = 1.0;
    bool listOnly = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--out" && i + 1 < argc) outDir = argv[++i];
        else if (arg == "--only" && i + 1 < argc) only = argv[++i];
        else if (arg == "--runs-scale" && i + 1 < argc) runsScale = std::atof(argv[++i]);
        else if (arg == "--list") listOnly = true;
        else { std::cerr << "usage: scx_experiments [--out DIR] [--only A-G] [--runs-scale F] [--list]\n"; return 2; }
    }
    if (!(runsScale > 0.0 && runsScale <= 10.0)) { std::cerr << "--runs-scale must be in (0, 10]\n"; return 2; }

    const std::vector<ExperimentPreset> presets = standardExperiments();
    if (listOnly) {
        for (const ExperimentPreset& p : presets) std::cout << p.id << "  " << p.title << "  (" << p.xValues.size() << " settings x " << p.config.runs << " runs)\n    " << p.question << "\n";
        return 0;
    }
    std::filesystem::create_directories(outDir);

    nlohmann::json all = nlohmann::json::array();
    bool ran = false;
    for (const ExperimentPreset& preset : presets) {
        if (!only.empty() && only != preset.id) continue;
        ran = true;
        std::cout << "Experiment " << preset.id << ": " << preset.title << " ..." << std::flush;
        const auto started = std::chrono::steady_clock::now();
        const std::vector<SweepPoint> points = runPreset(preset, runsScale);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

        std::string summary = summaryCsvHeader(), runs = runsCsvHeader();
        nlohmann::json settings = nlohmann::json::array();
        for (const SweepPoint& point : points) {
            summary += summaryCsvRows(preset.id, point.x, point.result);
            runs += runsCsvRows(preset.id, point.x, point.result);
            settings.push_back({{"x", point.x}, {"result", experimentResultToJson(point.result, false, preset.pairedReference)}});
        }
        std::ofstream(std::filesystem::path(outDir) / (preset.id + "_summary.csv"), std::ios::binary) << summary;
        std::ofstream(std::filesystem::path(outDir) / (preset.id + "_runs.csv"), std::ios::binary) << runs;
        all.push_back({{"id", preset.id}, {"title", preset.title}, {"question", preset.question}, {"xLabel", preset.xLabel},
                       {"kind", toString(preset.config.kind)}, {"pairedReference", preset.pairedReference},
                       {"runsPerSetting", points.empty() ? 0 : points[0].result.config.runs}, {"baseSeed", preset.config.baseSeed},
                       {"settings", settings}});
        std::cout << " done in " << seconds << " s\n";
    }
    if (!ran) { std::cerr << "no experiment with id '" << only << "'\n"; return 2; }
    std::ofstream(std::filesystem::path(outDir) / "all_results.json", std::ios::binary) << all.dump(1);
    std::cout << "Results written to " << outDir << "\n";
    return 0;
}
