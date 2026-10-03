# Bundled fonts

`DejaVuSans.ttf`, `DejaVuSans-Bold.ttf`, `DejaVuSans-Oblique.ttf` — DejaVu Sans
2.37 (2016-05-13), from
<https://github.com/dejavu-fonts/dejavu-fonts/releases/tag/version_2_37>.

Used as the bundled coverage fonts so text rendering (and golden-image tests) are
deterministic and work without system fonts. The demo and the golden text scene
use bold/oblique through `ui.text_style(size, font)` (a `text_scope`).
`LICENSE.txt` is the font's license (Bitstream Vera + Arev, free to use and
redistribute).

The design's required coverage set (see the migration plan §6.2) is asserted by
`test_font_coverage` in `tests/test_text.cpp`: Basic Latin, Latin-1 Supplement,
Latin Extended-A, Greek and Coptic, General Punctuation, Super/Subscripts,
Currency Symbols, Letterlike Symbols, Arrows, Mathematical Operators, and the
check-mark dingbats. Unassigned slots and a few rare symbols not present in
DejaVu 2.37 are listed as documented exceptions in that test.
