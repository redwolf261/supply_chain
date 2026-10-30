<#
.SYNOPSIS
    Starts SupplyChainX (C++ Backend + React Frontend) from any PowerShell prompt.
.DESCRIPTION
    Can be run from ANY working directory in PowerShell.
    Automatically:
      - Adds MinGW compiler toolchain to PATH if found
      - Frees up port 8080 if an old instance is lingering
      - Compiles backend if scx_server.exe is missing
      - Installs frontend packages if node_modules is missing
      - Launches the C++ API engine on http://127.0.0.1:8080
      - Launches the React Vite dashboard on http://127.0.0.1:5173
      - Automatically opens http://localhost:5173 in your default browser
      - Stops all processes cleanly on Ctrl+C
#>
param(
    [switch]$Production,
    [int]$ApiPort = 8080,
    [switch]$NoBrowser
)

$ErrorActionPreference = 'Stop'

# Determine absolute project root regardless of current working directory
$Root = if ($PSScriptRoot -and (Test-Path (Join-Path $PSScriptRoot 'data'))) {
    $PSScriptRoot
} elseif ($PSScriptRoot -and (Test-Path (Join-Path (Split-Path -Parent $PSScriptRoot) 'data'))) {
    Split-Path -Parent $PSScriptRoot
} else {
    'C:\Users\Rivan\Projects\DAA_CP'
}

$Exe = Join-Path $Root 'build\backend\scx_server.exe'
$DataDir = Join-Path $Root 'data'
$FrontendDir = Join-Path $Root 'frontend'

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "   SupplyChainX 2.0 - Universal Launcher" -ForegroundColor Cyan
Write-Host "   Project Root: $Root" -ForegroundColor DarkGray
Write-Host "==========================================================" -ForegroundColor Cyan

# 1. Add MinGW to PATH if not already present
$MinGwBin = 'C:\Program Files\CodeBlocks\MinGW\bin'
if ((Test-Path $MinGwBin) -and ($env:Path -notlike "*$MinGwBin*")) {
    $env:Path = "$MinGwBin;$env:Path"
    Write-Host "[+] Configured MinGW compiler toolchain in PATH" -ForegroundColor Green
}

# 2. Stop any dangling scx_server processes
Get-Process -Name scx_server -ErrorAction SilentlyContinue | ForEach-Object {
    Write-Host "[!] Stopping existing scx_server instance (PID $($_.Id))..." -ForegroundColor Yellow
    Stop-Process -Id $_.Id -Force
}

# 3. Check / Build backend if missing
if (-not (Test-Path $Exe)) {
    Write-Host "[*] scx_server.exe not found. Building C++ backend..." -ForegroundColor Yellow
    $BuildDir = Join-Path $Root 'build'
    & cmake -S $Root -B $BuildDir -G Ninja -DCMAKE_BUILD_TYPE=Release
    & cmake --build $BuildDir --target scx_server -j 4
    if (-not (Test-Path $Exe)) {
        throw "Failed to compile scx_server.exe. Check CMake/Ninja output above."
    }
    Write-Host "[+] Backend build successful!" -ForegroundColor Green
}

# 4. Check frontend dependencies
if (-not (Test-Path (Join-Path $FrontendDir 'node_modules'))) {
    Write-Host "[*] Installing frontend dependencies (npm install)..." -ForegroundColor Yellow
    Push-Location $FrontendDir
    try {
        & npm install
    } finally {
        Pop-Location
    }
}

# 5. Start C++ backend
Write-Host "[*] Starting C++ API engine on port $ApiPort..." -ForegroundColor Cyan
$server = Start-Process -FilePath $Exe -ArgumentList @('--port', $ApiPort, '--data-dir', $DataDir) -PassThru -WindowStyle Hidden

$backendReady = $false
for ($i = 0; $i -lt 60; $i++) {
    try {
        $resp = Invoke-WebRequest -Uri "http://127.0.0.1:$ApiPort/api/health" -UseBasicParsing -TimeoutSec 1
        if ($resp.StatusCode -eq 200) {
            $backendReady = $true
            break
        }
    } catch {
        Start-Sleep -Milliseconds 150
    }
}

if (-not $backendReady) {
    if (-not $server.HasExited) { Stop-Process -Id $server.Id -Force }
    throw "C++ API engine failed to respond within 9 seconds on http://127.0.0.1:$ApiPort/api/health"
}

Write-Host "[+] C++ Engine active at: http://127.0.0.1:$ApiPort" -ForegroundColor Green

# 6. Open browser and start frontend
try {
    $env:SCX_BACKEND = "http://127.0.0.1:$ApiPort"
    $targetUrl = if ($Production) { 'http://localhost:4173' } else { 'http://localhost:5173' }

    if (-not $NoBrowser) {
        Start-Job -ScriptBlock {
            param($url)
            Start-Sleep -Seconds 2
            Start-Process $url
        } -ArgumentList $targetUrl | Out-Null
    }

    Write-Host "[+] Frontend launching at: $targetUrl" -ForegroundColor Green
    Write-Host "[i] Press Ctrl+C at any time to gracefully terminate all services." -ForegroundColor DarkGray
    Write-Host ""

    Push-Location $FrontendDir
    if ($Production) {
        if (-not (Test-Path (Join-Path $FrontendDir 'dist\index.html'))) {
            Write-Host "[*] Building production frontend bundle..." -ForegroundColor Yellow
            & npm run build
        }
        & npm run preview
    } else {
        & npm run dev
    }
} finally {
    Pop-Location
    Write-Host "`n[*] Shutting down C++ backend engine..." -ForegroundColor Yellow
    if ($server -and -not $server.HasExited) {
        Stop-Process -Id $server.Id -Force
    }
    Write-Host "[+] All SupplyChainX services stopped cleanly." -ForegroundColor Green
}
