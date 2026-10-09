param(
    [ValidateSet('gui','cli','both')][string]$Mode = 'both',
    [ValidateRange(0,5039)][int]$Perm = 720,
    [ValidateRange(0,728)][int]$Orient = 0,
    [string]$Source = (Join-Path $PSScriptRoot 'solver-rv32i.s')
)
$ErrorActionPreference = 'Stop'
$lines = Get-Content -LiteralPath $Source -Encoding UTF8
$encoding = [System.Text.UTF8Encoding]::new($false)
$modes = if ($Mode -eq 'both') { @('gui','cli') } else { @($Mode) }
foreach ($kind in $modes) {
    $inside = $false
    $permCount = 0
    $orientCount = 0
    $result = foreach ($line in $lines) {
        if ($line.Trim() -eq '# RENDER_BEGIN') {
            if ($inside) { throw 'Nested renderer block.' }
            $inside = $true
            continue
        }
        if ($line.Trim() -eq '# RENDER_END') {
            if (!$inside) { throw 'Unmatched renderer end.' }
            $inside = $false
            continue
        }
        if ($inside -and $kind -eq 'cli') { continue }
        if ($line -match '^\s*\.equ\s+START_PERM\s*,') {
            $permCount++
            ".equ START_PERM, $Perm"
        } elseif ($line -match '^\s*\.equ\s+START_ORIENT\s*,') {
            $orientCount++
            ".equ START_ORIENT, $Orient"
        } else { $line }
    }
    if ($inside) { throw 'Unclosed renderer block.' }
    if ($permCount -ne 1 -or $orientCount -ne 1) {
        throw 'Expected exactly one START_PERM and START_ORIENT definition.'
    }
    if ($kind -eq 'cli' -and ($result -join "`n") -match 'LED_MATRIX_0_') {
        throw 'CLI still contains LED peripheral symbols.'
    }
    $path = Join-Path $PSScriptRoot "solver-$kind.s"
    [System.IO.File]::WriteAllLines($path, [string[]]$result, $encoding)
    Write-Host "Generated $path (p=$Perm, o=$Orient)"
}
