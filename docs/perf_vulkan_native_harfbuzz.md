# Native Vulkan with HarfBuzz and Slug

This branch starts at the first native Vulkan renderer, `8c62fc4`, and retains
triangle rasterization and the original blur pyramid. HarfBuzz shaping and the
upstream Slug graphics text shader are the default in a HarfBuzz build. There
is no compute painter, shape SDF interpreter, GPU tile list, function cache or
duplicate curve evaluator. The previous compute implementation is preserved on
`main` at `b30a7be5b08d13b7e3c1c535fb3dbd0d615bea12`.

The measured result is mixed. Native Slug costs more GPU time than bitmap text,
especially on Overview. The upload changes substantially reduce the Spiral
allocation peak, and staged Spiral improves, but direct Spiral wall time and
some cold/percentile results regress. This is not a claim that every workload
became faster, nor a foreground FPS qualification.

## Implementation

- Keep the renderer-owned semantic painter interface and explicit backend mesh
  implementation. Core/SDL remain independent of HarfBuzz and Vulkan.
- Retain stable font-owned HarfBuzz objects, a reusable shaping buffer, shaped
  runs and outline caches. Recycle upstream encoder blobs after copying them.
- Draw Slug text with a separate graphics pipeline and 64-byte glyph instances.
  A prepared 2048-instance batch preserves submission order and flushes before
  meshes, clips, target changes, clears, readbacks and submissions. Six vertices
  per glyph are generated in the vertex shader.
- Upload encoded outlines once to append-only mapped chunks. Initial storage,
  descriptors and batch capacity are prepared at device creation. Ordinary
  chunks are 512 KiB; the bounded resident store is 16 MiB / 32 chunks, also
  limited by the configured upload budget and Vulkan storage-buffer limits.
  Rebinding a context invalidates its device-residency tags.
- Separate geometry and transfer pages, grow geometrically, reuse prepared
  geometry storage through ordered submissions, cache completed timeline values
  and redundant graphics bindings. Exact byte-to-float color conversion and
  original float vertices remain unchanged.
- Prepare each surface slot from the observed logical-frame byte high-water
  mark before drawing. Large hints remain bounded by visible pixels and budget.
  This avoids repeatedly filling the initial small arena in ordinary scenes.
- Pass the executable asset root explicitly, retain Windows font fallbacks and
  add Linux fallback paths. Linux execution was not tested on this Windows host.
- Preserve bitmap fixtures by selecting bitmap explicitly in the two original
  Vulkan interaction/Spiral fixtures. Their assertions and golden bytes are
  unchanged. No existing golden was regenerated.

The optional original CPU raster cache defaults off. A Slug frame flushes its
ordered mesh prefix and bypasses that cache before native text; it does not
silently cache or approximate outline coverage.

## Protocol and provenance

Windows x64, MSVC Release (`/O2 /Ob2 /GL /MD`, `/LTCG`, SegmentHeap), AMD Radeon
RX 9070 XT. Vulkan SDK 1.4.335.0; HarfBuzz 15.0.0 commit
`35f656a1a2bab548ea7f54a6dd3fba2b09680d14`. The normal loader selected the AMD
adapter with no `VK_DRIVER_FILES` override.

The original library and headers were frozen from `8c62fc4`. The shared native
probe was linked separately against that library and the final library using
matching compiler flags. Overview compiles the actual showcase helpers;
Spiral performs the actual three keyboard Add activations, 5000 to 5003 items,
at 1360x860. The original and final core probes compile the actual implementation.
All binary, source, shader and font hashes are in the machine-readable receipt.
Git preserves the shader input bytes explicitly, including line endings, so a
new Windows or Unix checkout does not invalidate the checked-in shader manifest.

Timing uses six alternating repetitions of 180 frames for each of original
bitmap, final bitmap and final Slug, separately in DIRECT and STAGED modes:
6480 Spiral frames and 6480 Overview frames. Timed loops perform no readback.
Cold frame zero and every outlier remain in the distributions. GPU time uses
the complete completed timestamp total divided by 180 logical frames, including
mid-frame submissions; dividing by timestamp sample count would hide that work.
Device/pipeline creation and process startup are excluded from frame timing.

No builds, tests, capture comparisons or live GUI inspection overlapped these
cohorts. The original diagnostic cohort is also retained, rather than discarded.
Different cohorts have different absolute timings and are not frequency-matched
comparisons. Use the alternating final cohort for the tables below.

## Final Spiral results

Values are milliseconds. Mean and p99 are means across the six repetitions;
maximum is the single largest frame across all six. GPU time is per logical
frame. Upload peak is the exact allocated upload capacity, not total VRAM.

| Upload / text | Wall mean | p99 | Maximum | GPU mean | Upload peak MiB |
|---|---:|---:|---:|---:|---:|
| Original DIRECT bitmap | 4.3465 | 22.2541 | 28.2935 | 0.5778 | 254.398 |
| Final DIRECT bitmap | 4.5743 | 23.9124 | 27.0841 | 0.5779 | 67.000 |
| Final DIRECT Slug | 4.4538 | 23.9234 | 27.5755 | 0.7435 | 65.500 |
| Original STAGED bitmap | 4.8649 | 32.8977 | 43.4262 | 0.7767 | 238.873 |
| Final STAGED bitmap | 4.6908 | 24.8067 | 30.4702 | 0.7665 | 131.000 |
| Final STAGED Slug | 4.5167 | 24.7972 | 29.7757 | 1.3240 | 129.500 |

The same-backend bitmap control isolates the upload changes from text. DIRECT
bitmap wall mean increases 5.24%, while GPU time stays essentially flat and
allocated capacity falls 73.66%. STAGED bitmap wall mean falls 3.58%. Slug is
slightly faster in wall mean than final bitmap in this particular geometry-heavy
scene, yet costs 28.64% more DIRECT GPU time and 72.71% more STAGED GPU time.
Those are overlapping CPU/GPU intervals, not numbers to add together.

Final DIRECT Slug remains 2.47% slower in wall mean than the original bitmap
renderer, so this work does not recover every part of the original throughput.
Final STAGED Slug wall mean is 7.16% lower than that original STAGED control.
Cold mean also increases: DIRECT 12.4221 -> 15.0012 / 13.9224 ms
(bitmap / Slug), STAGED 13.2868 -> 16.4604 / 15.3716 ms.

Peak frame indexes are 126-128 around the second Add, and every one is included
in the exact image comparison. A peak Slug frame has about 96 MB of geometry
upload work but only 313 glyph instances in 20 glyph draws. The upload arena
trades bounded residency for some additional ordered submissions: original
DIRECT has 180 timestamp intervals, final has 207. No draw-buffer allocation
occurs in the tested prepared Spiral drawing path.

## Final Overview and the arena regression

| Upload / text | Wall mean ms | GPU mean ms | Timestamp intervals / 180 frames |
|---|---:|---:|---:|
| Original DIRECT bitmap | 0.4695 | 0.4007 | 180 |
| Final DIRECT bitmap | 0.4764 | 0.4017 | 181 |
| Final DIRECT Slug | 0.8699 | 0.8194 | 181 |
| Original STAGED bitmap | 0.5807 | 0.5081 | 180 |
| Final STAGED bitmap | 0.5844 | 0.5062 | 181 |
| Final STAGED Slug | 1.3767 | 1.3308 | 181 |

The first candidate's fixed 1 MiB arena was too small for Overview's roughly
1.165 MB bitmap stream. It caused an extra submit/wait on almost every frame:
358 timestamp intervals and 1.5820 ms DIRECT wall mean versus that cohort's
0.8551 ms original control. Automatic high-water preparation fixes this defect;
the independent regression requires exactly one submission on each subsequent
large steady frame. The initial negative cohort is included in the archive.

After that fix, final bitmap Overview is close to the original. Enabling Slug
adds about 0.394 ms wall and 0.418 ms GPU in DIRECT. This text-heavy scene has
up to 585 glyph instances per frame. The remaining GPU cost is real even when
CPU shaping and outline uploads hit their caches.

## HarfBuzz CPU diagnosis and useful next optimizations

The null-device CPU probe wraps the actual `hb_shape` and `hb_gpu_draw_encode`
calls and counts misses. It uses short ASCII `#N` labels; it does not represent
every script or font. Means of six runs:

| Workload | Calls | Bitmap total ms | HarfBuzz total ms | HarfBuzz shape calls | Outline encodes |
|---|---:|---:|---:|---:|---:|
| Cold 5000 labels | 5000 | 4.2420 | 3.6983 | 5000 | 11 |
| Warm 64 labels | 64000 | 0.9672 | 1.6893 | 0 | 0 |
| Churn 5000 labels | 50000 | 24.2074 | 15.1498 | 35704 | 0 |
| Size churn, 64 labels | 6400 | 0.7803 | 1.3683 | 2368 | 0 |

Pure `hb_shape` on this ASCII workload is 10.267 ms for 50000 calls, about
0.205 microseconds/call. Warm layout lookups have overhead, but they do not
repeatedly invoke HarfBuzz. The pinned upstream `hb_shape` already calls
`hb_shape_plan_create_cached2`; adding another generic shape-plan cache is not
the identified missing optimization. HarfBuzz returns glyph IDs and positions;
the renderer determines the drawing cost. See the upstream
[shaping documentation](https://harfbuzz.github.io/shaping-and-shape-plans.html)
and [plan caching documentation](https://harfbuzz.github.io/shaping-plans-and-caching.html).

The next candidates are not implemented in this revision:

1. Cache the exact coverage produced by native Slug for each glyph, using
   float precision and exact `texelFetch`, then retain the original glyph order
   and blending. Key font generation, glyph, size, DPI, exact subpixel phase and
   the relevant gamma state. Budget/churn/oversized misses must draw native Slug
   directly, without evaluating curves twice. R8 atlases or approximate MSDF/SDF
   are not pixel-preserving substitutes. Whole-run flattening also needs proof
   for overlap and intermediate RGBA8 quantization.
2. Improve the vector shaped-run cache's 4096 direct-mapped slots plus 64 victim
   slots. The 5000-label churn count proves collisions. A better set-associative
   policy and font-unit reuse across sizes can reduce reshaping without changing
   bitmap quantization or measured text metrics. Validate fonts, variation state,
   scale, clusters, offsets and fallback identity before reusing a run.
3. Independently reduce native vertex upload representation. The current
   32-byte float vertex dominates peak Spiral payload. A packed 20-byte
   representation could reduce vertex bytes 37.5%, but exact color conversion,
   float arithmetic, index order and all-frame equality must be proven first.

## Core/null control

The core probe links neither Vulkan nor HarfBuzz. Final mean ms/iteration:

| Workload | Original | Final |
|---|---:|---:|
| layout 37000 | 4.615784 | 4.621120 |
| motion still 5000 | 0.170436 | 0.164184 |
| motion moving 5000 | 0.198683 | 0.193807 |
| sectors 5000 | 0.426906 | 0.427420 |
| state live 5000 | 0.0000896 | 0.0000743 |
| state live 100000 | 0.0000823 | 0.0000743 |
| text-area gutter | 0.018353 | 0.018695 |

Empty-ID timing is near clock resolution (about 0.0000004 ms/iteration) and is
not used for a speedup claim. Allocations remain 27/iteration for layout and
zero for the other measured workloads. Gutter is 1.86% slower, about 0.34 us;
the initial noisy cohort had a much larger 0.02196 -> 0.05072 ms increase.
Both cohorts and all outliers are retained. No HarfBuzz explanation applies to
that null control, and its cause is not established by these measurements.

## GPU utilization and memory

Per-PID Windows PDH counters retain all engine/LUID identities and invalid
samples. Three alternating repetitions per case, DIRECT/STAGED, use hidden
native WSI with VSync off and a fixed 60 Hz harness cap: 6480 paced frames.
These percentages are matched-workload samples, not unlimited FPS comparisons.

| DIRECT scenario | Original bitmap 3D | Final bitmap 3D | Final Slug 3D |
|---|---:|---:|---:|
| Spiral | 3.5480% | 3.5841% | 4.5239% |
| Overview | 3.0871% | 3.0465% | 5.5454% |

DIRECT Spiral sampled shared-memory maximum falls from 289.000 MiB to
101.598 MiB bitmap / 100.098 MiB Slug. Sampled dedicated-memory maximum is
186.551 -> 188.043 / 184.043 MiB; no universal dedicated-VRAM reduction is
claimed. STAGED dedicated maximum is 304.551 -> 252.551 / 248.551 MiB.
Sampling can miss true peaks. These counters include images and driver storage;
they are a separate measure from exact upload arena allocation capacities.

## Exact image and regression verification

- All 180 bitmap Spiral frames compare against immutable first-native output.
  All 180 native triangle/Slug frames compare against the separate immutable
  pre-optimization native-text capture. DIRECT/STAGED and cache off/on yield
  1440 complete byte comparisons, covering every timed peak and its neighbors.
- Final bitmap Overview compares all 180 frames in both upload modes, 360
  complete comparisons. Raw RGBA captures remain local in
  `build-vulkan-native/*-rgba`; per-frame hashes are committed in the receipt.
- A text-only domain compares 96 frames per upload mode with frozen previous
  native Slug output. It does not confuse shape SDF and triangle raster output.
  Instanced/scalar glyph replay also compares 96 frames per upload mode.
- New tests cover ligatures/Arabic shaping, font-table relocation and fallback,
  context/device rebinding, clipped/overlapping text, append-only residency,
  native ordering, tiny upload pages and dense/sparse mesh splitting, automatic
  arena growth, effective defaults, font-root/CWD identity and validation flags.
- Full Release and Debug `tools/run_tests.py --vulkan --format` suites pass:
  core, pump, performance, independent old pixel fingerprints and goldens,
  showcase peaks, native/cache/external/fault/WSI, every example, installed
  consumers, API/tutorial drift and formatting. The final Vulkan-only arena fix
  was followed by all 27 registered native cases plus three native examples in
  each configuration (30 per configuration). Core/SDL code did not change after
  their full gates. HarfBuzz OFF with SDL OFF also builds and passes native tests.
- Final read-only format, tool, API, tutorial and whitespace checks pass.
  The SDL submodule is clean. All nine unrelated drafts retain their hashes.
  The Vulkan README pictures and explicit tutorial picture were regenerated
  from Release executables and visually inspected.

The standard executable launched without arguments and exposed its `showcase`
window. Foreground inspection is pending: activation failed and a refreshed
capture showed the Windows lock screen. No further app input was issued after
that observation; the user has been asked to unlock. Automated rendering
results do not substitute for a successful foreground inspection. The final
receipt records this boundary explicitly.

## Evidence and use

- [Complete receipt](perf/vulkan_native_harfbuzz_final.json): final measurements,
  source/binary/reference hashes, all maxima, CPU counts, PDH counters and limits.
- [All-frame CSV](perf/vulkan_native_harfbuzz_frames.csv.gz): initial and final
  Spiral/Overview timing rows, without deleting outliers.
- [Raw logs and protocols](perf/vulkan_native_harfbuzz_raw.zip): initial/final
  runs, GPU samples, original command lines, build/probe scripts and full gates.
- [Native text contract](harfbuzz.md) and
  [explicit ownership tutorial](tutorial/10-vulkan-without-shorthand.md).

Run the standard target without Python or painter flags:

```powershell
& "D:\myimgui\out\build\x64-release\Release\pui_showcase_vulkan.exe"
```

HarfBuzz/Slug is effective when built with `PUFFERUI_WITH_HARFBUZZ=ON` (the
fresh Vulkan default). `vulkan_device_opts::vector_text=false` selects the
bitmap diagnostic. This branch has no compute painter command-line mode.
