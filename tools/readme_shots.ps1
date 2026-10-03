# Regenerates the pictures in docs/img/readme/ that the README shows next to
# each example: every program runs offscreen (the software renderer) and its
# last frame is saved as a PNG.
#
# Usage:  powershell -File tools/readme_shots.ps1 [-BinDir out/build/x64-release/Release]
#
# Scripted programs are captured at frame 46: the selftest script has just
# released a click and typed, so widgets show hover/focus/typed state. The
# showcase runs with --no-script --no-hud (a clean, unscrolled page).
# Software-renderer note: very large blurred panels do not frost there (SDL's
# software triangle limit), so pictures can show slightly less glass than a
# GPU run.
param([string]$BinDir = 'out/build/x64-release/Release')

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$bin = Join-Path $root $BinDir
$img = Join-Path $root 'docs/img/readme'
New-Item -ItemType Directory -Force $img | Out-Null
Add-Type -AssemblyName System.Drawing

function Save-Png([string]$bmp, [string]$png) {
    $bitmap = [System.Drawing.Bitmap]::FromFile($bmp)
    try { $bitmap.Save($png, [System.Drawing.Imaging.ImageFormat]::Png) } finally { $bitmap.Dispose() }
}

# name, program, arguments (each example's own selftest flags)
$shots = @(
    @('rect', 'pui_ex_rect', '--selftest --frames 46'),
    @('cursors', 'pui_ex_cursors', '--selftest --frames 46'),
    @('tracks', 'pui_ex_tracks', '--selftest --frames 46'),
    @('grid', 'pui_ex_grid', '--selftest --frames 46'),
    @('ids', 'pui_ex_ids', '--selftest --frames 46'),
    @('text', 'pui_ex_text', '--selftest --frames 46'),
    @('buttons', 'pui_ex_buttons', '--selftest --frames 46'),
    @('inputs', 'pui_ex_inputs', '--selftest --frames 46'),
    @('widgets', 'pui_ex_widgets', '--selftest --frames 46'),
    @('panels', 'pui_ex_panels', '--selftest --frames 46'),
    @('scroll', 'pui_ex_scroll', '--selftest --frames 46'),
    @('popups', 'pui_ex_popups', '--selftest --frames 46'),
    @('dock', 'pui_ex_dock', '--selftest --frames 46'),
    @('windows', 'pui_ex_windows', '--selftest --frames 46'),
    @('drawing', 'pui_ex_drawing', '--selftest --frames 46'),
    @('blur', 'pui_ex_blur', '--selftest --frames 46'),
    @('animation', 'pui_ex_animation', '--selftest --frames 46'),
    @('relayout', 'pui_ex_relayout', '--selftest --frames 14 --resize 560x620'),  # mid-reflow
    @('theme', 'pui_ex_theme', '--selftest --frames 46'),
    @('custom', 'pui_ex_custom', '--selftest --frames 46'),
    @('patterns', 'pui_ex_patterns', '--selftest --frames 46'),
    @('expander', 'pui_ex_expander', '--selftest --frames 46'),
    @('combo', 'pui_ex_combo', '--selftest --frames 46 --no-script'),  # unscrolled
    @('keyboard', 'pui_ex_keyboard', '--selftest --frames 46'),
    @('vlist', 'pui_ex_vlist', '--selftest --frames 46'),
    @('components', 'pui_ex_components', '--selftest --frames 46 --no-script'),  # unscrolled
    @('counter', 'pui_counter', ''),  # its own --screenshot mode (frame 4)
    @('showcase', 'pui_showcase', '--selftest --frames 100 --page 0 --no-script --no-hud'),
    @('showcase-light', 'pui_showcase', '--selftest --frames 100 --page 3 --no-script --no-hud --light')
)
foreach ($s in $shots) {
    $exe = Join-Path $bin ($s[1] + '.exe')
    if (-not (Test-Path $exe)) { throw "missing $exe (build the examples first)" }
    $bmp = Join-Path ([System.IO.Path]::GetTempPath()) ('readme_' + $s[0] + '.bmp')
    $second = $bmp + '.second.bmp' # multi-window examples also save their second window
    foreach ($f in @($bmp, $second)) { if (Test-Path $f) { Remove-Item $f } }
    $args = @()
    if ($s[2] -ne '') { $args += $s[2].Split(' ') }
    $args += @('--screenshot', $bmp)
    & $exe @args | Out-Null
    if (-not (Test-Path $bmp)) { throw "no screenshot from $($s[1])" }
    $png = Join-Path $img ($s[0] + '.png')
    if (Test-Path $second) {
        # both windows side by side, on a neutral backdrop
        $a = [System.Drawing.Bitmap]::FromFile($bmp); $b = [System.Drawing.Bitmap]::FromFile($second)
        $gap = 24
        $out = New-Object System.Drawing.Bitmap(($a.Width + $gap + $b.Width), [Math]::Max($a.Height, $b.Height))
        $g = [System.Drawing.Graphics]::FromImage($out)
        $g.Clear([System.Drawing.Color]::FromArgb(32, 34, 40))
        $g.DrawImage($a, 0, 0, $a.Width, $a.Height)
        $g.DrawImage($b, $a.Width + $gap, 0, $b.Width, $b.Height)
        $g.Dispose(); $a.Dispose(); $b.Dispose()
        $out.Save($png, [System.Drawing.Imaging.ImageFormat]::Png); $out.Dispose()
    }
    else { Save-Png $bmp $png }
    Write-Output "wrote docs/img/readme/$($s[0]).png"
}

# The CPU renderer example writes its own frame as a PPM (P6) in the working
# directory: that file IS the picture (a custom render_device's output).
$tmp = Join-Path ([System.IO.Path]::GetTempPath()) 'readme_renderer'
New-Item -ItemType Directory -Force $tmp | Out-Null
Push-Location $tmp
try { & (Join-Path $bin 'pui_ex_renderer.exe') | Out-Null } finally { Pop-Location }
$ppm = [System.IO.File]::ReadAllBytes((Join-Path $tmp 'pui_renderer_example.ppm'))
# header: "P6\n<w> <h>\n255\n"
$pos = 0; $fields = @()
while ($fields.Count -lt 4) {
    $start = $pos
    while ($ppm[$pos] -ne 10 -and $ppm[$pos] -ne 32) { $pos++ }
    $fields += [System.Text.Encoding]::ASCII.GetString($ppm, $start, $pos - $start)
    $pos++
}
$w = [int]$fields[1]; $h = [int]$fields[2]
$bitmap = New-Object System.Drawing.Bitmap($w, $h, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
$data = $bitmap.LockBits((New-Object System.Drawing.Rectangle(0, 0, $w, $h)),
    [System.Drawing.Imaging.ImageLockMode]::WriteOnly, $bitmap.PixelFormat)
$row = New-Object byte[] ($data.Stride)
for ($y = 0; $y -lt $h; $y++) {
    for ($x = 0; $x -lt $w; $x++) {
        $src = $pos + ($y * $w + $x) * 3
        $row[$x * 3] = $ppm[$src + 2]; $row[$x * 3 + 1] = $ppm[$src + 1]; $row[$x * 3 + 2] = $ppm[$src] # RGB -> BGR
    }
    [System.Runtime.InteropServices.Marshal]::Copy($row, 0, [IntPtr]($data.Scan0.ToInt64() + $y * $data.Stride), $data.Stride)
}
$bitmap.UnlockBits($data)
$bitmap.Save((Join-Path $img 'renderer.png'), [System.Drawing.Imaging.ImageFormat]::Png)
$bitmap.Dispose()
Write-Output 'wrote docs/img/readme/renderer.png'
