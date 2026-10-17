# Starts the C++ API server and the React dev server together; Ctrl+C stops both.
#   powershell -File scripts\start.ps1            (dev server with hot reload on http://127.0.0.1:5173)
#   powershell -File scripts\start.ps1 -Production (serves the built frontend on http://127.0.0.1:4173)
# Build first:  cmake -S . -B build -G Ninja ; cmake --build build ; cd frontend ; npm install ; npm run build
param([switch]$Production, [int]$ApiPort = 8080)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'build\backend\scx_server.exe'
$frontend = Join-Path $root 'frontend'
if (-not (Test-Path $exe)) { throw "scx_server.exe not found. Build the backend first (see README.md)." }
if (-not (Test-Path (Join-Path $frontend 'node_modules'))) { throw "Run 'npm install' in the frontend folder first." }
if ($Production -and -not (Test-Path (Join-Path $frontend 'dist\index.html'))) { throw "Run 'npm run build' in the frontend folder first." }

$server = Start-Process -FilePath $exe -ArgumentList @('--port', $ApiPort, '--data-dir', (Join-Path $root 'data')) -PassThru -WindowStyle Hidden
try {
    for ($i = 0; $i -lt 50; $i++) { try { Invoke-WebRequest "http://127.0.0.1:$ApiPort/api/health" -UseBasicParsing | Out-Null; break } catch { Start-Sleep -Milliseconds 100 } }
    Write-Host "C++ API running on http://127.0.0.1:$ApiPort"
    $env:SCX_BACKEND = "http://127.0.0.1:$ApiPort"   # vite.config.ts proxies /api to this address
    Push-Location $frontend
    if ($Production) { Write-Host 'Open http://127.0.0.1:4173'; & npm run preview }
    else { Write-Host 'Open http://127.0.0.1:5173'; & npm run dev }
} finally {
    Pop-Location
    if (-not $server.HasExited) { Stop-Process -Id $server.Id -Force }
}
