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

# A declaration may span several lines: join continuation lines until the
# statement ends (';' for a declaration, '{' for an inline body). Returns the
# joined text and the index of its last line, or $null when the line does not
# start a function declaration.
function Read-Declaration([string[]]$src, [int]$start, [int]$limit, [string]$indent) {
    $raw = $src[$start]
    if (-not $raw.StartsWith($indent) -or $raw.Substring($indent.Length).StartsWith(' ')) { return $null }
    $t = $raw.Trim()
    if ($t -match '^(//|#|return|if |for |while |switch |case |else|struct |enum |using |typedef |template|namespace|static_assert|explicit |friend )') { return $null }
    if ($t -notmatch '^[A-Za-z_][A-Za-z0-9_:<>,\*& ]*[ \*&][A-Za-z_][A-Za-z0-9_]*\(') { return $null }
    $text = $t
    $j = $start
    while (-not ($text.EndsWith(';') -or $text.EndsWith('{') -or $text.EndsWith('}')) -and $j + 1 -lt $limit) {
        $j++
        $text = $text + ' ' + $src[$j].Trim()
    }
    if (-not ($text.EndsWith(';') -or $text.EndsWith('{'))) { return $null }
    $text = ($text -replace '\s*\{$', ';') -replace '\s+', ' '
    $text = $text -replace '\( ', '(' -replace ' \)', ')'
    return @{ Text = $text; End = $j }
}

$out = New-Object System.Collections.Generic.List[string]
$out.Add('# PufferUI API reference (generated)')
$out.Add('')
$out.Add('Generated from the declaration section of `include/pufferui/pufferui.h`')
$out.Add('by `tools/gen_api.ps1` (CI checks it does not drift). Every declaration here')
$out.Add('is public; the implementation lives behind `PUFFERUI_IMPLEMENTATION`.')
$out.Add('')

# ---- ui members: inside `struct ui { ... };`, one indent level in -------------
$uiStart = -1
for ($i = 0; $i -lt $impl; $i++) {
    if ($lines[$i] -match '^struct ui$') { $uiStart = $i; break }
}
$uiCount = 0
if ($uiStart -ge 0) {
    $out.Add('## `ui` methods')
    $out.Add('')
    $depth = 0; $opened = $false
    for ($i = $uiStart; $i -lt $impl; $i++) {
        $l = $lines[$i]
        $decl = $null
        if ($depth -eq 1) { $decl = Read-Declaration $lines $i $impl '    ' }
        if ($decl) {
            $out.Add(('- `' + $decl.Text + '`'))
            $uiCount++
            # skip the continuation lines, but still count their braces
            for ($k = $i; $k -le $decl.End; $k++) {
                foreach ($ch in $lines[$k].ToCharArray()) {
                    if ($ch -eq '{') { $depth++; $opened = $true } elseif ($ch -eq '}') { $depth-- }
                }
            }
            $i = $decl.End
            continue
        }
        foreach ($ch in $l.ToCharArray()) {
            if ($ch -eq '{') { $depth++; $opened = $true } elseif ($ch -eq '}') { $depth-- }
        }
        if ($opened -and $depth -eq 0 -and $i -gt $uiStart) { break }
    }
    $out.Add('')
}

# ---- file-scope functions: column 0, split into core and `pui::comp` -----------
$core = New-Object System.Collections.Generic.List[string]
$comp = New-Object System.Collections.Generic.List[string]
$inComp = $false
for ($i = 0; $i -lt $impl; $i++) {
    $l = $lines[$i]
    if ($l -match '^namespace comp$') { $inComp = $true; continue }
    if ($l -match '^\} // namespace comp') { $inComp = $false; continue }
    $decl = Read-Declaration $lines $i $impl ''
    if (-not $decl) { continue }
    if ($decl.Text.Contains(' operator') -or $decl.Text -match '^inline ') { $i = $decl.End; continue }
    if ($inComp) { $comp.Add(('- `' + $decl.Text + '`')) } else { $core.Add(('- `' + $decl.Text + '`')) }
    $i = $decl.End
}
$out.Add('## Free functions and parameters')
$out.Add('')
foreach ($e in $core) { $out.Add($e) }
$out.Add('')
$out.Add('## Components (`pui::comp`)')
$out.Add('')
foreach ($e in $comp) { $out.Add($e) }
$out.Add('')

# Coverage guard: the generator must keep seeing the multi-line declarations it
# once missed. If a refactor of the header or this script drops any of them, fail
# loudly instead of publishing a reference that silently shrank.
$text = ($out -join "`n") + "`n"
foreach ($probe in @('slider_float', 'combo(', 'dock_space', 'titlebar', 'switch_toggle',
                     'radio_group', 'tab_bar', 'comp::table', 'command_palette', 'measure_text')) {
    if (-not $text.Contains($probe) -and -not ($probe -eq 'comp::table' -and $text -match 'table\(ui')) {
        throw "gen_api coverage check failed: '$probe' is missing from the generated reference"
    }
}

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
Write-Host ("wrote docs/api.md ({0} lines, {1} ui methods, {2} free, {3} components)" -f $out.Count, $uiCount, $core.Count, $comp.Count)
