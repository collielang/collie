# run_all.ps1 - batch verifier for Collie examples
# Usage:  powershell -ExecutionPolicy Bypass -File examples\run_all.ps1
# Exit 0 = all results match expectation; exit 1 = mismatch found; exit 2 = setup error.
#
# Categories:
#   expectPass - must exit 0 (runnable subset, checked outputs live in each README)
#   expectFail - must exit non-zero today (spec-target / diagnostics samples).
#                When one of these starts passing, it is reported as UPGRADE:
#                the language grew a feature -> update that example's README + dashboard.

param([string]$Exe = "")

$root = Split-Path -Parent $PSScriptRoot
if ($Exe -eq "") { $Exe = Join-Path $root "compiler\build\Release\collie.exe" }
if (-not (Test-Path $Exe)) { Write-Host "[SETUP] collie.exe not found: $Exe"; exit 2 }

$expectPass = @(
    "basics\a01-hello-world\main.collie",
    "basics\a02-variables-types\main.collie",
    "basics\a03-numeric\main.collie",
    "basics\a04-strings\main.collie",
    "basics\a05-logic-tribool\main.collie",
    "basics\a06-control-flow\main.collie",
    "basics\a07-for-variants\main.collie",
    "basics\a09-functions\main.collie",
    "basics\a10-arrays-collections\main.collie",
    "basics\a11-classes\main.collie",
    "basics\a12-inheritance\main.collie",
    "practical\b01-fizzbuzz\main.collie",
    "practical\b02-sorting\main.collie",
    "practical\b03-binary-search\main.collie",
    "practical\b04-string-toolkit\main.collie",
    "practical\b05-number-utils\main.collie",
    "practical\b06-statistics\main.collie",
    "practical\b07-bank-account\main.collie",
    "practical\b08-state-machine\main.collie",
    "practical\b09-matrix\main.collie",
    "practical\b10-json-builder\main.collie",
    "edge-cases\c01-numeric-limits\main.collie",
    "edge-cases\c02-string-edge\main.collie",
    "edge-cases\c03-array-edge\main.collie",
    "edge-cases\c04-deep-recursion\main.collie",
    "edge-cases\c04-deep-recursion\probe.collie",
    "edge-cases\c05-scope-shadowing\main.collie",
    "stress\d01-loop-throughput\main.collie",
    "stress\d02-string-concat\main.collie",
    "stress\d03-array-churn\main.collie",
    "stress\d04-function-call-overhead\main.collie",
    "stress\d05-object-churn\main.collie",
    "stress\d06-nested-loops\main.collie",
    "diagnostics\e03-runtime-errors\tonumber_probe.collie"
)

$expectFail = @(
    # spec-target examples, waiting for features
    "basics\a08-labels\main.collie",
    "basics\a13-enum\main.collie",
    "basics\a14-tuple-destructuring\main.collie",
    "basics\a15-error-handling\main.collie",
    "basics\a16-bitwise\main.collie",
    "basics\a02-variables-types\future.collie",
    "basics\a05-logic-tribool\future.collie",
    "basics\a07-for-variants\future.collie",
    "basics\a09-functions\future.collie",
    "basics\a10-arrays-collections\future.collie",
    "basics\a12-inheritance\future.collie",
    # intentional-failure attachments
    "edge-cases\c03-array-edge\oob.collie",
    # diagnostics series (failure IS the test)
    "diagnostics\e01-syntax-errors\main.collie",
    "diagnostics\e01-syntax-errors\lexer_fatal.collie",
    "diagnostics\e02-semantic-errors\main.collie",
    "diagnostics\e03-runtime-errors\main.collie",
    "diagnostics\e03-runtime-errors\string_oob.collie"
)

function Invoke-Collie([string]$rel) {
    $full = Join-Path (Join-Path $root "examples") $rel
    if (-not (Test-Path $full)) { return @{ code = -999; secs = 0 } }
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    & $Exe $full *> $null
    $sw.Stop()
    return @{ code = $LASTEXITCODE; secs = [Math]::Round($sw.Elapsed.TotalSeconds, 2) }
}

$okPass = 0; $okFail = 0; $broken = @(); $upgraded = @(); $missing = @()

Write-Host "=== expectPass ($($expectPass.Count)) ==="
foreach ($rel in $expectPass) {
    $r = Invoke-Collie $rel
    if ($r.code -eq -999) { $missing += $rel; Write-Host ("MISSING  {0}" -f $rel); continue }
    if ($r.code -eq 0) { $okPass++; Write-Host ("PASS     {0}  ({1}s)" -f $rel, $r.secs) }
    else { $broken += $rel; Write-Host ("BROKEN   {0}  exit={1}" -f $rel, $r.code) }
}

Write-Host ""
Write-Host "=== expectFail ($($expectFail.Count)) ==="
foreach ($rel in $expectFail) {
    $r = Invoke-Collie $rel
    if ($r.code -eq -999) { $missing += $rel; Write-Host ("MISSING  {0}" -f $rel); continue }
    if ($r.code -ne 0) { $okFail++; Write-Host ("XFAIL    {0}  exit={1}" -f $rel, $r.code) }
    else { $upgraded += $rel; Write-Host ("UPGRADE  {0}  now exits 0!" -f $rel) }
}

# catch stray .collie files not covered by either list
$known = $expectPass + $expectFail
$all = Get-ChildItem -Path (Join-Path $root "examples") -Recurse -Filter *.collie |
       ForEach-Object { $_.FullName.Substring((Join-Path $root "examples").Length + 1) }
$unlisted = $all | Where-Object { $known -notcontains $_ }

Write-Host ""
Write-Host "=== summary ==="
Write-Host ("PASS ok     : {0}/{1}" -f $okPass, $expectPass.Count)
Write-Host ("XFAIL ok    : {0}/{1}" -f $okFail, $expectFail.Count)
if ($broken.Count)   { Write-Host ("BROKEN      : {0}  <- regressions, fix or update README" -f ($broken -join ", ")) }
if ($upgraded.Count) { Write-Host ("UPGRADE     : {0}  <- new features live, update dashboard" -f ($upgraded -join ", ")) }
if ($missing.Count)  { Write-Host ("MISSING     : {0}" -f ($missing -join ", ")) }
if ($unlisted.Count) { Write-Host ("UNLISTED    : {0}  <- add to run_all.ps1" -f ($unlisted -join ", ")) }

if ($broken.Count -or $missing.Count) { exit 1 } else { exit 0 }
