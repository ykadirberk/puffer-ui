# Same benchmark source compiled with baseline/candidate implementations.
# Run sequentially after builds and tests have finished; retain every sample.
param(
    [Parameter(Mandatory)][string]$Before,
    [Parameter(Mandatory)][string]$After,
    [string]$Output = 'build-core-perf-fixes/core',
    [int]$Runs = 5
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
$samples = @()
$cases = @{}
foreach ($run in 1..$Runs) {
    $order = if ($run % 2) { @('before','after') } else { @('after','before') }
    foreach ($variant in $order) {
        $exe = if ($variant -eq 'before') { $Before } else { $After }
        $prefix = Join-Path $Output "$variant-r$run"
        & $exe > "$prefix.csv" 2> "$prefix.stderr.txt"
        if ($LASTEXITCODE -ne 0) { throw "Core benchmark failed: $exe" }
        $rows = @(Import-Csv -LiteralPath "$prefix.csv")
        if ($rows.Count -ne 8) { throw 'Expected eight core benchmark cases' }
        $cases[$variant] = @($rows | ForEach-Object { "$($_.case):$($_.iterations)" })
        foreach ($row in $rows) {
            $samples += [pscustomobject]@{
                Run=$run; Variant=$variant; Case=$row.case
                Ms=[double]$row.ms_per_iteration
                Allocations=[double]$row.allocations_per_iteration
            }
        }
        Write-Host "$variant core run $run complete"
    }
    if (Compare-Object $cases.before $cases.after) { throw 'Core workloads differ' }
}
$samples | Export-Csv -LiteralPath (Join-Path $Output 'samples.csv') -NoTypeInformation
$summary = foreach ($case in @($samples.Case | Select-Object -Unique)) {
    $a = @($samples | Where-Object { $_.Case -eq $case -and $_.Variant -eq 'before' })
    $b = @($samples | Where-Object { $_.Case -eq $case -and $_.Variant -eq 'after' })
    $beforeMedian = Median @($a.Ms)
    $afterMedian = Median @($b.Ms)
    [pscustomobject]@{
        Case=$case; BeforeMedianMs=$beforeMedian; AfterMedianMs=$afterMedian
        DeltaPercent=(100 * ($afterMedian / $beforeMedian - 1))
        BeforeMinMs=($a.Ms | Measure-Object -Minimum).Minimum
        BeforeMaxMs=($a.Ms | Measure-Object -Maximum).Maximum
        AfterMinMs=($b.Ms | Measure-Object -Minimum).Minimum
        AfterMaxMs=($b.Ms | Measure-Object -Maximum).Maximum
        BeforeMedianAllocations=(Median @($a.Allocations))
        AfterMedianAllocations=(Median @($b.Allocations))
    }
}
$summary | Export-Csv -LiteralPath (Join-Path $Output 'summary.csv') -NoTypeInformation
$summary | Format-Table
@{ Before=$Before; BeforeSHA256=(Get-FileHash -LiteralPath $Before).Hash
   After=$After; AfterSHA256=(Get-FileHash -LiteralPath $After).Hash; Runs=$Runs } |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Output 'protocol.json')
