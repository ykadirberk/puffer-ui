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
