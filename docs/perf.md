# PufferUI performance benchmark & log

The benchmark is `pui_bench` (headless, `null_device`): it measures the
**core's** per-frame cost — layout, `interact`, draw-list recording and
flushing — not the renderer. The scene: 2,000 labeled buttons in an auto-fit
grid, 500 text rows, 50 rounded panels, plus a focused text field / checkbox
/ slider so the per-frame state paths stay hot. Each button is a rect + text
+ border loop, which is exactly the draw-batching workload this file tracks.

Output is one line: `frames=N avg=…ms worst=…ms draw_calls=N vertices=N`.
The timing is machine-relative (recorded here: Windows, MSVC, one laptop);
the **draw_calls / vertices** counts are the platform-stable numbers that
commits are judged by. Run:

```sh
out/build/x64-release/Release/pui_bench.exe --frames 300
```

## Baseline (before the batch/storage work)

| Build | avg ms/frame | worst ms | draw_calls | vertices |
| --- | --- | --- | --- | --- |
| Debug  | 105.7 | 117.4 | **4006** | 374,702 |
| Release | 6.4 | 9.2 | **4006** | 374,702 |

Reading: ~2 draw calls per button — every solid rect / outline flushes the
text batch and every text run flushes the geometry batch (`draw_rect` records
with the null texture, glyphs with the atlas texture; the batcher flushes on
that switch).

## History

| Commit | Change | draw_calls | release avg ms | notes |
| --- | --- | --- | --- | --- |
| (r88 baseline) | — | 4006 | 6.4 | the batch break measured |
| r89 (2573dae) | white texel (solid joins the glyph batch) | **2** | 6.4 | 2000× fewer calls |
| r89 | text layout cache (one probe, measure+emit shared) | 2 | 5.0 | -22% frame cost |
| r89 | scratch buffers + rotation recurrence in primitives | 2 | 4.8 | -25% total |
| r89 | SDL zero-copy (strided RenderGeometryRaw) | 2 | 4.8 | renderer-side copy removed |
| r89 | draw-list culling + `is_visible` + `needs_redraw` gate | 2 | 4.8 | -25% total |
| geometric culling | draws wholly outside the client area or the clip are never submitted (shapes, text per run/glyph, blur); early outs before fans are built | 2 | 2.7 | vertices 374,702 -> 221,570 (-41%); release avg 4.0 -> 2.7 ms |

## text_area (r102)

`pui_bench --textarea`: a read-only, unwrapped `text_area` holding a 10,000-line log
that grows by one line every frame (1400x800 at 1600x1000). The line layout is
rebuilt only when the text changes (here: every frame), and only the visible
rows are drawn.

| Build | avg ms/frame | worst ms | draw_calls | vertices |
| --- | --- | --- | --- | --- |
| Release | 1.0 | 1.2 | 2 | 8,723 |

## Glyph atlas budget (r103)

`pui_bench --atlas [--budget N]`: one of 20 text sizes per frame (24..138 px),
~80 distinct glyphs each, 600 frames. Release.

| Budget | atlas pages at the end | avg ms/frame |
| --- | --- | --- |
| 0 (unlimited, the old behavior) | 10 | 0.004 |
| 16 (default) | 10 | 0.004 |
| 4 | 4 | 0.005 |

The default scenario is unchanged (2 draw calls, 221,570 vertices).

## Large text and long lists (r108)

New scenes (all 1600x1000, release, median of 3 interleaved runs of 200 frames
against the pre-r108 binary on the same machine):

| Scene | before avg ms | after avg ms | what changed |
| --- | --- | --- | --- |
| `--wrap-type` (360 KB wrapped document, model edited every frame) | 961 | 1.8 | one-pass wrap + per-rebuild advance/kerning memo |
| `--wrap-resize` (same document, width changing every frame) | 909 | 1.9 | same |
| `--bigtext` (static 1 MB log, line numbers, scrolled to the end) | 1.68 | 0.068 | no per-frame hash / newline count / model copy |
| `--longlines` (60 static lines x 12,000 chars, no wrap) | 10.5 | 6.7 | runs stop at the right edge |
| `--vlist` (200,000 rows, `heights_version` 0) | 0.49 | 0.47 | unchanged by design |
| `--vlist-kept` (the same with `heights_version` set) | 0.49 | 0.006 | kept row offsets |
| `--a11y` (default scene + 1 MB text area, accessibility on) | 5.7 | 4.0 | node slots reused |
| default scene | 2.80 | 2.83 | within noise |

What is left in `--longlines`: most of the 6.7 ms is the direct-mapped
layout cache - two pairs of the 47 visible 12,000-glyph lines share a slot
and evict each other every frame (about four full rebuilds per frame).

## Glyph checks and overlay replay (r109)

`pui_bench` release, 5 interleaved runs of 400 frames against r108:

| Scene | r108 median ms | r109 median ms |
| --- | --- | --- |
| default | 2.88 | 2.84 (within noise) |
| `--longlines` | 6.69 | 6.80 (within noise) |
| `--atlas` | 0.003 | 0.003 |

The per-glyph liveness check and LRU store were a few nanoseconds per glyph
(~20k glyphs per default frame): below what this benchmark resolves. The
overlay replay change removes one vertex-array copy per open spilling popup
per frame; no scene measures it.

## Rendering-preserving review fixes (r110)

Baseline: `feb1702`. Same machine, MSVC release (`/O2`, `/GL`, precise floating
point), same vendored SDL and bundled fonts. No existing tests, golden BMPs,
golden hashes, theme defaults, geometry, sampling passes or text accumulation
order were changed.

Changes:
- Index batches grow geometrically instead of reserving the exact size per draw.
- Text layouts keep the original 4096 primary slots and add 64 bounded victim
  slots. Victims are reused only for the built-in shaper, the exact raw size,
  and the original build's atlas epoch. Primary hit/eviction history stays
  unchanged (quantized font-size keys otherwise change kerning on collisions).
  Temporary layouts transfer their glyph vectors into the cache.
- Ellipsis reuses the built-in glyph advances when the exact size and atlas
  epoch match; it keeps the last cut for the same width/size/tail/epoch. Custom
  shapers and quantized-size mismatches retain the original codepoint scan.
- A context caches the gamma coverage table by the raw gamma value, including
  the existing identity branch and minimum-gamma clamp.
- Tables retain separate scratch buffers for every active nesting depth.
- Blur allocates and clears eighth/sixteenth targets only when first needed.
  Target dimensions, blit rectangles, radius thresholds and composites match
  the baseline.

`pui_perf_tests` adds eight regressions: geometric growth with exact indices,
move ownership, colliding layouts and invalidation, quantized-size collision
history, ellipsis
against the original algorithm (UTF-8, fractional sizes, all alignments,
substituting/fallback shapers), exact gamma coverage, allocation-free nested
table scratch reuse, and lazy blur allocation/resize/failure recovery. Internal
tests compile through the same `src/pufferui_impl.cpp`; shipping builds have no
test counters or public test API.

`pui_perf_pixels` matches 16 software-rendered pixel fingerprints captured from
the baseline before editing the renderer. They cover odd blur extents and all
pyramid levels, repeated blur calls, gamma values in the same quantization bin,
fractional font sizes, ellipsis, and nested tables of 12/40 columns.

The original 22 golden scenes remain exact in debug and release, including
`blur_stable` (zero drift over 30 frames). All 205 existing core tests, pump
tests, example/tutorial selftests, external core and SDL consumers, API/snippet
checks and clang-format 22.1.3 pass on Windows.

Showcase: `tools/compare_showcase.ps1` checked 33 before/after BMP pairs at
1360x860 (all five pages, light/dark, first/16th frames, scripted overlays) and
a scripted resize to 640x420. Every complete BMP file is byte-identical.
`--no-hud` excludes the timing graph's inherently variable pixels; application
sources are otherwise unchanged.

For reproduction, preserve a baseline `pui_perf_tests --bench` binary and a
baseline `pui_bench` binary before rebuilding. The showcase comparison binaries
use the identical showcase application object and SDL library; only the
implementation object differs (baseline header/slices from `feb1702`). Link
both with identical flags. Run:

```powershell
out/build/x64-release/Release/pui_perf_tests.exe --bench
out/build/x64-release/Release/pui_bench.exe --frames 300
out/build/x64-release/Release/pui_bench.exe --longlines --frames 300
tools/compare_showcase.ps1 -Before path/to/before.exe -After path/to/after.exe -Frames 96 -TimingPages @(0,3)
```

Measurement output and matching BMPs from this run are retained locally under
`build-perf-review/` (gitignored). Times below use interleaved execution order;
they are machine-relative, not cross-platform performance guarantees.

Core benchmarks: median of five interleaved runs, 300 frames each (the first
quarter is warm-up). The geometry counts are identical:

| Scene | before ms/frame | after ms/frame | draw calls / vertices, both |
| --- | --- | --- | --- |
| default | 2.823 | 2.737 | 2 / 221,570 |
| `--longlines` | 6.687 | 0.594 | 2 / 30,935 |

Isolated `pui_perf_tests --bench` workloads: median of five interleaved runs,
normalized per iteration. The setup/font loading is outside the timed region.
Allocation counters are enabled in both versions for these isolated probes.

| Workload | before ms | after ms | mechanism / stable evidence |
| --- | --- | --- | --- |
| cold index capacity, 4,000 quads | 2.36155 | 0.08047 | 4,000 exact capacity increases become geometric growth; indices identical |
| alternate two colliding 12k-character lines | 2.21389 | 0.01221 | zero steady-state allocations after warm-up |
| create/insert a 12k-glyph vector | 0.52300 | 0.45006 | cache owns the original allocation; no 2.88 MB vector copy |
| ellipsis of a static 12k-character label | 1.08726 | 0.01864 | same UTF-8 cut and vertex stream as the original scan |
| rasterize 26 new glyphs, gamma 1.35 | 0.09469 | 0.05433 | exact coverage table reused across glyphs/sizes |
| warmed 12-column table | 0.01531 | 0.01424 | allocations fall from 9 per frame to 0; the time difference is small |

Cold blur with radius 2 at 1600x1000: target creation falls from four targets to
two; RGBA8 payload from 2,125,200 to 2,000,000 bytes. Both versions still perform
two blits. Payload is a dimension-based calculation, not a GPU memory reading.

Showcase timings use the offscreen SDL software renderer at 1360x860, with
`--selftest --no-hud --no-script --page N --frames 96 --perf`. Every run skips
60 warm-up frames and averages the remaining 36. Execution order alternates;
the table reports medians of three runs per version for Overview and five for
Effects (two extra pairs checked the variation in this case).

| Showcase page | before build ms | after build ms | before frame ms | after frame ms |
| --- | --- | --- | --- | --- |
| Overview | 137.711 | 134.474 | 148.815 | 145.025 |
| Effects | 140.742 | 138.981 | 151.913 | 149.291 |

These are small median differences (frame time -2.55% / -1.73%) in a workload
that includes the software renderer's cost. Repeated build averages ranged
133.973..141.648 -> 129.399..134.669 ms for Overview and
133.664..144.308 -> 134.499..153.296 ms for Effects: the Effects ranges overlap
substantially. The isolated long-text gains should not be extrapolated to a
large showcase-wide or accelerated-renderer speedup.

Local evidence: `build-perf-review/core.csv`, `micro.csv`,
`showcase/timing.csv`, `showcase/pixels.csv`, `binary-hashes.csv`, and the
`debug-verified-suite.log` / `release-verified-suite.log` full-suite logs.

## Showcase layout scalability without visual changes (r111, 2026-10-20)

Baseline: `f876bb2`. The new `pui_showcase_tests` harness was compiled and its
baseline executable preserved **before** editing the showcase or SDL upload
path. It includes the actual `examples/pui_showcase.cpp`, with its example
entry point renamed in the test TU. It does not copy the application's drawing
or animation implementation. Both binaries use the same MSVC Release/LTO
settings, bundled fonts, core library and pinned SDL revision.

Five fixes:

- Gradient helpers reject shapes outside the paint area before building their
  vertices. The 1.5 px feather allowance matches the rounded underlay. Tile
  animation, interaction, hover state and context menus still execute.
- Rounded gradients cache the original 28 corner angles' sine/cosine results.
  The angle expressions, vertex arithmetic, colors and triangle order stay exact.
- Grid packing keeps the earliest unoccupied cell instead of restarting at cell
  zero for every item. A featured 2x2 item leaves earlier holes available to 1x1
  items, preserving the original first-fit placement.
- Layout slots belong to the showcase state; their capacity survives frames.
  The warmed Spiral layout performs zero slot allocations.
- The vendored SDL D3D11 upload keeps geometric buffer capacity with headroom
  for transient layout peaks. Growth reserves at least twice the current payload
  when representable, with a 64 KiB minimum and UINT ByteWidth overflow guards.
  Geometric rounding can reserve almost four times the requested payload; reused
  buffers never shrink. Only actual vertex bytes are uploaded, not spare capacity.
  This trades retained buffer capacity for fewer driver allocations during motion.
  If headroom allocation fails, an exact-size allocation is attempted. Allocation
  or mapping failure leaves the old buffer and its capacity intact.

The SDL submodule is unchanged. `cmake/PufferUISdlD3d11.cmake` verifies the pinned
upstream upload function's SHA256 and substitutes `src/sdl_d3d11_buffer.inl` in a
generated source copy for the Windows vendored static build. A changed upstream
function fails configuration for review. Other renderers, disabled D3D11 builds
and system SDL builds retain upstream behavior. Shaders, uploaded vertex values,
indices, draw order, blur passes and sampling are unchanged.

Six regressions cover first-fit layouts against an independent original scan
(72 combinations of width, size, featured position and a dying tile), exact
gradient vertex/index streams against the original arithmetic at clip boundaries,
retained slots including Add, bounded buffer growth using captured Spiral payloads,
exact upload bytes/reuse, and allocation/map failures with exact-size fallback.
The upload tests compile the actual replacement function against a small COM fake;
they run without requiring Windows graphics hardware.

### Reproducible 1150-tile Spiral scenario

`pui_showcase_tests --bench <renderer>` starts with 1150 tiles, Spiral, tile size
64, at 1360x860. Time advances by a fixed 1/60 s. Frames 90, 120 and 150 invoke
the actual Add control by keyboard; releases follow on the next frame. The run
finishes with 1153 tiles. HUD pixels are excluded; normal rendering and motion
remain enabled. The native window is hidden, vsync is off. Timed work is
`begin_frame` + the showcase callback + `end_frame`/presentation; the OS event pump
and process/font startup are outside this measurement.

Five process runs per version alternate before/after order, with no simultaneous
builds or benchmarks. Frames 0..59 warm up. `tools/compare_spiral.ps1` checks that
all 180 per-frame tile/draw/vertex/index counts match and writes the raw CSVs.
Separate capture runs compare frames 89, 91, 98 and 104, so readback does not
affect timing runs. Reproduce with:

```powershell
tools/compare_spiral.ps1 -Before path/to/before.exe -After path/to/after.exe -Renderer direct3d11 -Runs 5
tools/compare_spiral.ps1 -Before path/to/before.exe -After path/to/after.exe -Renderer null -Runs 5
out/build/x64-release/Release/pui_showcase_tests.exe --pixels
```

D3D11 results below are medians of five per-run statistics. Steady state is
frames 60..89; Add statistics use 90..179; P95/P99 use the full warmed interval
60..179 (nearest rank). The worst-frame row is the median of five maxima, not
the maximum across every run.

| Native D3D11 statistic | Before ms | After ms |
| --- | ---: | ---: |
| Steady median frame | 16.286 | 14.581 |
| Add-period mean frame | 33.252 | 19.397 |
| Worst Add frame per run, median | 182.529 | 40.127 |
| Warmed P95 | 127.037 | 25.167 |
| Warmed P99 | 178.108 | 32.960 |

The Add-period mean falls 41.7%; the median of worst Add frames falls 78.0%.
All five maxima are retained: before 211.306 / 192.400 / 182.529 / 123.872 /
134.040 ms; after 53.790 / 72.919 / 31.287 / 40.127 / 38.750 ms (rounded).
The result reduces the hitch; it is not a guarantee that every frame meets
16.7 ms. SDL still expands indexed geometry into vertex copies on this path.

With the null device (CPU UI construction, no rasterization), the median steady
frame falls 6.376 -> 4.960 ms and the median Add-period mean 8.313 -> 6.409 ms.
CPU timing has outliers too; its five worst Add frames range 9.998..26.597 ms
before and 8.207..34.173 ms after. These measurements describe this machine and
protocol, not a cross-platform FPS promise.

Four software-renderer golden hashes were captured from the baseline before
optimization and pinned in `tests/test_showcase.cpp`. Original golden files and
test expectations remain unchanged. Full BMP comparisons for those four frames
and four native D3D11 frames are byte-identical. The standard 33-case showcase
comparison additionally covers all five pages, both themes, overlays and resize.

Local evidence is in `build-perf-fixes/`: `native-final/summary.csv`,
`null-final/summary.csv`, their per-run CSVs and pixel SHA256 lists,
`software-pixels.csv`, `measurement-summary.json`, and the full-suite logs.
The executable pair is `build-perf-fixes/spiral-before.exe` / `spiral-after.exe`;
their SHA256s are in `binary-hashes.csv`. Generated files
and captures are gitignored. Build/run integration includes the new regressions
in quick mode and the four Spiral golden frames in the full suite and CI.

Windows validation: full Debug and Release suites pass, including all 205
existing core tests, pump, eight existing performance regressions, six new
showcase/upload regressions, 22 original golden scenes, 16 existing pixel
references, four new Spiral golden frames, all example/tutorial selftests,
external consumers, API/tutorial checks and clang-format 22.1.3. Installed SDL
consumer checks and a native demo startup/graceful-close smoke also pass.
No existing test source, golden BMP or expected hash was modified.

## Rounded geometry and offscreen paint preparation (r112, 2026-10-20)

Baseline: `8dd5a0a`. Before changing production code, the existing showcase
harness gained `--tiles 1150|5000`, and four additional software golden hashes
were captured at 5000 tiles. The original 1150 hashes and all existing test
bodies/expectations are unchanged. Both measured executables compile the real
showcase and link the normal core/SDL libraries with MSVC Release/LTO, without
review instrumentation, fast-math, reduced motion or altered drawing settings.
The baseline executable was preserved before the three production fixes.

- Rounded fans copy an immutable, constexpr 324-index topology. Vertex positions,
  UV callbacks, colors, feather geometry and triangle order keep their original
  arithmetic. The private helper also preserves indexing with prefilled scratch.
- Draw-list index appends open the destination range once, then write the original
  indices plus the original vertex base in order. Capacity still grows geometrically:
  at least 64 indices, then doubling (with a max-size overflow guard). This can retain
  up to roughly twice the current index payload after growth; buffers remain reusable.
  It does not restore the old exact-size reserve per primitive.
- Shadows reject a conservative union of all four layers and their feather before
  preparing colors. Tile fills prepare HSV/alpha colors only if the body or gloss
  can paint. The gloss has its own bounds because it can extend below a short,
  animated body. Outlines, labels, animation, hover, interaction and menus still run.

The public triangle AABB cull, SDL upload policy, shaders, blur and sampling are
unchanged. A broader AABB-bypass experiment from the review was not included.

Four new regressions compare against independent r111 implementations:

- Full fan vertices/UVs/colors and indices across 32 rect/radius/prefix cases,
  plus zero scratch allocations after warm-up.
- Bulk appends across 12 scale/remap/disabled combinations and mixed payloads,
  exact vertex transforms/index order, and geometric growth across 2000 submits.
- Original shadow streams across 270 clip/rect/spread/signed-offset cases.
- Original tile streams across 168 clip/rect/selection/hover/mode cases, including
  empty/thin clips, gloss-only visibility, outlines and labels. Font atlases are
  warmed before comparing batching so atlas creation is outside the comparison.

### Matched Spiral measurements

The r111 protocol is retained: 1360x860, tile size 64, Spiral, fixed 1/60 time,
180 frames, actual keyboard Add at frames 90/120/150. Counts finish at 1153 or
5003. Five before/after processes per renderer and tile count alternate order;
no builds or other tests run during timing. Capture/readback uses separate runs.
The native D3D11 window is hidden with vsync off. Timed work is begin-frame,
showcase construction, and end-frame/presentation; startup and the OS event pump
are outside the interval. Null measures CPU construction without rasterization.

Each table entry is the median of five per-run statistics. Steady medians use
frames 60..89; Add means/maxima use 90..179; P95/P99 use warmed frames 60..179
with nearest rank. These are this revision's matched measurements, not a direct
comparison against timings from earlier reports or the instrumented review.

| Renderer / initial tiles | Steady median before/after ms | Add mean before/after ms | Add mean reduction |
| --- | ---: | ---: | ---: |
| Null / 1150 | 2.696 / 1.658 | 3.538 / 2.265 | 36.0% |
| Null / 5000 | 4.356 / 2.985 | 6.600 / 4.305 | 34.8% |
| D3D11 / 1150 | 10.771 / 10.128 | 14.592 / 12.744 | 12.7% |
| D3D11 / 5000 | 12.769 / 12.154 | 24.995 / 21.553 | 13.8% |

| Renderer / initial tiles | Worst Add frame, median before/after ms | Warm P95 before/after ms | Warm P99 before/after ms |
| --- | ---: | ---: | ---: |
| Null / 1150 | 5.472 / 4.018 | 4.664 / 2.939 | 5.025 / 3.702 |
| Null / 5000 | 30.630 / 18.665 | 13.689 / 7.835 | 22.282 / 17.441 |
| D3D11 / 1150 | 29.026 / 24.256 | 22.483 / 17.766 | 26.321 / 23.031 |
| D3D11 / 5000 | 208.343 / 195.304 | 44.902 / 39.641 | 138.169 / 136.539 |

Outliers are retained. All five native 5000-tile maxima were 588.821 / 208.343 /
232.848 / 203.602 / 207.208 ms before and 310.143 / 194.428 / 204.175 / 180.573 /
195.304 ms after. Null 5000 also retained a 47.250 ms candidate outlier. The
three fixes reduce construction and average frame cost; they do not eliminate
large-population native hitches or guarantee a 16.7 ms budget.

```powershell
tools/compare_spiral.ps1 -Before path/to/before.exe -After path/to/after.exe -Tiles 1150 -Renderer null -Runs 5
tools/compare_spiral.ps1 -Before path/to/before.exe -After path/to/after.exe -Tiles 5000 -Renderer direct3d11 -Runs 5
out/build/x64-release/Release/pui_showcase_tests.exe --pixels
out/build/x64-release/Release/pui_showcase_tests.exe --pixels --tiles 5000
```

Raw evidence is local and gitignored under `build-perf-fixes2/`: the frozen
`spiral-before.exe` / `spiral-after.exe`, `binary-hashes.csv`, four workload
directories with all per-run CSVs and per-frame geometry checks,
`measurement-summary.json`, native capture hashes, software comparisons and
validation logs. All 180 frame counts match in every before/after timing pair;
the eight native Spiral BMP pairs are byte-identical. The 5000-tile goldens are
included in the full test runner and Windows/Linux/macOS CI.

Windows validation: `tools/run_tests.py --format` and `--release --format` both
finish with `all steps passed`. This covers the unchanged core/pump suites,
10 private performance regressions (8 existing + 2 new), 8 showcase/upload/paint
regressions (6 existing + 2 new), all 22 original golden scenes, all 16 existing
pixel fingerprints, and both four-frame Spiral golden sets. All example/tutorial
selftests, external core consumers, API reference and tutorial snippet checks,
and clang-format 22.1.3 pass. The two new regression files also pass an explicit
format check while untracked. The regenerated single-header distribution compiles
and runs; a native Debug demo opens and closes gracefully with exit 0.

All eight software Spiral BMP pairs are byte-identical, in addition to the eight
native pairs. The standard 33-case showcase comparison also matches byte-for-byte
across all pages, both themes, modal layers and resize. No original golden BMP,
existing test body or old expected hash was changed. The measurement runner's
tile-count extension and new regressions are the only test-source additions.
`scope-verification.json` confirms this and confirms that the final Release
executables have the same SHA256s as the measured candidate executables.

## r113 - indexed D3D11 geometry and upload size classes (2026-10-21)

Baseline: `b2f6e2d` (r112). The benchmark harness was extended and its baseline
executable frozen before changing the production renderer. Existing test bodies,
original golden BMPs and all earlier expected pixel hashes remain unchanged.
The two changes apply only to the pinned, bundled Windows SDL D3D11 renderer;
system SDL and other renderer implementations retain their upstream code.

The 5000-tile Spiral hitch follows the second Add: random-position insertion
shifts many layout indices, so existing straight spring paths cross the center.
That motion and its drawing remain unchanged. At frame 127 the application
submits 1,963,939 vertices and 8,351,460 indices. Expanding each index into a full
32-byte vertex requires about 267.25 MB; shared vertices plus 32-bit indices
require about 96.25 MB, excluding record/alignment overhead.

- Geometry now uploads each source vertex once and keeps the original index
  sequence for `DrawIndexed`. Position/UV/color arithmetic, linear-color
  conversion, shader inputs, blend/scissor state and triangle order are retained.
  Each geometry record starts at a 32-byte boundary so point/line commands can
  share the upload arena. Checked record/arena sizes fit D3D11's 32-bit ByteWidth
  and draw offsets, including alignment and 32-bit SDL arena growth limits.
- Uploads up to 1 MiB rotate through five small slots; 1..4 MiB use a medium slot;
  larger uploads alternate between two large slots. All eight slots remain
  renderer-owned. A 192-byte blur/copy quad no longer discards
  a buffer retained for a large tile batch. Cursors and the active buffer belong
  to each renderer. Two large resources allow the previous geometry upload to
  remain in flight; selection advances only after a successful upload. Capacity
  growth, exact-size fallback, old-resource ownership
  on allocation/map failure, release and device recreation retain their behavior.

The overlay checks the complete normalized upstream source SHA256 as well as
the previous upload-function hash, and generates the replacement during CMake
configuration. The SDL submodule is untouched. No explicit flushes, global
policy switches, animation changes, reduced geometry or shader changes are used.

Three added portable regressions compile the actual queue, dispatch and upload
slices with SDL/COM fakes. They cover 48 combinations of textured/untextured,
linear/nonlinear, 8/16/32-bit/implicit indices and aligned/misaligned prior arena
contents against the original expanded shader-input stream; actual indexed draw
offsets and state selection; empty/invalid/overflow/allocation failure cases;
size-class thresholds, repeated tiny uploads after large ones, two independent
renderers, map retry and resource recreation. Existing upload regressions still
exercise enlarged-allocation fallback and old-buffer preservation.

### Matched 5000-tile Spiral measurements

MSVC Release/LTO, native D3D11, hidden 1360x860 window, vsync off, Spiral size 64,
fixed 1/60 time, 180 frames and actual keyboard Add at 90/120/150 (5000 -> 5003).
Five runs per version alternate order, with no concurrent builds/tests. Startup
and the OS event pump are outside the timed begin/build/end-frame interval.
Capture/readback runs separately. Each result below is the median of five
per-run statistics: steady median at 60..89, Add mean/max at 90..179, warmed
nearest-rank P95/P99 at 60..179. Outliers are retained. Comparisons against older
reports or the instrumented review are not matched measurements.

<!-- r113-measurements -->
| Statistic (median of five per-run values) | Before ms | After ms | Reduction |
| --- | ---: | ---: | ---: |
| Steady median | 27.920 | 20.826 | 25.4% |
| Add mean | 44.011 | 30.191 | 31.4% |
| Worst Add frame | 176.902 | 105.742 | 40.2% |
| Warm P95 | 92.041 | 59.128 | 35.8% |
| Warm P99 | 158.049 | 101.145 | 36.0% |

| Peak frame (median across five runs) | Before ms | After ms |
| --- | ---: | ---: |
| 125 | 168.871 | 57.141 |
| 126 | 158.049 | 76.697 |
| 127 | 152.831 | 96.020 |
| 128 | 144.746 | 105.742 |
| 129 | 130.847 | 102.027 |

The complete per-run Add means/maxima are retained:

| Run | Before mean / maximum ms | After mean / maximum ms |
| --- | ---: | ---: |
| 1 | 44.011 / 183.284 | 31.382 / 109.235 |
| 2 | 44.140 / 235.221 | 29.493 / 105.742 |
| 3 | 43.088 / 154.117 | 30.191 / 104.312 |
| 4 | 42.972 / 168.871 | 29.075 / 102.929 |
| 5 | 45.301 / 176.902 | 171.438 / 1178.140 |

**The fifth candidate run is a material exception, not a discarded sample.**
Its worst frame is 128, included in both the software goldens and native BMP
comparison. Pooling all 450 Add frames per version gives an arithmetic mean
of **43.902 ms before and 58.316 ms after (32.8% slower)**, rather than the
median-of-run-means improvement above. The aggregate wall timers do not establish
the cause of this exception. These results support a typical-frame improvement
and exact output equality, but do not establish uniformly improved latency or
elimination of the remaining hitches. A 16.7 ms frame budget is not guaranteed.

Null-renderer control (five alternating runs, same final binaries) has an Add
mean median of 3.667 -> 3.744 ms and a steady median of 2.629 -> 2.650 ms. The
core construction path is unchanged; the native gain is in the renderer path.
Native timings varied substantially between measurement blocks; no timing from
an earlier report or an intermediate candidate is substituted for the final data.

The final upload distribution was chosen with a separate, bounded indexed-only
trial: three runs each of 6-small/1-medium/1-large, 5/1/2 and 4/2/2 using the same
local prototype executable. Their median Add means were 55.534 / 49.592 / 55.484 ms;
median worst frames were 263.734 / 183.256 / 212.973 ms. Two alternating large
resources improve reuse while the previous upload is still in flight, at the
cost of retaining a second large allocation versus the single-large prototype.
The total resource count remains eight. The selected policy also matched all
14 baseline native images in that trial. These selection timings are separate
from the final before/after table. All trial runs, including the earlier
single-large-buffer candidate's variable tails, remain in the local evidence.
<!-- /r113-measurements -->

### Image equality and verification

The four existing 1150-tile and four existing 5000-tile software goldens are
retained. Ten new pre-fix software references cover frames
120/124/125/126/127/128/129/135/156/179, including the actual largest geometry
peak. `--peak-pixels` runs all three Adds and is part of the full test runner and
Windows/Linux/macOS CI. Native captures compare complete before/after BMPs at
these ten frames plus the original 89/91/98/104. The comparison script also
requires all 180 application draw/vertex/index counts to match in every pair;
these are application submissions, not the driver's internal GPU draw count.

```powershell
tools/compare_spiral.ps1 -Before build-perf-fixes3/before.exe -After build-perf-fixes3/after-two-large.exe -Tiles 5000 -Renderer direct3d11 -Runs 5 -Output build-perf-fixes3/5000-direct3d11-two-large
out/build/x64-release/Release/pui_showcase_tests.exe --peak-pixels
python tools/run_tests.py --format
python tools/run_tests.py --release --format
```

Raw CSVs, separate captures, preserved binaries, binary hashes and validation
logs remain local under gitignored `build-perf-fixes3/`. Software reference
hashes were obtained before the production edits; their initial collection log
reports mismatches against temporary zero placeholders, not accepted test
failures. The committed expectations are those captured baseline values.

Final Windows validation: both complete Debug and Release runs finish with
`all steps passed`, including unchanged core/pump tests, 10 private performance
regressions, 11 showcase regressions (8 existing + 3 new), 22 original golden
scenes, 16 original pixel fingerprints, 8 original Spiral references and the
10 new peak references. All example/tutorial selftests, external core consumers,
API reference, 57 tutorial snippets and clang-format 22.1.3 pass. Installed SDL
consumers pass in matching Debug and Release configurations using the existing
consumer source. The native D3D11 demo starts and closes normally with exit 0.
Linux/macOS CI runs the new peak mode; those platforms were not run locally.

All 61 final BMP pairs are byte-identical: 14 native Spiral frames, 14 software
Spiral frames and 33 standard showcase cases across pages, themes, scripted
layers and resize. Every application's count/draw/vertex/index tuple matches
at all 180 frames in all five native timing pairs. The final measured test
executable and the compared showcase executable have the same SHA256s as their
final build outputs. Existing test bodies, previous hashes and golden BMPs are
unchanged, and the SDL submodule is clean.

The authoritative local receipt is
`build-perf-fixes3/two-large-measurement-summary.json`, with complete per-run
statistics, peak-frame timings, pixel hashes, source/binary SHA256s and scope
checks. Final logs use the `two-large-` prefix; native raw runs/captures are in
`5000-direct3d11-two-large/`. Earlier `5000-direct3d11-final/` and `-repeat/`
directories describe the rejected single-large-buffer candidate, not the final
implementation. The initial `5000-direct3d11/` series predates the final bounds
and test-fixture edits and is not pooled with final results.
