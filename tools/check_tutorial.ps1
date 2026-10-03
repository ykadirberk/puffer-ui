# Checks docs/tutorial against the code it quotes.
#
# Every code block in the tutorial that starts with `<!-- src: <path> -->` is an
# excerpt of that file. For each such block this script verifies that its lines
# (compared with surrounding whitespace trimmed) occur, in order, in the source
# file; a line that is only `// ...` (or `# ...`) marks an elision and is skipped.
# It also verifies that every relative link and image in the tutorial exists.
#
# Usage:  pwsh -File tools/check_tutorial.ps1        # exits 1 on any mismatch
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$dir = Join-Path $root 'docs/tutorial'
$utf8 = New-Object System.Text.UTF8Encoding $false
$failures = 0
$blocks = 0

foreach ($md in Get-ChildItem $dir -Filter '*.md') {
    $lines = [System.IO.File]::ReadAllLines($md.FullName, $utf8)
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -notmatch '^<!-- src: (\S+) -->$') { continue }
        $rel = $Matches[1]
        $src = Join-Path $root $rel
        if (-not (Test-Path $src)) {
            Write-Host ("{0}:{1}: source file '{2}' does not exist" -f $md.Name, ($i + 1), $rel)
            $failures++
            continue
        }
        $srcLines = [System.IO.File]::ReadAllLines($src, $utf8) | ForEach-Object { $_.Trim() }
        $j = $i + 2 # first line inside the fence
        $pos = 0
        $blocks++
        while ($j -lt $lines.Count -and $lines[$j] -ne '```') {
            $want = $lines[$j].Trim()
            if ($want -ne '' -and $want -ne '// ...' -and $want -ne '# ...') {
                $found = -1
                for ($k = $pos; $k -lt $srcLines.Count; $k++) {
                    if ($srcLines[$k] -ceq $want) { $found = $k; break }
                }
                if ($found -lt 0) {
                    Write-Host ("{0}:{1}: not found (in order) in {2}: {3}" -f $md.Name, ($j + 1), $rel, $want)
                    $failures++
                    break
                }
                $pos = $found + 1
            }
            $j++
        }
        $i = $j
    }

    # relative links and images must exist (code in backticks is not a link)
    $text = [System.IO.File]::ReadAllText($md.FullName, $utf8)
    $text = [regex]::Replace($text, '(?s)```.*?```', '')
    $text = [regex]::Replace($text, '`[^`\n]*`', '')
    foreach ($m in [regex]::Matches($text, '\]\(([^)\s]+)\)')) {
        $target = $m.Groups[1].Value
        if ($target -match '^(https?:|mailto:|#)') { continue }
        $path = ($target -split '#')[0]
        if ($path -eq '') { continue }
        if (-not (Test-Path (Join-Path $dir $path))) {
            Write-Host ("{0}: broken link '{1}'" -f $md.Name, $target)
            $failures++
        }
    }
}

if ($failures -gt 0) {
    Write-Host ("tutorial check FAILED: {0} problem(s) in {1} code block(s)" -f $failures, $blocks)
    exit 1
}
Write-Host ("tutorial check ok: {0} code blocks match their sources" -f $blocks)
