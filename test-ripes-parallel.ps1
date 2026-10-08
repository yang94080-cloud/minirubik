# Run independent copies of the existing, previously tested Ripes runner.
# Stop the old sequential runner before using its result directory here.
# Save this file beside solver-rv32i.s, build-renderer.ps1 and test-ripes.ps1.
# First try four new cases:
# powershell -NoProfile -ExecutionPolicy Bypass -File .\test-ripes-parallel.ps1 -Workers 4 -Limit 4 -OutDir .\results-parallel-check
# Then reuse that merged CSV and continue in a new directory:
# powershell -NoProfile -ExecutionPolicy Bypass -File .\test-ripes-parallel.ps1 -Workers 4 -PreviousResults .\results-parallel-check -OutDir .\results-depth11-parallel
# Each worker retains its reports under worker-N\results; earlier reports remain
# in PreviousResults. Existing result directories and solver sources are not edited.
# This wrapper has not been executed under Windows PowerShell by the assistant.
# A partial run is not the full gate. Budget failures remain failures after merging.
param(
    [string]$RipesPath = 'C:\Users\User\Desktop\Ripes-v2.2.6-106-g5b8a616-win-x86_64\Ripes.exe',
    [string]$CasesCsv = (Join-Path $PSScriptRoot 'depth11-cases.csv'),
    [string]$PreviousResults = (Join-Path $PSScriptRoot 'results-depth11-iss'),
    [string]$OutDir = (Join-Path $PSScriptRoot 'results-depth11-parallel'),
    [ValidateRange(1,8)][int]$Workers = 4,
    [ValidateRange(0,2644)][int]$Limit = 0,
    [ValidateRange(1,86400)][int]$TimeoutSeconds = 600
)
$ErrorActionPreference = 'Stop'
$RipesPath = (Resolve-Path -LiteralPath $RipesPath).Path
$CasesCsv = (Resolve-Path -LiteralPath $CasesCsv).Path
$PreviousResults = (Resolve-Path -LiteralPath $PreviousResults).Path
$names = @('solver-rv32i.s','build-renderer.ps1','test-ripes.ps1')
$sourcePaths = @($names | ForEach-Object {
    (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot $_)).Path
})
$identity = [ordered]@{
    source_sha256 = (Get-FileHash -LiteralPath $sourcePaths[0]).Hash
    builder_sha256 = (Get-FileHash -LiteralPath $sourcePaths[1]).Hash
    tester_sha256 = (Get-FileHash -LiteralPath $sourcePaths[2]).Hash
    ripes_sha256 = (Get-FileHash -LiteralPath $RipesPath).Hash
    cases_sha256 = (Get-FileHash -LiteralPath $CasesCsv).Hash
    processor = 'RV32_ISS'
}
$previousManifest = Get-Content -LiteralPath (Join-Path $PreviousResults 'manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
foreach ($key in $identity.Keys) {
    if ($previousManifest.$key -ne $identity[$key]) {
        throw "Previous results do not match current $key. Do not mix revisions."
    }
}
$cases = @(Import-Csv -LiteralPath $CasesCsv)
$expected = @{}
foreach ($case in $cases) {
    foreach ($field in 'rank','p','o','state','distance') {
        if (!$case.PSObject.Properties[$field]) { throw "Missing case field: $field" }
    }
    $p = [int]$case.p; $o = [int]$case.o
    if ($p -lt 0 -or $p -ge 5040 -or $o -lt 0 -or $o -ge 729 -or
        [int]$case.rank -ne ($p * 729 + $o) -or [int]$case.distance -ne 11) {
        throw "Invalid distance-11 case: $($case.rank)"
    }
    if ($expected.ContainsKey($case.rank)) { throw 'Duplicate input rank.' }
    $expected[$case.rank] = $case
}
if ($cases.Count -ne 2644) { throw 'Expected the complete 2644-row distance-11 CSV.' }

function Assert-ResultRow($row) {
    foreach ($field in 'rank','p','o','state','expected_length','processor','instructions','length','replay_status','error') {
        if (!$row.PSObject.Properties[$field]) { throw "Missing result field: $field" }
    }
    if (!$expected.ContainsKey($row.rank)) { throw "Unexpected result rank: $($row.rank)" }
    $case = $expected[$row.rank]
    if ($row.p -ne $case.p -or $row.o -ne $case.o -or $row.state -ne $case.state -or
        $row.expected_length -ne $case.distance -or $row.processor -ne 'RV32_ISS') {
        throw "Result does not match input: $($row.rank)"
    }
    if ($row.error -eq '' -and ($row.instructions -notmatch '^\d+$' -or
        $row.length -notmatch '^-?\d+$' -or $row.replay_status -notmatch '^\d+$')) {
        throw "Invalid measurement fields: $($row.rank)"
    }
}

$oldCsv = Join-Path $PreviousResults 'results.csv'
$oldHash = (Get-FileHash -LiteralPath $oldCsv).Hash
$oldRows = @(Import-Csv -LiteralPath $oldCsv)
if ((Get-FileHash -LiteralPath $oldCsv).Hash -ne $oldHash) {
    throw 'Previous CSV is changing. Stop the sequential runner first.'
}
$done = @{}
$seenOld = @{}
$baseline = @(foreach ($row in $oldRows) {
    Assert-ResultRow $row
    if ($seenOld.ContainsKey($row.rank)) { throw 'Duplicate rank in previous results.' }
    $seenOld[$row.rank] = $true
    # Keep successful measurements even if they exceed the instruction budget.
    # Execution or correctness failures are eligible to run again.
    if ($row.error -eq '' -and [int]$row.length -eq 11 -and [int]$row.replay_status -eq 0) {
        $done[$row.rank] = $true
        $row
    }
})
$remaining = @($cases | Where-Object { !$done.ContainsKey($_.rank) })
$selected = if ($Limit -gt 0) { @($remaining | Select-Object -First $Limit) } else { $remaining }
if (Test-Path -LiteralPath $OutDir) {
    throw 'Choose a new OutDir. Existing results are never overwritten.'
}
New-Item -ItemType Directory -Path $OutDir | Out-Null
$OutDir = (Resolve-Path -LiteralPath $OutDir).Path
$identity | ConvertTo-Json -Compress | Set-Content -LiteralPath (Join-Path $OutDir 'manifest.json') -Encoding UTF8
$PreviousResults | Set-Content -LiteralPath (Join-Path $OutDir 'previous-results-path.txt') -Encoding UTF8
Copy-Item -LiteralPath $oldCsv -Destination (Join-Path $OutDir 'previous-results.csv')
$baseline | Export-Csv -LiteralPath (Join-Path $OutDir 'results.csv') -NoTypeInformation -Encoding UTF8
@{
    workers=$Workers; limit=$Limit; previous_csv_sha256=$oldHash
    wrapper_sha256=(Get-FileHash -LiteralPath $PSCommandPath).Hash
    timestamp_utc=[DateTime]::UtcNow.ToString('o')
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutDir 'parallel-run.json') -Encoding UTF8

$jobs = @()
$workerDirs = @()
Write-Host "Reusing $($baseline.Count) measured cases; scheduling $($selected.Count) remaining cases across $Workers workers."
$timer = [System.Diagnostics.Stopwatch]::StartNew()
try {
    for ($worker = 0; $worker -lt $Workers; ++$worker) {
        $shard = @(for ($index=$worker; $index -lt $selected.Count; $index += $Workers) { $selected[$index] })
        if ($shard.Count -eq 0) { continue }
        $dir = Join-Path $OutDir ("worker-{0}" -f ($worker+1))
        New-Item -ItemType Directory -Path $dir | Out-Null
        foreach ($path in $sourcePaths) { Copy-Item -LiteralPath $path -Destination $dir }
        $hashKeys = @('source_sha256','builder_sha256','tester_sha256')
        for ($i=0; $i -lt $names.Count; ++$i) {
            if ((Get-FileHash -LiteralPath (Join-Path $dir $names[$i])).Hash -ne
                $identity[$hashKeys[$i]]) { throw 'Source snapshot mismatch.' }
        }
        $shard | Export-Csv -LiteralPath (Join-Path $dir 'cases.csv') -NoTypeInformation -Encoding UTF8
        $workerDirs += $dir
        $jobs += Start-Job -ArgumentList $dir,$RipesPath,$TimeoutSeconds -ScriptBlock {
            param($dir,$exe,$timeout)
            $ErrorActionPreference = 'Stop'
            try {
                & (Join-Path $dir 'test-ripes.ps1') -RipesPath $exe -CasesCsv (Join-Path $dir 'cases.csv') -Processor RV32_ISS -OutDir (Join-Path $dir 'results') -TimeoutSeconds $timeout *>&1 |
                    Out-File -LiteralPath (Join-Path $dir 'worker.log') -Encoding UTF8
            } catch {
                # The existing runner throws after a completed over-budget batch.
                # Retain that message; the parent evaluates the actual CSV rows.
                $_ | Out-String | Add-Content -LiteralPath (Join-Path $dir 'worker.log') -Encoding UTF8
            }
        }
    }
    while (@($jobs | Where-Object { $_.State -eq 'Running' -or $_.State -eq 'NotStarted' }).Count -gt 0) {
        foreach ($job in $jobs) { Receive-Job -Job $job -ErrorAction Continue }
        $count = $baseline.Count
        foreach ($dir in $workerDirs) {
            $path = Join-Path $dir 'results\results.csv'
            if (Test-Path -LiteralPath $path) { $count += @(Import-Csv -LiteralPath $path).Count }
        }
        Write-Host "Recorded so far: $count / 2644; new parallel batch wall time: $([math]::Round($timer.Elapsed.TotalMinutes,1)) min"
        Start-Sleep -Seconds 5
    }
    foreach ($job in $jobs) { Receive-Job -Job $job -ErrorAction Continue }
} finally {
    foreach ($job in $jobs) {
        if ($job.State -eq 'Running' -or $job.State -eq 'NotStarted') { Stop-Job -Job $job }
        Remove-Job -Job $job -Force -ErrorAction SilentlyContinue
    }
    $timer.Stop()
    # Preserve completed worker rows even if the parent is interrupted.
    $merged = @($baseline)
    foreach ($dir in $workerDirs) {
        $path = Join-Path $dir 'results\results.csv'
        if (Test-Path -LiteralPath $path) { $merged += @(Import-Csv -LiteralPath $path) }
    }
    $seen = @{}
    foreach ($row in $merged) {
        Assert-ResultRow $row
        if ($seen.ContainsKey($row.rank)) { throw "Duplicate merged rank: $($row.rank)" }
        $seen[$row.rank] = $true
    }
    $merged | Sort-Object { [int]$_.rank } | Export-Csv -LiteralPath (Join-Path $OutDir 'results.csv') -NoTypeInformation -Encoding UTF8
}
$errors = @($merged | Where-Object { $_.error -ne '' }).Count
$valid = @($merged | Where-Object { $_.error -eq '' })
$lengthFailures = @($valid | Where-Object { [int]$_.length -ne 11 }).Count
$replayFailures = @($valid | Where-Object { [int]$_.replay_status -ne 0 }).Count
$budgetFailures = @($valid | Where-Object { [long]$_.instructions -gt 50000000 }).Count
$maximum = ($valid | ForEach-Object { [long]$_.instructions } | Measure-Object -Maximum).Maximum
$complete = $merged.Count -eq $cases.Count
$passed = $complete -and $errors -eq 0 -and $lengthFailures -eq 0 -and $replayFailures -eq 0 -and $budgetFailures -eq 0
$summary = @(
    'Processor: RV32_ISS', "Completed: $($merged.Count) / 2644",
    "Execution/report errors: $errors", "Length failures: $lengthFailures",
    "Replay failures: $replayFailures", "Budget failures: $budgetFailures",
    "Maximum retired instructions among valid reports: $maximum",
    "New parallel batch wall time: $([math]::Round($timer.Elapsed.TotalSeconds,3)) seconds",
    "All 2644 distance-11 ISS cases completed: $complete",
    "Full distance-11 ISS gate passed: $passed"
)
$summary | Set-Content -LiteralPath (Join-Path $OutDir 'summary.txt') -Encoding UTF8
$summary | ForEach-Object { Write-Host $_ }
if (!$complete) { Write-Host 'Partial run. Use this OutDir as PreviousResults and a new OutDir to continue.' }
if ($errors -gt 0 -or $lengthFailures -gt 0 -or $replayFailures -gt 0 -or $budgetFailures -gt 0) { exit 1 }
