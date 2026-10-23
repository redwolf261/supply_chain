# Installation Guide

Tested on Windows 11 with the versions below. Other versions and operating systems have not been tested.

## 1. Prerequisites

| Tool | Tested version | Needed for | Check |
|---|---|---|---|
| MinGW-w64 g++ (C++17, **posix** thread model) | 15.2.0 | the C++ backend | `g++ --version` |
| CMake | 4.2.1 (3.16 or newer required by `CMakeLists.txt`) | building | `cmake --version` |
| Ninja | (any recent) | the build generator used below | `ninja --version` |
| Node.js + npm | Node 24.18, npm 11.16 | the dashboard | `node -v`, `npm -v` |
| Google Chrome or Microsoft Edge | installed copy | only for the browser test (`npm run e2e`) | |
| Python 3 with pandas, numpy, matplotlib | Python 3.13 | only for analysing and plotting experiments | `python -c "import pandas, numpy, matplotlib"` |

The C++ code uses `std::thread` (HTTP server and its test), which requires a MinGW-w64 build with the posix thread model. Header-only libraries (nlohmann/json 3.11.3, cpp-httplib 0.18.3) are already included in `backend/third_party`; nothing else is downloaded for the backend.

Disk space: about 130 MB for `frontend/node_modules`, a few MB for the build.

## 2. Build and test the backend

From the project root (`SupplyChainX`):

```powershell
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: `100% tests passed, 0 tests failed out of 12`. The build produces:

| Executable (in `build\backend`) | Purpose |
|---|---|
| `scx_server.exe` | the JSON API used by the dashboard (statically linked: runs without MinGW DLLs on `PATH`) |
| `scx_experiments.exe` | runs the standard experiments A to G |
| `scx_advanced_demo.exe` | prints worked examples of DP, backtracking, merge sort and topological sort |
| `test_*.exe` | the test suites run by CTest |

## 3. Install, test and build the dashboard

```powershell
cd frontend
npm ci            # installs exactly the versions in package-lock.json (npm install also works)
npm test          # unit tests
npm run build     # type-check and production build into frontend\dist
cd ..
```

## 4. Run

```powershell
powershell -ExecutionPolicy Bypass -File scripts\start.ps1
```

This starts the API on `http://127.0.0.1:8080` and the dashboard on `http://127.0.0.1:5173` (open it in a browser); Ctrl+C stops both. Options: `-Production` serves the built dashboard on port 4173 instead of the development server; `-ApiPort 9000` uses another API port (the dashboard is pointed at it automatically).

Running the pieces by hand:

```powershell
build\backend\scx_server.exe --port 8080 --data-dir data     # from the project root, so that data\ is found
cd frontend; npm run dev                                       # or: npm run preview (after npm run build)
```

Both servers listen on 127.0.0.1 only. The dashboard reaches the API through the Vite proxy (`/api`); set the environment variable `SCX_BACKEND` to point the proxy at another address.

## 5. Optional verification

```powershell
powershell -ExecutionPolicy Bypass -File scripts\smoke_test_api.ps1   # real server over HTTP from a separate process
cd frontend; npm run e2e                                                # real browser against the real server (needs ports 8080 and 4173 free)
```

The browser test looks for Chrome, then Edge, in their standard locations; set `SCX_BROWSER` to the path of another Chromium-based browser if needed. Screenshots go to `frontend\e2e-output`.

## 6. Experiments (optional)

```powershell
build\backend\scx_experiments.exe --list
build\backend\scx_experiments.exe --out experiments\results        # about 85 seconds
python experiments\scripts\analyze.py > experiments\results\analysis.txt
python experiments\scripts\plot_results.py
```

`--runs-scale 0.1` makes a quick, smaller run; `--only F` runs a single experiment. Running the full set overwrites the committed results in `experiments\results` (the numbers will be identical except for measured times and memory).

## 7. Troubleshooting

| Symptom | Cause and fix |
|---|---|
| `cannot listen on 127.0.0.1:8080` | another program uses the port: stop it, or use `scripts\start.ps1 -ApiPort 9000` |
| Dashboard shows "Engine unreachable" | the API is not running, or runs on another port than the proxy expects: start it with `start.ps1`, or set `SCX_BACKEND` |
| `scx_server.exe not found` | build the backend first (section 2) |
| PowerShell refuses to run the scripts | run them with `powershell -ExecutionPolicy Bypass -File ...` as shown |
| Build errors about `std::thread` | the compiler uses the win32 thread model: install a MinGW-w64 build with posix threads |
| `npm run e2e`: "No Chrome/Edge found" | install Chrome or set `SCX_BROWSER` |
| `npm run e2e` fails to start the preview server | port 4173 or 8080 busy: close the program using it (`Get-NetTCPConnection -LocalPort 4173`) |
| Python scripts fail on import | `pip install pandas numpy matplotlib` |
