# Compare binaries built with the same compiler, flags, SDL and application TU.
# --no-hud removes timing-dependent pixels. All other drawing is unmodified.
param(
    [Parameter(Mandatory)][string]$Before,
    [Parameter(Mandatory)][string]$After,
    [string]$Output = 'build-perf-review/showcase',
    [int]$Runs = 3,
    [int]$Frames = 180,
    [int[]]$TimingPages = @(-1, 0, 1, 2, 3, 4),
    [switch]$SkipPictures,
    [switch]$SkipTiming
)
$ErrorActionPreference = 'Stop'
$Before = (Resolve-Path -LiteralPath $Before).Path
$After = (Resolve-Path -LiteralPath $After).Path
New-Item -ItemType Directory -Path $Output -Force | Out-Null
$Output = (Resolve-Path -LiteralPath $Output).Path

function Invoke-Showcase([string]$Exe, [string[]]$Options) {
    $result = @(& $Exe @Options)
    if ($LASTEXITCODE -ne 0) { throw "Showcase failed: $Exe $Options" }
    return $result
}

if (!$SkipPictures) {
    $cases = @()
    foreach ($light in @($false, $true)) {
        foreach ($page in 0..4) {
            foreach ($frame in @(1, 16)) {
                $mode = if ($light) { 'light' } else { 'dark' }
                $opts = @('--page', "$page", '--frames', "$frame", '--no-script')
                if ($light) { $opts += '--light' }
                $cases += @{ Name = "$mode-page$page-f$frame"; Options = $opts }
            }
        }
    }
    foreach ($frame in @(6, 8, 10, 12, 13, 14, 16, 18, 20, 24, 36, 48)) {
        $cases += @{ Name = "script-f$frame"; Options = @('--frames', "$frame") }
    }
    $cases += @{ Name = 'resize'; Options = @('--frames', '24', '--resize', '640x420') }
    $rows = foreach ($case in $cases) {
        $files = @()
        foreach ($variant in @('before', 'after')) {
            $exe = if ($variant -eq 'before') { $Before } else { $After }
            $path = Join-Path $Output ($case.Name + "-$variant.bmp")
            $opts = @('--selftest', '--no-hud') + $case.Options + @('--screenshot', $path)
            Invoke-Showcase $exe $opts | Out-Null
            $files += $path
        }
        $oldHash = (Get-FileHash -LiteralPath $files[0] -Algorithm SHA256).Hash
        $newHash = (Get-FileHash -LiteralPath $files[1] -Algorithm SHA256).Hash
        if ($oldHash -ne $newHash) { throw "Pixels changed in $($case.Name): $files" }
        [pscustomobject]@{ Case = $case.Name; Identical = $true; SHA256 = $oldHash }
    }
    $rows | Export-Csv -LiteralPath (Join-Path $Output 'pixels.csv') -NoTypeInformation
    Write-Host "$($rows.Count) showcase BMP pairs are byte-identical"
}

if (!$SkipTiming) {
    $rows = foreach ($run in 1..$Runs) {
        foreach ($page in $TimingPages) {
            # Alternate execution order to reduce thermal/order bias.
            $order = if ($run % 2) { @('before', 'after') } else { @('after', 'before') }
            foreach ($variant in $order) {
                $exe = if ($variant -eq 'before') { $Before } else { $After }
                $opts = @('--selftest', '--no-hud', '--perf', '--frames', "$Frames")
                if ($page -ge 0) { $opts += @('--page', "$page", '--no-script') }
                $result = Invoke-Showcase $exe $opts
                $line = $result | Where-Object { $_ -match '^perf ' } | Select-Object -Last 1
                if ($line -notmatch 'build_avg=([0-9.]+)ms build_worst=([0-9.]+)ms frame_avg=([0-9.]+)ms') {
                    throw "Missing timing: $result"
                }
                Write-Host "$variant run=$run page=$page $line"
                [pscustomobject]@{
                    Run = $run; Page = $page; Variant = $variant
                    BuildMs = [double]::Parse($Matches[1], [cultureinfo]::InvariantCulture)
                    WorstMs = [double]::Parse($Matches[2], [cultureinfo]::InvariantCulture)
                    FrameMs = [double]::Parse($Matches[3], [cultureinfo]::InvariantCulture)
                }
            }
        }
    }
    $rows | Export-Csv -LiteralPath (Join-Path $Output 'timing.csv') -NoTypeInformation
    $rows | Format-Table
}
