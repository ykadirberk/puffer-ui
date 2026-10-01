# Generates docs/api.md from the public declaration section of the header.
# Usage:  pwsh -File tools/gen_api.ps1            # writes docs/api.md
#         pwsh -File tools/gen_api.ps1 -Check     # exits 1 when the file drifts
param([switch]$Check)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$header = Join-Path $root 'include/pufferui/pufferui.h'
$outFile = Join-Path $root 'docs/api.md'

$lines = [System.IO.File]::ReadAllLines($header, [System.Text.Encoding]::UTF8)

# The declaration section: namespace pui { ... } before the implementation block.
$impl = -1
for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($lines[$i].Contains('#if defined(PUFFERUI_IMPLEMENTATION)')) { $impl = $i; break }
}
if ($impl -lt 0) { throw 'implementation block not found' }

$out = New-Object System.Collections.Generic.List[string]
$out.Add('# PufferUI API reference (generated)')
$out.Add('')
$out.Add('Generated from the declaration section of `include/pufferui/pufferui.h`')
$out.Add('by `tools/gen_api.ps1` (CI checks it does not drift). Every declaration here')
$out.Add('is public; the implementation lives behind `PUFFERUI_IMPLEMENTATION`.')
$out.Add('')

# Collect ui member declarations inside `struct ui { ... };`
$uiStart = -1
for ($i = 0; $i -lt $impl; $i++) {
    if ($lines[$i] -match '^struct ui$') { $uiStart = $i; break }
}
if ($uiStart -ge 0) {
    $out.Add('## `ui` methods')
    $out.Add('')
    $depth = 0; $opened = $false
    for ($i = $uiStart; $i -lt $impl; $i++) {
        $l = $lines[$i]
        if ($l -match '^\s*//') { continue }
        $trim = $l.Trim()
        if ($trim -match '^(void|bool|f32|i32|uiid|rect|vec2|color|font_handle|panel_scope|popup_scope|scroll_view|text_scope|style_scope|id_scope|const char \*|interaction)\s+[A-Za-z_]') {
            if ($trim.Contains('(') -and ($trim.EndsWith(';') -or $trim.EndsWith('{'))) {
                $sig = ($trim -replace '\s*\{$', ';')
                $out.Add(('- `' + $sig + '`'))
            }
        }
        foreach ($ch in $l.ToCharArray()) {
            if ($ch -eq '{') { $depth++; $opened = $true }
            elseif ($ch -eq '}') { $depth-- }
        }
        if ($opened -and $depth -eq 0 -and $i -gt $uiStart) { break }
    }
    $out.Add('')
}

# Free functions / types: declarations at file scope (4-space indent would be members).
$out.Add('## Free functions and parameters')
$out.Add('')
for ($i = 0; $i -lt $impl; $i++) {
    $trim = $lines[$i].Trim()
    if ($trim -match '^(context \*|void |bool |f32 |uiid |rect |const char \*)[A-Za-z_].*\(' -and
        $trim.EndsWith(';') -and -not $lines[$i].StartsWith('    ')) {
        $out.Add(('- `' + $trim + '`'))
    }
}

$text = ($out -join "`n") + "`n"
if ($Check) {
    if (-not (Test-Path $outFile)) { Write-Host 'docs/api.md missing'; exit 1 }
    $existing = [System.IO.File]::ReadAllText($outFile) -replace "`r`n", "`n"
    if ($existing -ne $text) {
        Write-Host 'docs/api.md is stale: regenerate with tools/gen_api.ps1'
        exit 1
    }
    Write-Host 'docs/api.md is up to date'
    exit 0
}
[System.IO.File]::WriteAllText($outFile, $text, (New-Object System.Text.UTF8Encoding $false))
Write-Host ("wrote docs/api.md ({0} lines)" -f $out.Count)
