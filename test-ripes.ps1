param(
    [string]$RipesPath = 'C:\Users\User\Desktop\Ripes-v2.2.6-106-g5b8a616-win-x86_64\Ripes.exe',
    [string]$CasesCsv = (Join-Path $PSScriptRoot 'smoke-cases.csv'),
    [ValidateSet('RV32_ISS','RV32_5S')][string]$Processor = 'RV32_ISS',
    [ValidateRange(0,2644)][int]$Limit = 0,
    [string]$OutDir = (Join-Path $PSScriptRoot 'results-smoke-iss'),
    [ValidateRange(1,86400)][int]$TimeoutSeconds = 600
)
$ErrorActionPreference = 'Stop'
$RipesPath = (Resolve-Path -LiteralPath $RipesPath).Path
$CasesCsv = (Resolve-Path -LiteralPath $CasesCsv).Path
$cases = @(Import-Csv -LiteralPath $CasesCsv)
if ($cases.Count -eq 0) { throw 'Empty cases CSV.' }
$keys = @{}
foreach ($case in $cases) {
    foreach ($field in 'rank','p','o','state','distance') {
        if (!$case.PSObject.Properties[$field]) { throw "Missing column: $field" }
    }
    $p = [int]$case.p; $o = [int]$case.o; $d = [int]$case.distance
    if ($p -lt 0 -or $p -ge 5040 -or $o -lt 0 -or $o -ge 729 -or
        $d -lt 0 -or $d -gt 11 -or [int]$case.rank -ne ($p * 729 + $o)) {
        throw "Invalid case: $($case.rank)"
    }
    if ($keys.ContainsKey($case.rank)) { throw 'Duplicate case rank.' }
    $keys[$case.rank] = $true
}
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
$OutDir = (Resolve-Path -LiteralPath $OutDir).Path
$sourcePath = Join-Path $PSScriptRoot 'solver-rv32i.s'
$buildPath = Join-Path $PSScriptRoot 'build-renderer.ps1'
$identity = [ordered]@{
    source_sha256 = (Get-FileHash -LiteralPath $sourcePath).Hash
    builder_sha256 = (Get-FileHash -LiteralPath $buildPath).Hash
    tester_sha256 = (Get-FileHash -LiteralPath $PSCommandPath).Hash
    ripes_sha256 = (Get-FileHash -LiteralPath $RipesPath).Hash
    cases_sha256 = (Get-FileHash -LiteralPath $CasesCsv).Hash
    processor = $Processor
}
$manifestPath = Join-Path $OutDir 'manifest.json'
$identityJson = $identity | ConvertTo-Json -Compress
if (Test-Path -LiteralPath $manifestPath) {
    if ((Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8).Trim() -ne $identityJson) {
        throw 'Source, build script, Ripes, processor or cases changed. Use a new OutDir.'
    }
} else {
    $identityJson | Set-Content -LiteralPath $manifestPath -Encoding UTF8
}
$csvPath = Join-Path $OutDir 'results.csv'
$done = @{}
if (Test-Path -LiteralPath $csvPath) {
    foreach ($row in (Import-Csv -LiteralPath $csvPath)) { $done[$row.rank] = $true }
}
& $buildPath -Mode cli
$template = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'solver-cli.s') -Raw -Encoding UTF8
$permPattern = [regex]::new('(?m)^\s*\.equ\s+START_PERM\s*,[^\r\n]*')
$orientPattern = [regex]::new('(?m)^\s*\.equ\s+START_ORIENT\s*,[^\r\n]*')
if ($permPattern.Matches($template).Count -ne 1 -or
    $orientPattern.Matches($template).Count -ne 1) { throw 'Invalid input constants.' }
$selected = if ($Limit -gt 0) { @($cases | Select-Object -First $Limit) } else { $cases }
$encoding = [System.Text.UTF8Encoding]::new($false)
$casePath = Join-Path $OutDir '_case.s'
foreach ($case in $selected) {
    if ($done.ContainsKey($case.rank)) { continue }
    $text = $permPattern.Replace($template, ".equ START_PERM, $($case.p)", 1)
    $text = $orientPattern.Replace($text, ".equ START_ORIENT, $($case.o)", 1)
    [System.IO.File]::WriteAllText($casePath, $text, $encoding)
    $prefix = Join-Path $OutDir "case-$($case.rank)"
    $reportPath = "$prefix-report.txt"
    if (Test-Path -LiteralPath $reportPath) { Remove-Item -LiteralPath $reportPath }
    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    $errorText = ''; $instructions = ''; $length = ''; $replay = ''
    try {
        $arguments = '--mode cli --src "' + $casePath + '" -t asm --proc ' +
            $Processor + ' --iret --regs --runinfo --output "' + $reportPath + '"'
        # Own the process handle from launch, rather than relying on Start-Process.
        $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
        $startInfo.FileName = $RipesPath
        $startInfo.Arguments = $arguments
        $startInfo.UseShellExecute = $false
        $startInfo.CreateNoWindow = $true
        $startInfo.RedirectStandardOutput = $true
        $startInfo.RedirectStandardError = $true
        $process = [System.Diagnostics.Process]::new()
        $process.StartInfo = $startInfo
        if (!$process.Start()) { throw 'Could not start Ripes.' }
        # Drain both streams asynchronously to avoid filling either pipe.
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        $exited = $process.WaitForExit($TimeoutSeconds * 1000)
        if (!$exited) { $process.Kill() }
        $process.WaitForExit()
        [System.IO.File]::WriteAllText("$prefix-stdout.txt", $stdoutTask.Result, $encoding)
        [System.IO.File]::WriteAllText("$prefix-stderr.txt", $stderrTask.Result, $encoding)
        $exitCode = $process.ExitCode
        $process.Dispose()
        if (!$exited) { throw "Timed out after $TimeoutSeconds seconds." }
        if ($exitCode -ne 0) { throw "Ripes exit code $exitCode. See stderr log." }
        $report = Get-Content -LiteralPath $reportPath -Raw -Encoding UTF8
        $mI = [regex]::Match($report, '(?m)^===== instructions retired\s*\r?\n\s*(\d+)')
        $mL = [regex]::Match($report, '(?m)^x10:\s*(-?\d+)')
        $mR = [regex]::Match($report, '(?m)^x12:\s*(-?\d+)')
        if (!$mI.Success -or !$mL.Success -or !$mR.Success) { throw 'Missing report fields.' }
        if ($report -notmatch ('processor:\s*' + [regex]::Escape($Processor) + '\b')) {
            throw 'Report processor mismatch.'
        }
        $instructions = [long]$mI.Groups[1].Value
        $length = [int]$mL.Groups[1].Value
        $replay = [int]$mR.Groups[1].Value
    } catch { $errorText = $_.Exception.Message }
    $timer.Stop()
    $lengthOk = if ($errorText -eq '') { $length -eq [int]$case.distance } else { '' }
    $replayOk = if ($errorText -eq '') { $replay -eq 0 } else { '' }
    $budgetApplies = ($Processor -eq 'RV32_ISS' -and [int]$case.distance -eq 11)
    $budgetOk = if ($budgetApplies -and $errorText -eq '') {
        ([long]$instructions -le 50000000)
    } else { '' }
    $row = [pscustomobject][ordered]@{
        rank=$case.rank; p=$case.p; o=$case.o; state=$case.state
        expected_length=$case.distance; processor=$Processor
        instructions=$instructions; length=$length; replay_status=$replay
        length_ok=$lengthOk; replay_ok=$replayOk; budget_ok=$budgetOk
        seconds=[math]::Round($timer.Elapsed.TotalSeconds,3); error=$errorText
    }
    if (Test-Path -LiteralPath $csvPath) {
        $row | Export-Csv -LiteralPath $csvPath -NoTypeInformation -Append -Encoding UTF8
    } else {
        $row | Export-Csv -LiteralPath $csvPath -NoTypeInformation -Encoding UTF8
    }
    Write-Host "rank=$($case.rank) length=$length replay=$replay instructions=$instructions budget=$budgetOk error=$errorText"
}
$rows = @(Import-Csv -LiteralPath $csvPath)
$errors = @($rows | Where-Object { $_.error -ne '' }).Count
$lengthFailures = @($rows | Where-Object { $_.length_ok -eq 'False' }).Count
$replayFailures = @($rows | Where-Object { $_.replay_ok -eq 'False' }).Count
$overBudget = @($rows | Where-Object { $_.budget_ok -eq 'False' }).Count
$validRows = @($rows | Where-Object { $_.error -eq '' })
$maximum = ($validRows | ForEach-Object { [long]$_.instructions } | Measure-Object -Maximum).Maximum
$meanSeconds = ($rows | ForEach-Object { [double]$_.seconds } | Measure-Object -Average).Average
$allDepth11 = ($cases.Count -eq 2644 -and @($cases | Where-Object { [int]$_.distance -ne 11 }).Count -eq 0)
$complete = ($allDepth11 -and $Processor -eq 'RV32_ISS' -and $rows.Count -eq 2644)
$passed = ($complete -and $errors -eq 0 -and $lengthFailures -eq 0 -and $replayFailures -eq 0 -and $overBudget -eq 0)
$summary = @(
    "Processor: $Processor", "Completed: $($rows.Count) / $($cases.Count)",
    "Execution/report errors: $errors", "Length failures: $lengthFailures",
    "Replay failures: $replayFailures", "Budget failures: $overBudget",
    "Maximum retired instructions among valid reports: $maximum",
    "Average process wall time: $([math]::Round($meanSeconds,3)) seconds",
    "Estimated remaining process time: $([math]::Round(($cases.Count-$rows.Count)*$meanSeconds/3600,2)) hours",
    "All 2644 distance-11 ISS cases completed: $complete",
    "Full distance-11 ISS gate passed: $passed"
)
$summary | Set-Content -LiteralPath (Join-Path $OutDir 'summary.txt') -Encoding UTF8
$summary | ForEach-Object { Write-Host $_ }
if ($errors -gt 0 -or $lengthFailures -gt 0 -or $replayFailures -gt 0 -or $overBudget -gt 0) {
    throw 'Some checks failed; see results.csv and per-case logs.'
}
