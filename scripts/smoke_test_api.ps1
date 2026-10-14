# Starts the real scx_server.exe, runs the specification's workflow over HTTP from a separate process,
# prints what happened, and stops the server. Exit code 0 only if every check passed.
# Usage (from the project root, after building):  powershell -File scripts\smoke_test_api.ps1 [-Port 8099]
param([int]$Port = 8099)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'build\backend\scx_server.exe'
if (-not (Test-Path $exe)) { throw "scx_server.exe not found at $exe - build the project first" }

$failures = 0
function Check($name, $condition) {
    if ($condition) { Write-Host "[ ok ] $name" } else { Write-Host "[FAIL] $name"; $script:failures++ }
}
$base = "http://127.0.0.1:$Port"
function ParseJson($text) { try { return ($text | ConvertFrom-Json) } catch { return $null } }
function Api($method, $path, $body = $null) {
    $args = @{ Uri = "$base$path"; Method = $method; UseBasicParsing = $true; ContentType = 'application/json' }
    if ($null -ne $body) { $args.Body = ($body | ConvertTo-Json -Depth 20 -Compress) }
    try {
        $r = Invoke-WebRequest @args
        return @{ Status = [int]$r.StatusCode; Json = (ParseJson $r.Content); Raw = $r.Content }
    } catch {
        if ($null -eq $_.Exception.Response) { throw }
        # Windows PowerShell 5.1 puts the HTTP error body in ErrorDetails.
        $text = $_.ErrorDetails.Message
        return @{ Status = [int]$_.Exception.Response.StatusCode; Json = (ParseJson $text); Raw = $text }
    }
}

$server = Start-Process -FilePath $exe -ArgumentList @('--port', $Port, '--data-dir', (Join-Path $root 'data')) -PassThru -WindowStyle Hidden
try {
    for ($i = 0; $i -lt 50; $i++) { try { Invoke-WebRequest "$base/api/health" -UseBasicParsing | Out-Null; break } catch { Start-Sleep -Milliseconds 100 } }

    $health = Api GET '/api/health'
    Check 'health reports ok and memory tracking' ($health.Json.status -eq 'ok' -and $health.Json.memoryTrackingActive)
    Check 'network before load is a 409 with a clear message' ((Api GET '/api/network').Status -eq 409)

    $load = Api POST '/api/scenario/load' @{ file = 'small_demo' }
    Check 'load small_demo (9 nodes)' ($load.Status -eq 200 -and $load.Json.network.nodes.Count -eq 9)
    Check 'bad scenario name rejected' ((Api POST '/api/scenario/load' @{ file = '../x' }).Status -eq 400)

    Api POST '/api/disruption/apply' @{ type = 'supplier_failure'; node = 'S1' } | Out-Null
    $down = Api POST '/api/disruption/apply' @{ type = 'supplier_failure'; node = 'S2' }
    $unmetDown = $down.Json.metrics.unmetDemand
    Check "two supplier failures leave unmet demand ($unmetDown)" ($unmetDown -gt 0)

    $greedy = Api POST '/api/algorithm/run' @{ algorithm = 'greedy'; execute = $true }
    Check 'greedy activates one backup and reduces unmet demand' ($greedy.Json.plan.actions.Count -eq 1 -and $greedy.Json.metrics.unmetDemand -lt $unmetDown)
    Check 'run reports runtime and real peak memory' ($greedy.Json.runtimeMs -ge 0 -and $greedy.Json.peakMemoryBytes -gt 0)

    $back = Api POST '/api/scenario/reset' @{ to = 'disrupted' }
    Check 'reset to disrupted state restores the same unmet demand' ($back.Json.metrics.unmetDemand -eq $unmetDown)

    $flow = Api POST '/api/algorithm/run' @{ algorithm = 'min_cost_flow' }
    $cmp = Api POST '/api/algorithms/compare' @{ strategies = @('min_cost_flow', 'greedy', 'dijkstra_dispatch') }
    Check 'compare returns 3 feasible rows' ($cmp.Json.results.Count -eq 3 -and -not ($cmp.Json.results | Where-Object { -not $_.feasible }))
    $mcfRow = $cmp.Json.results | Where-Object { $_.strategy -eq 'min_cost_flow' }
    Check 'compare agrees with the individual min_cost_flow run' ($mcfRow.metrics.totalCost -eq $flow.Json.metrics.totalCost)
    Write-Host ("       lowest total cost: {0}" -f $cmp.Json.lowestTotalCost)

    $bad = Api POST '/api/algorithm/run' @{ algorithm = 'bfs'; source = 'Nowhere' }
    Check 'unknown node gives 400 with error body' ($bad.Status -eq 400 -and $bad.Json.error.code -eq 'unknown_node')

    $csv = Api GET '/api/results?format=csv'
    Check 'CSV export has a header and rows' ($csv.Raw.StartsWith('id,timestamp') -and ($csv.Raw -split "`n").Count -ge 5)
    Check 'JSON results export lists runs' ((Api GET '/api/results').Json.results.Count -ge 5)
}
finally {
    if (-not $server.HasExited) { Stop-Process -Id $server.Id -Force }
}
if ($failures -gt 0) { Write-Host "$failures check(s) FAILED"; exit 1 }
Write-Host 'All smoke checks passed.'
