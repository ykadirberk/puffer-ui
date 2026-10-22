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

## Lossless core performance fixes (r114)

Baseline: `79c7007fd176aaa7219306dc03c44a1dd26ed4f8`. This revision implements
all six findings from the core review. Public APIs, geometry density, primitive
order, feathering, font rasterization, blur passes and existing tests/references
are retained. The SDL source, overlay and submodule are unchanged.

- Long built-in layouts (at least 256 UTF-8 bytes) memoize only pure kerning
  queries for that build. Glyph lookup/rasterization order, raw-size arithmetic,
  primary/victim layout-cache behavior and custom-shaper calls stay unchanged.
  Short labels keep the original path. The 2048-pair memo uses 24 KiB per context.
- Rect motion uses one map insertion/lookup and one spring plan for all four
  axes. It retains the original substeps, divisions, float operation order and
  settling thresholds. An axis at the exact target with zero velocity skips
  integration only with finite coefficients; unusual/overflowing coefficients
  retain the original arithmetic. Snap, origin, duplicate-step and TTL behavior
  are unchanged.
- Sector index topology is cached by segment/ring count in eight context-owned
  slots. Only at most 128 segments are retained (at most 72 KiB of index elements);
  larger arcs use the original scratch path. Vertex recurrence and triangle
  order are identical, including cold caches and collisions.
- State collection keeps a conservative oldest-use bound. A scan is skipped
  only when the original `now - last_used > ttl` test cannot expire any entry.
  It rescans for backwards/non-finite time or a different TTL and includes future
  inserts in the bound. Expiration remains exact, including floating-point TTL
  boundaries. A due scan remains O(n); this does not promise elimination of every
  worst-frame hitch.
- Empty ID tables return immediately from `clear`, retaining capacity and the
  original duplicate-detection semantics. Sparse nonempty tables still scan.
- Text-area gutter marks retain scratch per active call depth. The shared typed
  scratch-scope helper also serves nested tables. Both Debug and Release avoid
  gutter scratch allocations after warm-up; nested calls preserve outer marks.

### Reproducible workloads and references

`pui_core_bench` compiles `tests/bench_core_performance.inl` with the actual
implementation. Eight fixed workloads cover all six changes; allocations are
counted along with elapsed time. The before executable uses the exact same
benchmark source with the pinned baseline header/slices and matching MSVC
Release `/O2`/LTO settings. `tools/compare_core.ps1` alternates execution order
over five runs per version and retains every sample, including outliers.

The actual showcase uses the existing 5000-tile Spiral protocol: size 64,
1360x860, 180 fixed-dt frames, actual keyboard Add activations at 90/120/150,
and 5003 items at the end. `tools/compare_spiral.ps1 -Tiles 5000` compares all
180 count/draw/vertex/index tuples and all fourteen capture frames, including
second-Add peak frames 125..129. Timing and captures run separately from builds
and tests. Null-device CPU timings and native D3D11 total timings are reported
separately; neither is a promise about other machines/renderers.

Seven new private regressions leave the previous ten bodies unchanged:
original built-in text metrics and shaper paths; 20,000 original spring steps
including hitches; 900,000 rect submissions over 180 frames with 5000 items;
256 sector-stream comparisons and bounded-cache/fallback/allocation checks;
exact TTL/reference-map equivalence with backwards time and scan suppression;
large ID-table reuse; and allocation-free/nested text-area scratch.

`pui_core_perf_pixels` adds 24 software-rendered references captured with the
unchanged baseline implementation. They cover multiple gutters, wrapped and
unwrapped text, long UTF-8 runs, translucent outlines, sector topology, motion,
raw font-size collisions, gamma, DPI and a TTL time jump. Existing golden BMPs,
16 performance fingerprints and 18 Spiral references are untouched. The new
pixel target is included in the full runner and Windows/Linux/macOS CI.

### Core results

Median of five per-version averages, milliseconds per workload iteration:

| Workload | Before | After | Change |
| --- | ---: | ---: | ---: |
| 37,000-character uncached layout, warm glyphs | 8.464858 | 5.905058 | -30.2% |
| 5000 stationary rects, whole core frame | 0.423634 | 0.248468 | -41.3% |
| 5000 continuously retargeted rects, whole core frame | 0.369494 | 0.287837 | -22.1% |
| 5000 sectors, warm topology/draw buffers | 0.670414 | 0.587930 | -12.3% |
| Core frame with 5000 live rect states, none expired | 0.014864 | <0.001 | scan skipped |
| Core frame with 100,000 live rect states, none expired | 0.944759 | <0.001 | scan skipped |
| Empty ID table after 200,000 identities | 0.133853 | <0.001 | scan skipped |
| Static numbered text area, 1000 lines | 0.029123 | 0.025467 | -12.6% |

The last workload drops from **11 to 0 allocations per warm frame**. Layout
vector allocations remain 27 per forced rebuild; the kerning optimization does
not change their lifetime or glyph ordering. Other warm workloads allocate zero.
Sub-microsecond empty-frame/empty-clear numbers are measurement-floor values;
the useful result is that the linear scan no longer executes. Expired state,
large topology fallbacks and cold-start allocations still have their normal cost.

### 5000-tile Spiral results and variability

The initial five alternating pairs showed substantial temporal variation and
an adverse null-renderer pooled mean. Six additional pairs balance which binary
runs first (three each). Both series are retained: **eleven pairs per renderer**,
990 timed Add-window frames per version per renderer. The following medians
cover all eleven run summaries; pooled means cover every Add-window frame.

| Metric (ms) | Null before | Null after | D3D11 before | D3D11 after |
| --- | ---: | ---: | ---: | ---: |
| Median steady-frame median | 2.678850 | 2.531800 | 5.517700 | 5.393350 |
| Median Add-window mean | 3.740218 | 3.667017 | 10.435218 | 10.173261 |
| Median worst Add frame | 14.562200 | 14.849600 | 94.942700 | 89.970900 |
| Median warm p95 | 7.403000 | 7.490900 | 25.381700 | 22.668900 |
| Median warm p99 | 13.188500 | 13.624800 | 83.211100 | 82.061400 |
| Pooled Add-window mean | 4.206690 | 4.234439 | 11.620877 | 11.372413 |

The complete null-renderer mean is **0.66% worse**; its median worst frame is
also worse. Native pooled mean improves 2.14%, but the confirmation series alone
changes 10.029872 -> 9.970215 ms (-0.59%) and its median worst frame is slightly
worse (87.949600 -> 88.839450 ms). These small native changes do not establish
a uniform total-frametime gain. Spikes remain: the initial candidate's frame
126 reaches **190.5762 ms**, versus the baseline series maximum of 172.6277 ms.
Frame 126 is included in both software and native pixel comparisons. No outlier
or unfavorable series is omitted, and this revision does not claim a 16.7 ms cap.

For context, the initial null pooled mean is 4.825485 -> 5.056317 ms; the balanced
confirmation mean is 3.691027 -> 3.549542 ms. All eleven runs' per-frame CSVs,
per-run summaries and maxima are retained. These API wall timers do not identify
the cause of the observed timing drift or driver stalls.

The targeted state-collection saving is visible in the application: pooled Add
`begin_frame` time is 0.103439 -> 0.000368 ms on null, and 0.245128 -> 0.002339 ms
on D3D11. Null build time is 4.100360 -> 4.231015 ms, so the begin-frame saving
does not translate into a lower pooled whole-frame mean in this experiment.
Native build time is 10.508832 -> 10.459348 ms; end time is
0.866917 -> 0.910726 ms. The draw/vertex/index stream is unchanged throughout.

Five paired 180-frame control runs also retain every sample:

| Existing core scene | Before median avg ms | After median avg ms | Median paired delta |
| --- | ---: | ---: | ---: |
| default | 3.036 | 3.184 | -0.34% |
| `--longlines` | 0.696 | 0.684 | -2.31% |
| `--wrap-type` | 2.401 | 2.486 | -2.54% |

Default and wrap-type cross-run medians are slower even though the medians of
matched-pair percentage differences are negative. Default paired differences
range from -16.96% to +28.03%; long-lines includes a +29.74% pair. These controls
do not establish a general scene speedup. Counts and atlas-page totals match in
every paired control. Dedicated core improvements are reported separately from
these noisy application/control timings.

### Visual and validation receipt

All **85 canonical BMP pairs** are byte-identical: 24 new core scenes, fourteen
native 5000-tile Spiral frames, fourteen software Spiral frames and 33 showcase
cases (pages/themes/scripted layers/resize). The confirmation series' fourteen
additional native pairs match each other and the original native captures.
Every count/draw/vertex/index tuple matches for all 180 frames in all eleven
pairs of each timed renderer and in the software capture pair.

Final Windows Debug and Release full suites both finish with `all steps passed`:
unchanged core/pump tests, **17** private performance regressions (10 existing,
7 new), 11 existing showcase regressions, 22 existing golden scenes (including
zero blur drift over 30 frames), 16 existing pixel fingerprints, 24 new core
references and all 18 existing Spiral references, including the 5000-tile peak.
Every example/tutorial selftest, external core consumers, API-reference drift,
57 tutorial snippets and clang-format 22.1.3 pass. New source files also pass
an explicit formatting check. Installed SDL consumers pass in matching Debug
and Release configurations; the amalgamated-header smoke passes. The native
D3D11 demo starts and closes normally with exit 0. Linux/macOS CI is wired for
the new pixel target; those platforms were not executed locally.

All **49 previously tracked test/reference files** retain their original SHA256s;
no existing test body, hash or golden BMP was changed. Final measured/copied
executables and all source/font hashes are pinned in the receipt and verified
against the final workspace/build outputs. The SDL submodule is clean.

The committed machine-readable receipt is [perf/r114.json](perf/r114.json): all
core samples, both complete Spiral series and aggregates, every run's worst
warm frame, control samples, image hashes, source/binary identities, raw CSV
hashes and validation results. Local raw artifacts are in
`build-core-perf-fixes/`: `core/`, `5000-null/`, `5000-direct3d11/`, their
`-confirmation/` counterparts, core/software BMPs, `showcase-pixels/` and
`full-{debug,release}.log`. Earlier smoke/prototype CSVs are not final measurements.

Re-run the frozen binaries after builds/tests finish:

```powershell
tools/compare_core.ps1 -Before build-core-perf-fixes/before-core-bench.exe -After build-core-perf-fixes/after-core-bench.exe -Output build-core-perf-fixes/core-repeat -Runs 5
tools/compare_spiral.ps1 -Before build-core-perf-fixes/before-showcase-tests.exe -After build-core-perf-fixes/after-showcase-tests.exe -Output build-core-perf-fixes/spiral-repeat -Renderer direct3d11 -Tiles 5000 -Runs 6
```


## r115 — bounded geometry allocations and retained SDL scratch (2026-10-22)

The 5000-tile Spiral briefly submits roughly 1.96 million vertices and
8.35 million indices after Add. Consecutive core-vector relocations, preserving
unused SDL-arena capacity and oversized dynamic D3D11 resources magnify these
frames. This revision reduces that memory work while preserving draw boundaries,
primitive order, vertex/index arithmetic, color conversion and all existing
pixel references. The faster draw-splitting experiment from the review was
rejected because it changed pixels; it is not part of this revision.

### Implementation and regression coverage

- `draw_context.inl` grows vertex capacity geometrically using checked integer
  arithmetic and `max_size`, avoiding consecutive relocations around the peak.
  The new tests cover complete appended vertex/index streams, the two successive
  peak sizes, integer precision above 2^24 and capacity limits.
- `sdl_d3d11_upload_capacity` retains the small/medium policy. Uploads above
  4 MiB use 25% headroom rounded to 64 KiB, with checked ByteWidth arithmetic
  and the original exact-size allocation fallback. A 67,910,784-byte payload
  asks for 81 MiB rather than 256 MiB. The two large slots, shader inputs and
  cursor advancement rules are unchanged. Failure, retry, retained-resource,
  renderer-isolation and boundary tests exercise the actual upload slice.
- `sdl_d3d11_arena.inl` allocates new arena storage and copies only
  `vertex_data_used` bytes, then lets SDL assign alignment/offsets as before.
  If fresh allocation fails, the original realloc-style path remains available;
  a failed fallback retains the old storage and queued bytes. The wrapper is
  scoped to the local D3D11 geometry overlay; SDL's submodule stays clean.
  Tests include live prefixes 0/3/37/3680, retained offsets, untouched spare
  bytes, allocation failure/fallback, reuse and size_t overflow.
- SDL color scratch keeps its initialized high-water prefix. Smaller draws no
  longer destroy that prefix and force later zero-initialization. The original
  `/255.0f` operations, 20-byte stride and software-renderer stride requirements
  stay fixed. Tests check every possible byte-channel value, retained padding,
  storage and independent devices through the actual SDL draw method.

There are **nine new regressions** in a separate `pui_memory_tests` target; it
also reuses three existing COM-fake upload tests, so its total is twelve.
The original 17 performance and 11 showcase tests remain independent. Restoring
the four old paths in a private source copy makes all four regression groups
fail. No existing test body or reference was edited: all **52 previously
tracked test/reference files** have identical SHA256s. The runner and Windows,
Linux and macOS CI include the new target.

### Matched 5000-tile Spiral measurement

Baseline is `6487aee827752a4a18fd96311939eeefd247b9df`. MSVC Release `/O2 /GL`,
precise floats, the same pinned SDL, fonts and real showcase TU; 1360x860,
Spiral size 64, 5000 initial tiles, fixed dt=1/60, 180 frames and actual keyboard
Add activations at 90/120/150. Final count is 5003. The renderer's queried
adapter is AMD Radeon RX 9070 XT (0x1002/0x7550), feature level 0xb100.

Six initial alternating pairs per renderer were followed by twelve balanced
null and six balanced D3D11 confirmation pairs. All **18 null and 12 D3D11
pairs** are retained, including outliers. Builds, tests and captures were
separate from timed runs. Captures do not enter these timing aggregates.

Add mean pools all frames 90–179. Steady is the per-run median over 60–89.
Worst median is the median of each run's maximum over 90–179. P95/P99 are
per-run quantiles over 60–179, then medians across runs. All 0–179 frame values
are committed in [r115-frames.csv](perf/r115-frames.csv): **10,800 frames**;
the frame CSV is never trimmed to selected favorable frames.

| Metric | Before (ms) | After (ms) | Change |
|---|---:|---:|---:|
| D3D11 Add mean | 13.011271 | 11.416066 | -12.26% |
| D3D11 worst median | 127.307650 | 68.205100 | -46.42% |
| D3D11 warm P99 median | 109.217150 | 56.482350 | -48.28% |
| D3D11 absolute maximum in Add interval | 243.862100 | 108.702100 | -55.42% |
| Null Add mean | 4.182705 | 4.167764 | -0.36% |
| Null worst median | 16.654900 | 13.648950 | -18.05% |
| Null absolute maximum in Add interval | 25.012300 | 27.726600 | +10.85% |

The pooled null mean is effectively flat; it is not evidence of a general CPU
speedup. Its initial and confirmation series point in opposite directions,
and the single slowest null Add frame is worse. Native peaks improve, but the
remaining spikes still exceed a 60 Hz frame budget. There is no claim that
every frame improved or that the hitches have been eliminated.

| Series (all Add frames) | Before (ms) | After (ms) | Change |
|---|---:|---:|---:|
| null-initial | 5.424420 | 5.632241 | +3.83% |
| null-confirmation | 3.561848 | 3.435526 | -3.55% |
| direct3d11-initial | 15.301042 | 12.826085 | -16.18% |
| direct3d11-confirmation | 10.721499 | 10.006046 | -6.67% |

The medians at the peak frames distinguish relocation savings from moving a
maximum to the adjacent frame:

| Frame | Null before/after (ms) | D3D11 before/after (ms) |
|---|---:|---:|
| 124 | 6.7485 / 6.4212 | 25.4259 / 23.9156 |
| 125 | 12.8557 / 12.8376 | 69.3226 / 54.6445 |
| 126 | 14.8233 / 11.1180 | 127.3076 / 53.6028 |
| 127 | 11.9314 / 11.1813 | 63.0113 / 68.2051 |
| 128 | 10.3154 / 10.3872 | 109.2172 / 55.2964 |
| 129 | 9.6621 / 9.1393 | 28.7816 / 48.6514 |

### Pixels and final validation

All **75 full-BMP comparisons** match: fourteen native Spiral frames, fourteen
software Spiral frames, thirty-three other showcase page/theme/script/resize
cases and fourteen additional native confirmation frames. Every timed run has
the same count/draw/vertex/index tuple on all 180 frames in each before/after
pair. The software capture pair also has identical tuples. Capture frames are
89/91/98/104/120/124/125/126/127/128/129/135/156/179, including the second-Add
geometry peak and the later transition.

Full Windows Debug and Release `tools/run_tests.py --format` suites both finish
with `all steps passed`: core/pump, original performance/showcase regressions,
new memory regressions, 22 existing golden scenes (zero blur drift), 16 existing
performance fingerprints, 24 core references, all 18 Spiral references,
every example/tutorial selftest, external consumers, API drift, 57 tutorial
snippets and clang-format 22.1.3. The final new memory test sources were also
rebuilt/run in both configurations. Installed SDL consumers pass in Debug and
Release, the amalgamated-header smoke passes and the native D3D11 demo starts
and closes normally with exit 0. Linux/macOS CI is wired; those platforms were
not executed locally.

The measured candidate executables match the final build SHA256s. Source/font
hashes, every run summary and worst frame, all CSV hashes, the 52 unchanged-file
checks, 75 image hashes and validation results are in
[perf/r115.json](perf/r115.json). Raw frame values are in
[perf/r115-frames.csv](perf/r115-frames.csv). Local binaries, complete logs,
capture BMPs and red-path evidence are under `build-memory-perf-fixes/`.

After builds/tests finish, repeat the frozen executable comparisons:

```powershell
tools/compare_spiral.ps1 -Before build-memory-perf-fixes/before-pui_showcase_tests.exe -After build-memory-perf-fixes/after-pui_showcase_tests.exe -Output build-memory-perf-fixes/repeat-null -Renderer null -Tiles 5000 -Runs 12
tools/compare_spiral.ps1 -Before build-memory-perf-fixes/before-pui_showcase_tests.exe -After build-memory-perf-fixes/after-pui_showcase_tests.exe -Output build-memory-perf-fixes/repeat-native -Renderer direct3d11 -Tiles 5000 -Runs 6
```


## r116 — explicit geometry preparation and bounded Spiral storage

Baseline: `f625323` (r115). This revision addresses the two remaining CPU
storage findings. Vulkan remains a design proposal; no Vulkan backend,
tile cache, shader or D3D11 upload/present policy is implemented here.

### Implementation and startup tradeoff

`geometry_capacity` is an explicit per-batch vertex/index hint.
`ui::prepare_geometry` reserves and initializes context-owned storage while
preserving live geometry, logical sizes, clip/texture state and draw boundaries.
It does not draw or flush. Invalid i32/allocator counts are rejected before
either buffer is changed. Warm-prefix markers survive clear and are invalidated
when ordinary growth relocates storage. The markers are cold fields at the end
of the draw list, preserving the original hot texture/clip field offsets.

`render_device::prepare_geometry` is an independent optional backend hook:
unsupported devices return false and continue their normal path. SDL3 retains
an initialized color prefix, preserving existing colors/padding and exactly the
same `/255.0f` conversion and software-renderer stride. Application-owned devices
can be prepared directly even when a measurement/decorator device wraps drawing.

Showcase prepares only the Spiral page. Its budget comes from four rounded
shadow fans, both gradient underlays/fills, a hovered/selected outline and the
longest u32 label, rounded in 128-tile buckets. CPU preparation is bounded to
128 MiB split across core vertices, core indices and SDL colors; the selected
5000-item prefixes total **112 MiB** (2,097,152 vertices, 8,388,608 indices,
and 2,097,152 padded colors). This is initialized payload, not a total process
memory/RSS or GPU-memory limit. Existing larger allocations are not shrunk;
larger draws retain normal growth. Small scenes do not automatically get the
maximum allocation. The CPU renderer atomic documents core-only setup usage.

Preparation is real work. In the unchanged Spiral harness it runs **inside
showcase_frame at frame 0**, inside the measured build interval. It is not moved
outside timing. The first-frame increase below is the explicit cost paid to
avoid allocation/first-touch work during Add. SDL/device/font initialization
and deterministic tile seeding remain outside both versions' frame timing.

### Protocol and final matched measurements

MSVC x64 Release, `/O2 /GL`, precise float, same pinned SDL/D3D11 overlay and
bundled DejaVu fonts. The live SDL adapter query confirms AMD Radeon RX 9070 XT
(0x1002/0x7550, feature level 0xb100). Actual showcase helpers, 1360×860, Spiral,
size 64, 5000 initial items, 180 frames, dt=1/60, actual keyboard Adds at
90/120/150, final count 5003. Fixed mouse and no timing-dependent HUD pixels.
Standard native runs use the existing hidden-window/vsync-off harness; this is
not a foreground/visible all-frame FPS certification.

Main final comparisons: 12 alternating AB/BA null pairs and 6 D3D11 pairs.
Every frame/outlier is retained. Add mean covers 90–179; steady covers 60–89;
warm p95/p99 cover 60–179 and are summarized by the per-run median. The median
run maximum and the absolute maximum are different metrics. Builds, tests,
captures and timing runs were sequential; BMP capture processes are excluded
from timing aggregates. Both versions have identical item/draw/vertex/index
counts at every matched frame.

**Null renderer, main final series**

| Metric | Before (ms) | After (ms) | Change |
| --- | ---: | ---: | ---: |
| Add mean (90–179) | 3.5660 | 3.3263 | -6.72% |
| Median run maximum (90–179) | 12.9345 | 10.2164 | -21.01% |
| Absolute maximum, every frame | 17.2349 | 12.9464 | -24.88% |
| Mean, every frame including frame 0 | 3.1037 | 2.9487 | -4.99% |
| Frame 0 median | 2.7056 | 9.0628 | +234.96% |
| Steady median (60–89) | 2.5014 | 2.4543 | -1.88% |
| Warm p95 median | 6.3839 | 6.1526 | -3.62% |
| Warm p99 median | 10.8341 | 9.9483 | -8.18% |

**D3D11, main final series**

| Metric | Before (ms) | After (ms) | Change |
| --- | ---: | ---: | ---: |
| Add mean (90–179) | 15.1721 | 14.1740 | -6.58% |
| Median run maximum (90–179) | 106.9033 | 107.3521 | +0.42% |
| Absolute maximum, every frame | 112.7335 | 113.8784 | +1.02% |
| Mean, every frame including frame 0 | 12.0273 | 11.4221 | -5.03% |
| Frame 0 median | 30.9176 | 39.9001 | +29.05% |
| Steady median (60–89) | 7.1669 | 6.0309 | -15.85% |
| Warm p95 median | 43.5290 | 43.8580 | +0.76% |
| Warm p99 median | 99.1525 | 98.7393 | -0.42% |

The native main-series mean improves, but its median run maximum and absolute
worst frame do not improve. This revision does not claim to remove the separately
identified driver submission/presentation wait. A shorter CPU build can also
change where that wait becomes visible; moving a wait is not a speedup.

**Fixed-size native confirmation, 6 further AB/BA pairs after validation**

| Metric | Before (ms) | After (ms) | Change |
| --- | ---: | ---: | ---: |
| Add mean (90–179) | 13.6365 | 13.6832 | +0.34% |
| Median run maximum (90–179) | 109.2613 | 109.3208 | +0.05% |
| Absolute maximum, every frame | 113.2024 | 114.5673 | +1.21% |
| Mean, every frame including frame 0 | 10.3926 | 10.4998 | +1.03% |
| Frame 0 median | 26.4103 | 38.1183 | +44.33% |
| Steady median (60–89) | 5.4641 | 5.6643 | +3.66% |
| Warm p95 median | 43.8745 | 43.5088 | -0.83% |
| Warm p99 median | 99.8716 | 98.4129 | -1.46% |

**Native A/A control, the same final executable on both sides, 6 pairs**

| Metric | Before (ms) | After (ms) | Change |
| --- | ---: | ---: | ---: |
| Add mean (90–179) | 13.4557 | 13.3007 | -1.15% |
| Median run maximum (90–179) | 107.4483 | 107.0104 | -0.41% |
| Absolute maximum, every frame | 114.3530 | 113.2070 | -1.00% |
| Mean, every frame including frame 0 | 10.3169 | 10.2181 | -0.96% |
| Frame 0 median | 37.3426 | 36.9814 | -0.97% |
| Steady median (60–89) | 5.3961 | 5.3997 | +0.07% |
| Warm p95 median | 43.5049 | 43.4920 | -0.03% |
| Warm p99 median | 97.3124 | 99.0486 | +1.78% |

The control characterizes observed variability; it does not retrospectively
prove the cause of a particular outlier or turn a native regression into a win.
Native tail-latency reduction is not certified by these two CPU storage fixes.
No Map/Flush/present instrumentation or policy changes are in production.

### Main-series peak-frame medians

| Frame | Null before / after (ms) | D3D11 before / after (ms) |
| --- | ---: | ---: |
| 120 | 2.6043 / 2.5716 | 6.5764 / 6.0473 |
| 124 | 6.3752 / 5.5551 | 27.2076 / 22.8702 |
| 125 | 12.8937 / 7.6768 | 83.8877 / 84.4612 |
| 126 | 10.7860 / 9.5983 | 96.8435 / 98.7393 |
| 127 | 10.7526 / 10.1096 | 106.9033 / 107.3521 |
| 128 | 10.0773 / 10.0019 | 79.9137 / 78.5743 |
| 129 | 9.1546 / 9.0008 | 72.7724 / 72.1145 |
| 135 | 3.5526 / 3.5914 | 13.0580 / 9.4386 |
| 156 | 3.1721 / 3.1886 | 8.1889 / 8.2919 |
| 179 | 2.4493 / 2.4157 | 8.3836 / 5.5453 |

### Regression, pixel and consumer validation

Seven new tests are isolated in `tests/test_geometry_preparation.inl`, target
`pui_preparation_tests`, compiling the actual implementation and showcase helpers:

- Pending batch/texture/clip preservation and exact geometry with 1.5× DPI,
  disabled alpha, solid atlas UVs and UTF-8 text.
- Prepared peak/small/peak buffer reuse, exact vertex/index data and context
  isolation; empty/overflow counts rejected before mutation.
- Normal relocation invalidates warm-prefix markers, with live data preserved.
- SDL initialized-prefix reuse, original padding, all channel conversions,
  device isolation, zero hints and invalid counts.
- Bounded geometry-derived Spiral budget checked against real maximum-label,
  selected and hovered tile painting.
- Actual 180-frame 5000-item Spiral with three real Adds: core vertex/index
  addresses remain stable through every peak and the final item count is 5003.

A private red control keeps the new API shape but disables both preparation
bodies: the new regressions fail (expected exit 1). Production tests pass.
The **54 pre-existing test/reference files are SHA256-identical** to the baseline;
no existing test body, expected value or committed golden was changed.

Final full Windows Debug/Release runners (`--format`) pass: Debug
344.28s, Release 194.00s. This includes core,
pump, 17 original performance regressions, 12 memory regressions, the 7 new
preparation tests, 11 showcase regressions, 22 golden scenes (blur drift 0),
16 performance fingerprints, 24 core frames, 1150/5000 four-frame Spiral
oracles and all ten 5000-item peak oracles, external consumers, all example/
tutorial selftests, API drift, tutorial excerpts and clang-format 22.1.3.
Installed SDL consumers exercise the new API in Debug and Release; an
amalgamated-header consumer also passes. The D3D11 demo builds, starts and
closes normally with exit 0. Linux/macOS CI includes the new target; those
platforms were not executed locally.

Final baseline/candidate capture evidence is **61 full BMP pairs, byte for
byte identical**: 14 main native Spiral frames, 14 native confirmation frames
(both include 120/124/125/126/127/128/129/135/156/179), and 33 normal showcase
page/theme/script/resize cases. Another 14 A/A BMP pairs are control evidence,
not additional optimization coverage. No golden `--update` was used.

### Retained evidence and reproduction

[perf/r116.json](perf/r116.json) records every run, maxima, fixed-frame medians,
phase means, source/font/binary hashes, pixel hashes, unchanged-file checks and
validation receipts. [perf/r116-frames.csv](perf/r116-frames.csv) retains **17,280
timing frames**: 6480 final main frames, 2160 confirmation frames, 2160 A/A
frames, plus the initial implementation's 6480 frames. The initial implementation
placed setup metadata before the hot batch fields; it is explicitly labeled
`initial`, has its own source/binary hashes, and is not pooled with final code.
All initial values/outliers remain available. CSV SHA256 is for LF bytes.
Local frozen executables, raw stdout/stderr, captures, red control and complete
logs remain under `build-geometry-prep-fixes/`.

Build baseline `f625323` and the candidate with the same toolchain and preserve
the executables before replacing either build. The existing, unchanged timing
and image harness can reproduce the final main workload:

```powershell
tools/compare_spiral.ps1 -Before build-geometry-prep-fixes/before-pui_showcase_tests.exe -After build-geometry-prep-fixes/final-after-pui_showcase_tests.exe -Output build-geometry-prep-fixes/repeat-null -Renderer null -Tiles 5000 -Runs 12
tools/compare_spiral.ps1 -Before build-geometry-prep-fixes/before-pui_showcase_tests.exe -After build-geometry-prep-fixes/final-after-pui_showcase_tests.exe -Output build-geometry-prep-fixes/repeat-native -Renderer direct3d11 -Tiles 5000 -Runs 6
```
