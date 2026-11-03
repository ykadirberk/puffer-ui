# Builds the single-header distribution: pufferui.h with every
# `#include "impl/<name>.inl"` replaced by that slice's text.
#
# Usage:  pwsh -File tools/amalgamate.ps1                 # writes dist/pufferui_single.h
#         pwsh -File tools/amalgamate.ps1 -Out some.h     # custom output path
#
# The slices are plain text cuts of the implementation block (one namespace),
# so the result is the same header the library had before the split: define
# PUFFERUI_IMPLEMENTATION in exactly one TU, add vendored/stb to the include
# path, and nothing else from include/pufferui/impl is needed.
param([string]$Out = '')

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$headerPath = Join-Path $root 'include/pufferui/pufferui.h'
if ($Out -eq '') { $Out = Join-Path $root 'dist/pufferui_single.h' }

$utf8 = New-Object System.Text.UTF8Encoding $false
$header = [System.IO.File]::ReadAllText($headerPath, $utf8) -replace "`r`n", "`n"
$lines = $header -split "`n"

# Slices can split a private namespace/class across smaller local .inl files.
# Expand them in place so the distribution never requires the source tree.
$implRoot = [System.IO.Path]::GetFullPath((Join-Path $root 'include/pufferui/impl'))
function Add-Slice([string]$path, [System.Collections.Generic.List[string]]$destination, [string[]]$parents) {
    $path = [System.IO.Path]::GetFullPath($path)
    $prefix = $implRoot + [System.IO.Path]::DirectorySeparatorChar
    if (-not $path.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "slice outside implementation directory: $path"
    }
    if ($parents -contains $path) { throw "cyclic implementation include: $path" }
    $text = [System.IO.File]::ReadAllText($path, $utf8) -replace "`r`n", "`n"
    if ($text.EndsWith("`n")) { $text = $text.Substring(0, $text.Length - 1) }
    $sliceLines = $text -split "`n"
    # Every slice begins with the four-line provenance banner.
    for ($i = 4; $i -lt $sliceLines.Count; $i++) {
        $entry = $sliceLines[$i]
        if ($entry -match '^\s*#include "([^"\r\n]+\.inl)"\s*$') {
            $child = Join-Path (Split-Path $path -Parent) $Matches[1]
            Add-Slice $child $destination ($parents + @($path))
        } else { $destination.Add($entry) }
    }
    $destination.Add('')
}

$result = New-Object System.Collections.Generic.List[string]
foreach ($line in $lines) {
    if ($line -match '^#include "impl/([A-Za-z0-9_]+)\.inl"$') {
        $slice = Join-Path $root ("include/pufferui/impl/" + $Matches[1] + ".inl")
        Add-Slice $slice $result @()
    }
    elseif ($line -eq '// The implementation, in order (one namespace; slices are plain text cuts).') {
        continue
    }
    else {
        $result.Add($line)
    }
}

$dir = Split-Path $Out -Parent
if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null }
[System.IO.File]::WriteAllText($Out, ($result -join "`n"), $utf8)
Write-Host ("wrote {0} ({1} lines)" -f $Out, $result.Count)
