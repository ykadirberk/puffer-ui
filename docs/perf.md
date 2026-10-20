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
