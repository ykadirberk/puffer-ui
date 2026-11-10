# Vulkan analytic rounded rectangles

The old rounded gradient drew a feathered solid underlay and a color fan
with a hard edge. The native analytic path uses the same four corner colors
and an underlay tint. It blends them inside one shape with a continuous
1.25-framebuffer-pixel coverage ramp. The Vulkan showcase uses this path for
solid rounded fills and gradients. Each instance uploads 56 bytes and expands
to a quad. A run of consecutive shapes with the same clip shares one draw.

## 5000-item Spiral

The Release benchmark rendered the actual Layout lab at 1360 × 860, Spiral,
size 64 and 5000 starting items. It activated Add by keyboard at frames 90,
120 and 150. Both paths used the same SVG icons and HarfBuzz/Slug text.
The runs used an AMD Radeon RX 9070 XT on Windows with driver version
`8389024` as reported by Vulkan. Timing used an offscreen surface with no
validation layers, readbacks or HUD. GPU time is the sum of completed Vulkan
timestamp intervals for the 180-frame run.

Each upload mode had one warmup per path and five alternating measured pairs.
The table gives the median of the five per-run measurements. Worst observed
frame keeps the single highest time across all five runs in each column.
Frame times include begin, UI build and submission. Setup ends before frame 0.

| Measurement | DIRECT mesh | DIRECT analytic | STAGED mesh | STAGED analytic |
| --- | ---: | ---: | ---: | ---: |
| Mean frame (ms) | 3.762 | 1.554 | 4.136 | 1.975 |
| p95 frame (ms) | 5.999 | 2.083 | 7.816 | 3.833 |
| p99 frame (ms) | 16.318 | 3.206 | 20.502 | 5.032 |
| Highest of the three Add frames (ms) | 3.191 | 1.783 | 3.398 | 1.826 |
| Frame 0 (ms) | 5.946 | 6.481 | 6.863 | 6.992 |
| Worst frame per run (ms) | 17.406 | 6.481 | 24.341 | 6.992 |
| Worst observed frame (index and ms) | 127 · 21.595 | 0 · 6.942 | 128 · 30.046 | 0 · 28.078 |
| Setup (ms) | 168.292 | 162.056 | 172.010 | 181.522 |
| Completed GPU time over 180 frames (ms) | 155.372 | 128.441 | 186.587 | 123.878 |
| Initial resident upload pages (MiB) | 65.59 | 65.59 | 129.59 | 129.59 |

Each upload mode submitted 70,599,128 recorded vertices in the mesh control
and 4,635,784 in the analytic path, including 845,252 rounded instances. The
frame upload totals were 2,612,202,712 and 89,091,752 bytes, a 96.6% reduction.
The number of recorded draws grew from 15,015 to 30,938 as mixed paints switched
pipelines. Total GPU time fell by 17.3% in DIRECT and 33.6% in STAGED. Both paths
recorded zero draw-time buffer allocations in the timed runs. The conservative
preflight still reserved the same initial upload pages for this workload.

One STAGED analytic run reached 28.078 ms at frame 0. The other four frame-0
times ranged from 6.420 to 8.401 ms. In four of the five STAGED runs the worst
analytic frame was the first frame. The mesh peaks occurred at frames 126–128
in every run on both upload modes. In the fifth STAGED pair, frame 128 uploaded
71.41 MB on the mesh path and 1.54 MB on the analytic path. Its time dropped
from 30.046 ms to 2.057 ms in that pair.

Build `pui_vulkan_rounded_showcase_tests` in a Vulkan Release configuration.
Run `--bench --mesh-rounds` for the mesh control and `--bench` for analytic
coverage. Add `--staged` to either command for STAGED uploads. The executable
writes all frame times and geometry counters as CSV and reports setup and GPU
timestamps on stderr.

## Pixel and lifecycle checks

- `pui_vulkan_rounded_tests` checks an independent circular coverage oracle at
  1×, 1.5×, 2× and 4× DPI. It also checks translucent gradient compositing,
  clipping, mesh order, the fixed batch bound and exact cache-on/off pixels
  in both upload modes.
- `pui_vulkan_rounded_showcase_tests` compares every RGBA byte for all 180
  analytic Spiral frames in DIRECT and STAGED, including the three Add frames
  and all observed peak indices. Validation is enabled for these comparisons.
- Actual `pui_showcase_vulkan --selftest` captures verify that `--mesh-rounds`
  exactly reproduces the previous build in Overview, the 5000-item Spiral
  and a 520 × 580 resized window. The default analytic captures show the
  intentionally different antialiased edges and gradient interpolation.

## Library follow-up

The library bounds oversized Vulkan geometry hints on analytic surfaces with
a smaller starting reservation. Recorded high-water usage can grow it within
the existing surface and device limits. Vulkan keeps the underlay and the
four original side triangles for square gradients, omitting triangles whose
corner vertices coincide. The portable mesh painter preserves its submitted
geometry. Rounded fans reuse their fixed indices. Idle context menus bypass
the ID hash and keep the existing deferred-result path.

The comparison links one instrumented Release showcase benchmark object
against the `464dd7d` library and the candidate library. The unchanged workload
renders the same 180 frames and three Add events. Each mode uses one warmup
per version and seven alternating measured pairs without readbacks. Timings
below are medians of per-run values, except the single worst frame. Use
`pui_vulkan_rounded_showcase_tests --bench` and `--bench --staged` on each
revision to reproduce the workload and the upload counters.

| Measurement | DIRECT before → after | STAGED before → after |
| --- | ---: | ---: |
| Resident upload pages (MiB) | 65.59 → 9.59 | 129.59 → 17.59 |
| Device-side staged pages (MiB) | — | 64 → 8 |
| Mean frame (ms) | 2.512 → 2.432 | 2.352 → 2.227 |
| p95 frame (ms) | 5.995 → 5.720 | 6.138 → 5.286 |
| p99 frame (ms) | 7.412 → 6.988 | 7.519 → 6.657 |
| Frame 0 (ms) | 5.759 → 6.596 | 5.577 → 6.418 |
| Total completed GPU time (ms) | 96.848 → 93.714 | 100.268 → 90.840 |
| Worst observed frame (index and ms) | 59 · 14.613 → 8 · 15.196 | 108 · 12.230 → 107 · 13.433 |

The median paired change in mean frame time was −7.25% DIRECT and −13.46%
STAGED. The p99 improved in four of seven pairs in each mode. Frame 0 grew
in six DIRECT pairs and all seven STAGED pairs. Median frame 0 UI construction
rose from 5.530 to 6.347 ms DIRECT and from 5.178 to 6.009 ms STAGED.
All runs uploaded 201,092 bytes at frame 0 with zero slot wait and zero
draw-time buffer allocations.
The slowest candidate frames spent 13.510 ms DIRECT and 11.881 ms STAGED
waiting for a slot, with zero draw-time buffer allocations. The preliminary
build with a broader mesh change recorded a 34.196 ms DIRECT frame, of which
32.930 ms was UI construction.

Both modes recorded the same work reduction over 180 frames. Uploads fell
from 89,091,752 to 87,616,424 bytes, removing 46,104 vertices and 138,312
indices. At frame 2, the painter removes degenerate triangles from 650
square gradients.
That frame uploads 572,984 bytes instead of 1,072,184, a 46.6% reduction.
Its 1,421 draws and 1,398 pipeline binds are the same in both versions.
Both versions submit 180 times and report zero draw-time buffer allocations.

Validation compares the SHA-256 of every full RGBA frame against `464dd7d`
with DIRECT and STAGED uploads and both analytic and mesh rounded paths.
All 720 comparisons match. Regressions exercise the bounded reservation,
fitting hints, recorded-peak growth, exact square-gradient pixels at three
DPI scales and deferred menu picks. The existing Showcase stream test still
checks the portable vertex and index stream at the clip edges. [Vulkan polygon rasterization rules](https://docs.vulkan.org/spec/latest/chapters/primsrast.html#primsrast-polygons-basic)

Full Release verification passed all 60 Vulkan cases, the existing SDL
goldens and Spiral peak frames, installed consumer checks, example selftests,
API and tutorial checks, and clang-format.
