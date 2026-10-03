# Regenerates the pictures in docs/tutorial/img/ by running each tutorial program
# offscreen (--selftest, the software renderer) and saving its last frame.
#
# Usage:  pwsh -File tools/tutorial_shots.ps1 [-BinDir out/build/x64-release/Release]
#
# The scripted programs (the finished todo app) are captured after enough frames
# for the typed tasks, the ticked checkbox and the hovered row to show; the scene
# drifts with the clock, so two runs differ slightly in background position.
param([string]$BinDir = 'out/build/x64-release/Release')

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$bin = Join-Path $root $BinDir
$img = Join-Path $root 'docs/tutorial/img'
New-Item -ItemType Directory -Force $img | Out-Null
Add-Type -AssemblyName System.Drawing

# name, program, frames, size [, crop x,y,w,h]
$shots = @(
    @('step01', 'pui_tut_step01_shot', 4, ''),
    @('step02', 'pui_tut_step02_layout', 4, ''),
    @('step03', 'pui_tut_step03_tasks', 4, ''),
    @('step04', 'pui_tut_step04_filters', 4, ''),
    @('step05', 'pui_tut_step05_glass', 4, ''),
    @('step06', 'pui_tut_step06_motion', 30, ''),
    @('final', 'pui_tut_todo', 28, ''),
    @('glass-wide', 'pui_tut_todo', 28, '900x560'),
    @('glass-group', 'pui_tut_todo', 40, '', '10,180,500,112'),   # a crop: x,y,w,h
    @('button-states', 'pui_tut_button_states', 3, '')
)
foreach ($s in $shots) {
    $exe = Join-Path $bin ($s[1] + '.exe')
    if (-not (Test-Path $exe)) { throw "missing $exe (build the pui_tut_* targets first)" }
    $bmp = Join-Path ([System.IO.Path]::GetTempPath()) ('tut_' + $s[0] + '.bmp')
    $args = @('--selftest', '--frames', $s[2], '--screenshot', $bmp)
    if ($s[3] -ne '') { $args += @('--size', $s[3]) }
    & $exe @args | Out-Null
    if (-not (Test-Path $bmp)) { throw "no screenshot from $($s[1])" }
    $png = Join-Path $img ($s[0] + '.png')
    $bitmap = [System.Drawing.Bitmap]::FromFile($bmp)
    try {
        $out = $bitmap
        if ($s.Count -gt 4) {
            $c = $s[4].Split(',') | ForEach-Object { [int]$_ }
            $out = $bitmap.Clone((New-Object System.Drawing.Rectangle($c[0], $c[1], $c[2], $c[3])), $bitmap.PixelFormat)
        }
        $out.Save($png, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally { $bitmap.Dispose() }
    Remove-Item $bmp
    Write-Host ("{0,-11} {1} ({2} bytes)" -f $s[0], $png, (Get-Item $png).Length)
}
