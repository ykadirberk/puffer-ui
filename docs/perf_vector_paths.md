# Immutable SVG/path resources and native Slug painting

This feature adds reusable SVG and C++ path sources to the required renderer
contract. Vulkan/HarfBuzz draws them with font-independent upstream Slug
coverage; mesh/SDL/null renderers prepare retained RGBA images. The UI submits
semantic operations. Parsing, encoding, rasterization, resource allocation and
pipeline creation happen during explicit preparation outside frames.

Existing rectangle, polygon, text and blur rendering retains its appearance.
The original four-stage shader header, its manifest and the shared licensed
Slug include are byte-identical to base
`06d1d669cb3af188291ffc76ff68296115e7c5ae`.

The implementation is **not a general performance win over prepared textures**.
Native analytic coverage preserves transformed curve detail, but isolated SVG
groups have a substantial GPU cost in this graphics implementation. The native
SVG/path default follows the requested Slug representation; fixed-size icon
applications can select `vulkan_device_opts::vector_paths=false`. This choice
does not select the text renderer or change existing primitive shaders.

## Implementation and preparation

- Immutable, explicitly retained `vector_handle` sources copy their input
  commands, gradients, dashes and SVG scene data. Public inspection views allow
  independent renderers to consume the full source without private headers.
- All painters implement prepare/release/readiness/capacity/draw operations.
  The UI performs no tessellation or implicit preparation. Missing preparation
  returns a structured status; empty, transparent and clipped draws are no-ops.
- The bounded icon/logo importer supports relative/absolute path commands,
  curves/arcs, primitives, transforms/viewports, local use/symbol references,
  gradients, strokes/dashes, clipping, currentColor and isolated opacity.
  Unsupported text, images, filters, masks, scripts and full CSS fail explicitly.
- Portable preparation retains a filtered texture and bounded batch storage.
  Native preparation builds font-free `hb_gpu_draw` data, independent path
  shaders, metadata buffers, instances and shared bounded layer targets.
  Buffers/descriptors retire after the last recorded submission serial.
- Both caches are bounded to 64 MiB/256 entries with per-resource budgets and
  work/precision limits. A packed Slug outline can also reach its 16-bit texel
  address limit; null encoder output is handled as `LIMIT`, without a crash.
- Complex stroke/fill/clip regions recover true contours after normalization,
  avoiding interior trapezoid edges in the GPU representation. Mixed mesh/vector
  rendering restores clip state. Overlay recording preserves its clip/opacity
  and retains sources. Context rebinding releases only its own source lease.

The [contract and limits](vector_paths.md) and the
[complete tutorial without example shorthand](tutorial/11-svg-and-paths.md)
describe ownership and application integration. No compute painter or GPU tile
binning is introduced by this feature.

## Protocol

Windows x64 Release, MSVC, AMD Radeon RX 9070 XT, vendor 4098/device 30032,
driver 8388996. Before binaries were frozen and hashed at the base revision.
Candidate binaries were frozen separately. Main production implementation and
example source hashes did not change after the timed series.

The old workload uses the unchanged actual showcase harness: 180 frames,
5000 to 5003 Spiral items, three keyboard Add activations. Eight alternating
before/after pairs cover DIRECT/STAGED, cache off/on, plus a null-renderer
control. There are no discarded warmup, cold or outlier frames. Setup and frame 0
are separate; timed runs perform no image readbacks.

Six alternating native/portable pairs per upload mode cover 5000 simple cubic
fills and 96 SVGs containing a gradient, clip and isolated opacity. Rotation
changes each frame and currentColor changes every 30 frames. These compare two
representations of **new** content, not historical before/after pixels.

The main series contains 128 runs / 23,040 timed frames. An additional 32
same-binary A/A runs retain 5760 frames. Eight separate instrumentation runs
retain 1440 frames and expose total versus device-local upload allocations.
There are **30,240 retained timed frames** in the combined evidence.
These are offscreen, unpaced CPU frame times and completed GPU timestamp totals;
they do not certify foreground FPS or Task Manager utilization percentages.

## Existing 5000-item Spiral and null control

CPU values below pool every frame of every repetition, in milliseconds.

| Workload | Mean before → after | Change | p95 before → after | p99 before → after | Maximum before → after |
| --- | --- | --- | --- | --- | --- |
| DIRECT | 4.211488 → 4.055136 | -3.71% | 9.6944 → 9.0330 | 16.9553 → 16.8713 | 22.2402 → 24.4390 |
| DIRECT cache | 3.871825 → 3.843947 | -0.72% | 7.2486 → 6.5136 | 16.8876 → 16.9309 | 23.0096 → 21.6457 |
| STAGED | 4.076950 → 4.186441 | +2.69% | 8.6612 → 9.6235 | 17.5104 → 18.0663 | 26.0935 → 23.5570 |
| STAGED cache | 4.406145 → 4.475055 | +1.56% | 7.9077 → 8.8441 | 19.8269 → 19.9408 | 26.8055 → 23.4643 |
| Null | 3.434759 → 3.410704 | -0.70% | 6.4437 → 6.3222 | 11.8842 → 11.5791 | 15.7210 → 14.4250 |

| Mode | Frame 0 median before → after | Setup median before → after | GPU mean/frame before → after |
| --- | --- | --- | --- |
| DIRECT | 5.1651 → 5.1136 | 122.0341 → 118.2814 | 0.793870 → 0.788485 |
| DIRECT cache | 5.1720 → 5.2760 | 118.6079 → 120.5007 | 0.648822 → 0.650640 |
| STAGED | 5.5686 → 5.2493 | 129.2705 → 122.9921 | 1.051039 → 1.025008 |
| STAGED cache | 5.3764 → 5.9423 | 138.4589 → 144.0931 | 0.937440 → 0.991680 |

The STAGED mean increases remain observed costs in this qualification. A/A
mean CPU differences were -0.154% (plain) and +0.535% (cache); GPU differences
were -0.279% and +0.127%. These controls do **not** explain away all of the
before/after STAGED difference. There is no universal CPU improvement claim.
The DIRECT absolute worst also increased despite a lower mean. The complete
receipt retains the run/frame for every peak, rather than selecting favorable
maxima. No causal GPU improvement/regression claim follows from these timestamp
differences alone.

Per-frame vertices, indices, draw/upload counts and glyph upload bytes match
before/after. Reserved upload memory remains 68,681,728 bytes DIRECT and
135,790,592 bytes STAGED. Existing scenes create no SVG/path assets or targets.

## New SVG/path representation tradeoff

| Upload / scene | CPU mean portable → native (ms) | GPU mean portable → native (ms) |
| --- | --- | --- |
| DIRECT / 5000 cubic fills | 0.980679 → 1.249907 | 0.021486 → 0.184443 |
| STAGED / 5000 cubic fills | 1.128548 → 1.197925 | 0.049715 → 0.183334 |
| DIRECT / 96 composed SVGs | 0.258832 → 2.287188 | 0.006464 → 2.282694 |
| STAGED / 96 composed SVGs | 0.284186 → 4.019518 | 0.007026 → 4.029121 |

The composed native scene issues 480 graphics draws per frame, versus one
prepared-texture draw. Its isolated groups require target changes, composition
and, in STAGED, additional upload/copy coordination. This is a concrete reason
to retain the texture configuration for large numbers of fixed-size composed
icons. Analytic Slug coverage is not automatically cheaper than sampling a
prepared texture. The source parses/encodes only during setup: this GPU cost is
not HarfBuzz text shaping repeated each frame.

Every measured SVG draw/flush sequence has zero counted C++ allocations and
zero draw-time upload-buffer allocations. The independent counter selftest
covers scalar, array, aligned and nothrow new/delete forms. C allocation hooks
are not claimed. GPU timestamp totals include the final drain; CPU frame times
end after submission, so their averages need not be identical when GPU-bound.

Allocation counters below are bytes, not a whole-driver VRAM measurement.
Total upload includes mapped and device-local allocations; device-local is a
subset, not another amount to add to that total. Images are listed separately.
The instrumentation supplement's historical `mapped_upload_bytes` column was
the total counter; the committed benchmark labels it `total_upload_bytes`.

| Scene / representation | Total upload DIRECT | Total upload STAGED | Device-local STAGED | Images |
| --- | --- | --- | --- | --- |
| 5000 fills / texture | 3,145,728 | 5,242,880 | 2,097,152 | 1,081,600 |
| 5000 fills / Slug | 5,245,688 | 9,439,992 | 4,194,304 | 1,048,832 |
| 96 composed / texture | 3,145,728 | 5,242,880 | 2,097,152 | 1,081,600 |
| 96 composed / Slug | 3,158,408 | 5,255,560 | 2,097,152 | 1,253,632 |

Renderer budget accounting is 34,192 / 643,784 bytes for simple texture/Slug
and 39,760 / 233,728 bytes for composed texture/Slug. This mixes retained CPU
data/batch storage with representation residency and is not additive to the
device counters. Setup medians, cold frames, percentiles and every sample are in
the machine-readable receipt.

## Visual and regression qualification

- Full Debug and Release `tools/run_tests.py --vulkan --format [--release]`
  passed, including 53 native registry cases, consumers, examples, original
  core/pump/performance suites and unchanged golden checks. Later changes only
  add a packaging fix, new-gallery clipping and test/telemetry checks; each has
  focused final verification. No shared rendering code changed after timing.
- Portable private suite: 23 tests. HB/native private suite: 28 tests. Native
  pipeline: 12 images; actual painter: 84 mode images; overlay: 16 images;
  affine/fit/suspended/recreated targets: 24 images. Native lifecycle includes
  shared contexts, renderer rebinding, exact retirement and 10,000 repeated
  allocation-free instances across upload modes. Buffer/map/descriptor fault
  cases preserve bindings and release partial resources without clearing sticky
  device errors.
- The 21 new immutable 64x64 RGBA fixtures cover seven scenes at 1x/2x/4x DPI.
  DIRECT/STAGED and cache off/on match exactly; text gamma .75/1/2.2 does not
  change path coverage. Fixtures qualify this adapter/driver, not arbitrary
  cross-driver raster equality. Existing fixtures were never regenerated.
- Frozen old Spiral captures match **all 180 frames** for both upload modes,
  cache off/on, prepared/late routes and before/candidate binaries: 2160 exact
  full-image comparisons, including frame 0 and every measured peak frame.
  All 360 original RGBA reference hashes remain unchanged.
- SDL/Vulkan/HarfBuzz OFF core and vector suites pass. Vulkan/HarfBuzz OFF draws
  the explicit tutorial using the complete portable implementation. An actual
  standalone amalgamated-header consumer parses and draws SVG with no SDK/SDL.
  Nested private slice expansion has its own packaging regression.
- Final actual `pui_showcase_vulkan` runs cover 180 SVG frames, resized portable
  SVG frames and 180 5000-item Spiral frames. A new pixel isolation regression
  executes this actual binary at 520/640/960 widths. Its pre-fix capture fails
  at (216,207); fixed card gaps stay background. Final Python tools: 19 pass.
  All 65 tutorial excerpts match their sources. Release pictures were regenerated
  and reviewed; native Slug gallery, standard Vulkan showcase and SDL demo were
  also inspected in live windows.
- Some desktop input interrupted CUA actions. The optional separate visible
  pacing helper returned zero with no required output, so it is inconclusive
  and excluded from pacing certification. Standard showcase live rendering and
  native startup/VSync state tests are separate evidence.
- Protected audit: 88 historical test/oracle files unchanged. The old semantic
  test double has exactly five required-interface stub additions, zero removed
  lines or changed assertions. All 32 unrelated report drafts and the pinned
  SDL submodule remain untouched.

## Evidence and reproduction

[Summary/source receipts](perf/vector_paths.json),
[all frame records](perf/vector_paths_frames.csv.gz),
[raw runs, commands, logs and validation bundle](perf/vector_paths_raw.zip),
[bundle hashes](perf/vector_paths_bundle.json).
The raw bundle includes rejected investigations and the immutable reference hash
receipt; it does not duplicate the multi-gigabyte historical RGBA corpus.

```powershell
python tools/run_tests.py --vulkan --format
python tools/run_tests.py --vulkan --format --release
& "out/build/x64-release/Release/pui_vulkan_vector_data_tests.exe" --goldens tests/golden/vector
& "out/build/x64-release/Release/pui_vulkan_vector_data_tests.exe" --bench --complex
& "out/build/x64-release/Release/pui_vulkan_vector_data_tests.exe" --bench --complex --portable --staged
& "out/build/x64-release/Release/pui_showcase_vulkan.exe" --vectors
```

Source and binary hashes, per-run setup/cold/peak records and the exact command
protocol are retained. The resource limits and representation tradeoffs remain
part of the public contract; this importer does not claim full browser SVG
conformance.
