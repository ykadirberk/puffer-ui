# Native Vulkan: exact Slug coverage, shaping reuse and compact uploads

Date: 2026-10-29. Branch: `codex/vulkan-native-harfbuzz`. Before: `e18ed126a249f03aa9d15d59ef4dd4f24bd8a8c6`. Main remains `b30a7be5b08d13b7e3c1c535fb3dbd0d615bea12`.

All three optimizations are enabled on the native branch. Triangle topology, glyph positions, text size, gamma, clipping, draw order and blending are preserved. The candidate matches every immutable 5000-item Spiral reference frame.

The native renderer now caches repeated Slug coverage in an R32 float image. The identity retains exact bounds, absolute baseline, scale, gamma and outline location; a hit uses `texelFetch` on the original glyph quad. Four bounded regions retire by GPU timeline. A newly reclaimed region admits glyphs before old pressure can retire it again. The image is prepared at a frame boundary, with no eviction wait or allocation in glyph submission. The default budget is 4 MiB; zero retains direct curve evaluation.

Native layout uses an 8192-entry bounded associative cache with full identity checks. A per-font cache retains integer font-unit glyph IDs, clusters, advances and offsets, so changing size repeats the original per-glyph arithmetic without reshaping. Font objects and tables are prepared at `load_font`. The original bitmap cache and its quantized-size history remain independent. Logical payload limits are 4 MiB per font and 32 MiB per context; table metadata and retained pool rounding are additional storage.

Shape vertices upload their original 20 bytes instead of expanding to 32-byte float colors. Integer RGBA8 attributes reconstruct exactly the original binary32 byte/255 values; UNORM conversion is not used. The original 32-byte route remains available with `compact_vertices=false`. Vertex bytes fall 37.5%; indices, native glyph instances and draw order are unchanged. Slots begin at 512 KiB; bounded high-water preparation leaves growth headroom before the next frame.

**Protocol and qualification.** The unchanged actual showcase harness renders 180 frames at 1360 x 860, Spiral mode, size 64, starting with 5000 items. Real Enter activations add items at frames 90, 120 and 150. Six alternating repeats compare before/default/coverage-off/coverage-off-plus-32-byte variants in direct and staged upload modes. All 8640 timing frames, cold frames and outliers are retained. Timing has no per-frame readback. GPU totals are divided by 180 logical frames, not the number of GPU submission intervals.

The old headers and static library were frozen before editing. A matching benchmark wrapper is compiled against those artifacts with `PUFFERUI_OPT3_BASELINE`; it reuses the original showcase test body unchanged. The final new wrapper is built in Release and Debug. Full suites completed before timing, and builds/tests did not overlap the qualified timing cohort.

Golden passes separately replay all 180 deterministic frames for three candidate variants, two upload modes and frame-cache off/on: **2160 exact full-RGBA comparisons**. Every timed frame ID, including every peak, belongs to this set. The 180 before-reference frames and all 77 original test/oracle files retain their hashes. No golden update mode was used.

**5000-item Spiral results.** Frame time below is the median of six run means. The paired change is the median of six before/after percentage changes matched by repeat; it differs from a ratio of two medians when runtime drift is present. GPU time is the median completed total/180.

| Upload | Wall before / after (ms) | Median paired wall change | GPU before / after (ms) | GPU change | Worst before / after (ms) |
|---|---:|---:|---:|---:|---:|
| DIRECT | 5.1872 / 4.7056 | -2.60% | 0.6944 / 0.5150 | -25.83% | 34.6824 / 38.7579 |
| STAGED | 5.1504 / 4.8647 | -7.46% | 1.2346 / 0.7995 | -35.24% | 40.6140 / 38.1527 |

The DIRECT default worst frame increases; the result is not a universal peak-time improvement. Runtime drift affects several late repeats. All individual paired deltas, p95/p99, cold distributions, peak frame IDs and raw CSV rows remain in the JSON/ZIP.

**Attribution controls and startup.** These controls are benchmark-only environment settings, not showcase UI or product CLI switches. The no-coverage variant retains the 20-byte stream and shaping caches. The legacy control additionally restores 32-byte float-color preparation.

| Upload / variant | Wall run-mean median (ms) | GPU (ms/frame) | First-frame median (ms) | Whole-process median (ms) | Maximum (ms) |
|---|---:|---:|---:|---:|---:|
| DIRECT / before | 5.1872 | 0.6944 | 15.2486 | 1115.00 | 34.6824 |
| DIRECT / after | 4.7056 | 0.5150 | 15.7373 | 1092.95 | 38.7579 |
| DIRECT / after-no-coverage | 4.3973 | 0.6371 | 15.1468 | 952.09 | 65.0302 |
| DIRECT / after-legacy-no-coverage | 4.9413 | 0.6900 | 15.2431 | 1063.87 | 43.2661 |
| STAGED / before | 5.1504 | 1.2346 | 15.8003 | 1095.34 | 40.6140 |
| STAGED / after | 4.8647 | 0.7995 | 16.5497 | 1052.10 | 38.1527 |
| STAGED / after-no-coverage | 4.7441 | 1.1175 | 16.5070 | 1035.40 | 37.3669 |
| STAGED / after-legacy-no-coverage | 5.7039 | 1.2056 | 16.1024 | 1204.69 | 39.0192 |

Coverage caching reduces GPU work but has CPU lookup/binding cost. On this CPU-heavy workload the coverage-off control can have a lower wall time. The complete default is retained with an explicit zero-budget diagnostic. Process time includes font/pipeline setup, but qualification has already exercised driver caches; it does not certify cold boot or uncached shader compilation.

| Upload / variant | Mean uploaded bytes/frame | Peak upload-page capacity (MiB) | Draw-buffer allocations in timed frames |
|---|---:|---:|---:|
| DIRECT / before | 19268380.2 | 65.50 | 0 |
| DIRECT / after | 14549232.9 | 65.50 | 0 |
| DIRECT / after-no-coverage | 14549045.9 | 65.50 | 0 |
| DIRECT / after-legacy-no-coverage | 19268380.2 | 65.50 | 0 |
| STAGED / before | 19268380.2 | 129.50 | 0 |
| STAGED / after | 14549232.9 | 129.50 | 0 |
| STAGED / after-no-coverage | 14549045.9 | 129.50 | 0 |
| STAGED / after-legacy-no-coverage | 19268380.2 | 129.50 | 0 |

The default totals 41957 coverage hits, 14816 misses, 526 fills and 39 fill draws per 180-frame run, with no region eviction in this Spiral workload. Total payload falls about 24.5%; reserved page capacity is unchanged because bounded headroom and allocation buckets remain. Page capacity is not VRAM usage. The dedicated R32 allocation is exactly 4194304 bytes on this GPU.

**CPU text workloads.** The probe compiles the actual implementation and uses no Vulkan device. Each value is the median total milliseconds across six alternating runs; call counts and every raw run are archived.

| Mode / workload | Before (ms) | After (ms) | Change | Actual hb_shape calls before / after |
|---|---:|---:|---:|---:|
| bitmap / cold_5000 | 4.26900 | 4.12350 | -3.41% | 0 / 0 |
| bitmap / warm_64 | 0.95680 | 0.96235 | +0.58% | 0 / 0 |
| bitmap / churn_5000 | 23.44520 | 23.64025 | +0.83% | 0 / 0 |
| bitmap / size_churn_64 | 0.77460 | 0.78575 | +1.44% | 0 / 0 |
| HarfBuzz / cold_5000 | 3.70630 | 5.37655 | +45.07% | 5000 / 5000 |
| HarfBuzz / warm_64 | 1.67820 | 1.80895 | +7.79% | 0 / 0 |
| HarfBuzz / churn_5000 | 15.21740 | 2.06240 | -86.45% | 35704 / 0 |
| HarfBuzz / size_churn_64 | 1.25010 | 0.75085 | -39.94% | 2368 / 0 |
| HarfBuzz / hb_shape_only_ascii | 9.44875 | 9.59455 | +1.54% | 50000 / 50000 |

The 5000-label repeated workload avoids 35704 reshapes; exact-size churn avoids 2368. First-seen 5000 native strings are slower because both cache representations must be populated; the small 64-label warm set also has additional identity/lookup cost. Those regressions are retained rather than hidden by a single aggregate. First-use payloads and outlines remain lazy; warmed cache hits do not shape text again.

**Independent null control.** No null speedup is claimed. Both PE `.text` sections are byte-identical: 187904 bytes, SHA256 `28ce665233672d5d9c0bbddd959d692bc0ea3f94f8296294110f856628f109e0`. The native/HarfBuzz changes are compiled out of this target. Measured timing differences, including the text-area outlier, are retained; allocation counts are unchanged.

| Null workload | Before ms/iteration | After ms/iteration | Change | Allocations/iteration before / after |
|---|---:|---:|---:|---:|
| layout_37000 | 4.52556125 | 4.6464525 | +2.67% | 27 / 27 |
| motion_still_5000 | 0.164611166 | 0.163775334 | -0.51% | 0 / 0 |
| motion_moving_5000 | 0.196227333 | 0.192755167 | -1.77% | 0 / 0 |
| sectors_5000 | 0.426542333 | 0.425207666 | -0.31% | 0 / 0 |
| state_live_5000 | 7.3e-05 | 7.8167e-05 | +7.08% | 0 / 0 |
| state_live_100000 | 7.45e-05 | 7.86665e-05 | +5.59% | 0 / 0 |
| empty_ids_after_200000 | 5e-07 | 4.5e-07 | -10.00% | 0 / 0 |
| text_area_gutter | 0.0193194445 | 0.023948611 | +23.96% | 0 / 0 |

**Matched GPU-resource sampling.** Three alternating repeats per upload mode run the same 180-frame workload through hidden native WSI with VSync off and a 60 Hz harness cap: 2160 paced frames. Windows PDH samples only the test PID. All engine/LUID counters and invalid samples are archived; the table shows the active discrete GPU. Means include process startup. This is separate supporting telemetry, not foreground FPS certification.

| Upload / counter | Before median run mean | After median run mean | Before sampled maximum | After sampled maximum |
|---|---:|---:|---:|---:|
| DIRECT / dedicated (MiB) | 173.2425 | 176.3194 | 184.0430 | 186.0430 |
| DIRECT / 3D (%) | 4.5874 | 3.3336 | 7.2586 | 5.3147 |
| DIRECT / shared (MiB) | 92.5730 | 92.5730 | 100.0977 | 100.0977 |
| STAGED / dedicated (MiB) | 232.7882 | 235.8651 | 248.5508 | 250.5508 |
| STAGED / 3D (%) | 7.8321 | 5.4455 | 11.9600 | 8.2306 |
| STAGED / shared (MiB) | 92.5730 | 92.5730 | 100.0977 | 100.0977 |

Sampled dedicated memory increases by about 3.08 MiB in median run means, and its sampled maximum increases by 2 MiB. This is the coverage-cache tradeoff; shared usage is unchanged. Actual VRAM peaks between samples are not established.

**Regressions and existing suites.** `pui_vulkan_native_optimization_tests` checks all 256 channel values against the independent float-color path, interpolated alpha/texture/clip output, exact upload-byte counts, 192 cache/uncached frames per upload mode, ligatures/Arabic/combining text, overlaps, DPI, gamma, blur ordering, oversized glyph fallback, pipelined GPU readback, timeline retirement, reclaim-under-pressure and integer-aligned hash distribution. CPU cases verify 5000-label reuse, size churn, exact scalar layouts and font identity.

Both `python tools/run_tests.py --vulkan --format --release` and `python tools/run_tests.py --vulkan --format` pass, including 29 registered native cases per configuration, all core/pump/performance/showcase suites, existing SDL goldens, every example, installed consumers, API/tutorial drift and formatting. The separate HarfBuzz-OFF/SDL-OFF build passes direct/staged interaction checks. The tooling suite has 12 passing tests. New untracked C++/INL sources also pass explicit clang-format dry-run checks.

The initial new aligned-position regression exposed a real retirement starvation bug. Its failed log is preserved; after the fix both complete suites pass. All 77 original files and the nine unrelated draft hashes remain unchanged. The SDL submodule is preserved.

**Live status.** The standard no-argument native executable was launched, but Windows was locked during capture. Desktop input stopped and an unlock was requested. No live appearance or foreground performance claim is made.

**Evidence and reproduction.**

- [Complete machine-readable receipt](perf/vulkan_native_optimizations_final.json)
- [Every timed frame](perf/vulkan_native_optimizations_frames.csv.gz)
- [Raw runs, golden logs, full-suite logs and protocol sources](perf/vulkan_native_optimizations_raw.zip)

The raw archive includes the exact compiler commands/probe sources. The before wrapper uses frozen `e18ed126` headers and `pufferui_vulkan.lib`; it does not use the new implementation. The immutable before capture directory remains local and every RGBA hash is committed in the receipt. On this checkout, rerun to a fresh output directory:

```powershell
cmake --build out/build/x64-release --target pui_vulkan_native_opt3_bench
python tools/compare_vulkan_native_optimizations.py `
  build-vulkan-native-opt3/before/optimization-bench-spiral.exe `
  out/build/x64-release/Release/pui_vulkan_native_opt3_bench.exe `
  --output build-vulkan-native-opt3/repeat-new-directory `
  --spiral-reference build-vulkan-native-opt3/spiral-slug-before-rgba `
  --baseline-receipt build-vulkan-native-opt3/baseline.json --runs 6
```

The standard native showcase remains directly runnable without Python:

```powershell
& "D:\myimgui\out\build\x64-release\Release\pui_showcase_vulkan.exe"
```
