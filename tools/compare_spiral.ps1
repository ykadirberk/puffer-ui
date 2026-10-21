# Both executables compile tests/test_showcase.cpp and the actual showcase source.
# Preserve the baseline binary before editing; run sequentially, without builds.
param(
    [Parameter(Mandatory)][string]$Before,
    [Parameter(Mandatory)][string]$After,
    [string]$Output = 'build-perf-fixes/spiral',
    [ValidateSet('null','software','direct3d11','direct3d12','opengl','metal')]
    [string]$Renderer = 'direct3d11',
    [int]$Runs = 5,
    [ValidateSet(1150,5000)]
    [int]$Tiles = 1150
)
$ErrorActionPreference = 'Stop'
$Before = (Resolve-Path -LiteralPath $Before).Path
$After = (Resolve-Path -LiteralPath $After).Path
New-Item -ItemType Directory -Path $Output -Force | Out-Null
$Output = (Resolve-Path -LiteralPath $Output).Path
function Median($Values) {
    $sorted = @($Values | Sort-Object)
    if ($sorted.Count % 2) { return $sorted[[int][math]::Floor($sorted.Count / 2)] }
    return ($sorted[$sorted.Count / 2 - 1] + $sorted[$sorted.Count / 2]) / 2
}
$summary = @()
$counts = @{}
foreach ($run in 1..$Runs) {
    $order = if ($run % 2) { @('before','after') } else { @('after','before') }
    foreach ($variant in $order) {
        $exe = if ($variant -eq 'before') { $Before } else { $After }
        $prefix = Join-Path $Output "$variant-r$run"
        $opts = @('--bench', $Renderer, '--tiles', "$Tiles")
        & $exe @opts > "$prefix.csv" 2> "$prefix.stderr.txt"
        if ($LASTEXITCODE -ne 0) { throw "Spiral failed: $exe ($LASTEXITCODE)" }
        $rows = @(Import-Csv -LiteralPath "$prefix.csv")
        if ($rows.Count -ne 180 -or [int]$rows[-1].count -ne ($Tiles + 3)) { throw 'Add scenario did not complete' }
        $counts[$variant] = @($rows | ForEach-Object { "$($_.frame):$($_.count):$($_.draws):$($_.vertices):$($_.indices)" })
        $steady = @($rows | Where-Object { [int]$_.frame -ge 60 -and [int]$_.frame -lt 90 })
        $adding = @($rows | Where-Object { [int]$_.frame -ge 90 })
        $warm = @($rows | Where-Object { [int]$_.frame -ge 60 })
        $times = @($warm | ForEach-Object { [double]$_.total_ms } | Sort-Object)
        $summary += [pscustomobject]@{
            Run=$run; Variant=$variant; Renderer=$Renderer; Tiles=$Tiles
            SteadyMedianMs=(Median @($steady | ForEach-Object { [double]$_.total_ms }))
            AddingMeanMs=($adding | ForEach-Object { [double]$_.total_ms } | Measure-Object -Average).Average
            AddingWorstMs=($adding | ForEach-Object { [double]$_.total_ms } | Measure-Object -Maximum).Maximum
            WarmP95Ms=$times[[int][math]::Ceiling($times.Count * 0.95) - 1]
            WarmP99Ms=$times[[int][math]::Ceiling($times.Count * 0.99) - 1]
        }
        Write-Host "$variant run $run complete"
    }
    if (Compare-Object $counts.before $counts.after) { throw "Geometry or interaction count changed in run $run" }
}
$summary | Export-Csv -LiteralPath (Join-Path $Output 'summary.csv') -NoTypeInformation
$summary | Format-Table
if ($Renderer -ne 'null') {
    foreach ($variant in @('before','after')) {
        $exe = if ($variant -eq 'before') { $Before } else { $After }
        $prefix = Join-Path $Output "$variant-pixels"
        & $exe --bench $Renderer --tiles $Tiles --capture $prefix > "$prefix.csv" 2> "$prefix.stderr.txt"
        if ($LASTEXITCODE -ne 0) { throw "Spiral capture failed: $exe" }
    }
    $pixels = foreach ($frame in @(89,91,98,104,120,124,125,126,127,128,129,135,156,179)) {
        $a = (Get-FileHash -LiteralPath (Join-Path $Output "before-pixels-$frame.bmp")).Hash
        $b = (Get-FileHash -LiteralPath (Join-Path $Output "after-pixels-$frame.bmp")).Hash
        if ($a -ne $b) { throw "Spiral pixels changed at frame $frame" }
        [pscustomobject]@{Frame=$frame;Identical=$true;SHA256=$a}
    }
    $pixels | Export-Csv -LiteralPath (Join-Path $Output 'pixels.csv') -NoTypeInformation
    Write-Host 'All 14 Spiral BMP pairs are byte-identical (including the second-Add peak)'
}
