# Immutable composed Slug frames

`frames.zip` holds all 180 complete 512x512 RGBA frames of the unchanged
96-instance composed SVG benchmark (gradient, clipped isolated opacity,
per-frame rotation and changing currentColor).

Captured with implementation revision
`48db13f08a2b897f37ffe381b31391c6cfd4721b`, before batching or consolidated
uploads. DIRECT and STAGED captures are byte-identical; `receipt.json` inside
the archive retains every hash and the frozen capture executable identity.
Capture and subsequent baseline identity checks cover all frames independently.
The capture command refuses existing directories. Never regenerate this fixture
to accept a candidate difference.

Qualification adapter: Windows x64, AMD Radeon RX 9070 XT (4098:30032), driver
8389024. Floating-point native coverage is not a cross-driver raster guarantee;
the ordinary native batch suite also compares ordered immediate/batched pixels
on the current adapter. This immutable fixture is an additional explicit gate.

`tools/compare_vulkan_batches.py --vector-references` accepts this archive,
validates all hashes, safely extracts it to the new result directory and compares
every candidate frame, including all timed peaks. For a standalone check, extract
to a fresh directory and run `pui_vulkan_batch_tests --reference DIRECTORY`
(also `--staged`, `--cache`, and both).
