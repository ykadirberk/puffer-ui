# Checked native placement reuse and independent integer reductions

Baseline: `a0dbe72c5e642540bcd758c69360d05e449ead9a`. Branch:
`codex/vulkan-native-harfbuzz`. Qualification uses Windows x64 Release/MSVC,
Radeon RX 9070 XT, vendor/device 4098/30032, driver 8389024.

## Changes and visual contract

Native path drawing previously called readiness, which computed placement and
looked up the prepared source, then repeated both operations before drawing.
A private checked-placement helper now serves readiness and submission. Drawing
uses its returned placement and asset once. Public readiness remains callable
independently. Empty/clipped no-ops, error ordering, prepared extents, DPI,
suspension and native clip restoration retain their existing behavior. No
placement persists across frames, different clips or transforms.

The native 192-index block planner previously reduced every SIMD load through
one running signed minimum/maximum pair. Four independent pairs now break that
serial compare/select dependency and merge once per block. The signed seeds,
48 loads per block, original scalar boundary refinement, sparse-first-triangle
fallback and greedy end/low/high tuples remain exact. The existing guaranteed-
SSE2 selection and scalar fallback remain; there are no new architecture or
floating-point flags, heap scratch, upload arenas or public API requirements.

Shape/text/path shaders, Slug operands, colors, geometry arithmetic, primitive
order, draw boundaries, logical uploads, storage budgets and blur are unchanged.
The rejected page-tail packing experiment is not included in this revision.

## Permanent regressions

The independent `pui_vulkan_vector_cpu_tests` target runs in DIRECT and STAGED.
It pins 5000 placement calls for 5000 visible native paths, zero counted C++ draw
allocations, zero Vulkan draw-buffer allocations and the existing one-draw upload.
It also checks standalone readiness, malformed/unprepared/empty/clipped inputs,
error priority while suspended, reflection, singular transforms, clip changes,
2x DPI and release/reuse behavior.

Another 442368 merge-boundary cases per run place extrema in every SIMD lane
across three blocks and compare all tuples against an independent scalar oracle.
Unaligned starts, 192-index boundaries, scalar tails, exact capacity fits,
signed extrema, both vertex strides and allocation/work bounds are covered.
The existing integer, indexed, path and all other test/oracle files stay fixed.

The golden mode imports the actual unchanged `bench_vector.inl` workload instead
of copying its frame logic. Only this independent target wraps post-frame stats
queries to read pixels. Timing continues to use the original executable and
performs no readback. Baseline headers and executable were frozen before either
fix; all 180 complete 512x512 simple frames were captured in both upload modes.
The two sets are byte-identical. A deduplicated immutable archive preserves the
180 frames, both hash maps and the baseline executable identity.

## Measurement protocol and results

Eight alternating before/after pairs cover 5000 simple paths, composed SVG
controls, the actual 5000-to-5003-item Spiral in DIRECT/STAGED with cache off/on,
and null. Every fixed-time 180-frame sequence retains frame 0 and all outliers;
Spiral uses the real keyboard Adds at 90/120/150. Setup and GPU totals remain
separate. GPU totals are divided by 180 logical frames, not command submissions.
CPU frame wall time includes waits and overlaps GPU execution.

The main series contains 144 runs / **25920 timed frames**. Means and
quantiles below pool all eight runs per revision; paired changes are medians
of the eight matched run-mean changes. No control normalization is applied.

| Workload | Frame wall mean before / after (ms) | Paired change | Improving pairs | GPU mean before / after (ms) |
| --- | ---: | ---: | ---: | ---: |
| null | 3.191039 / 3.228430 | +1.20% | 3/8 | n/a |
| spiral-direct-cache | 3.927850 / 3.885463 | -1.65% | 6/8 | 0.632523 / 0.634624 |
| spiral-direct-plain | 3.981825 / 3.947338 | -0.84% | 6/8 | 0.637531 / 0.639818 |
| spiral-staged-cache | 3.781259 / 3.735051 | -1.10% | 7/8 | 0.792447 / 0.792353 |
| spiral-staged-plain | 3.786591 / 3.747253 | -1.23% | 6/8 | 0.803653 / 0.813721 |
| vector-direct-complex | 0.966398 / 1.019695 | +4.69% | 3/8 | 0.934000 / 0.989218 |
| vector-direct-simple | 0.725873 / 0.522645 | -28.41% | 8/8 | 0.269166 / 0.269983 |
| vector-staged-complex | 0.938185 / 0.968644 | +0.41% | 2/8 | 0.902767 / 0.932129 |
| vector-staged-simple | 0.764941 / 0.547446 | -27.83% | 8/8 | 0.274463 / 0.273782 |

| Workload | p95 before / after (ms) | p99 before / after (ms) | Absolute maximum before / after (ms) | Frame 0 median before / after (ms) |
| --- | ---: | ---: | ---: | ---: |
| null | 6.2670 / 6.4591 | 11.8629 / 11.8163 | 15.8378 / 18.0515 | 15.0508 / 14.8646 |
| spiral-direct-cache | 6.9603 / 7.1760 | 16.5441 / 15.4758 | 18.2705 / 18.4240 | 6.6629 / 6.8404 |
| spiral-direct-plain | 7.1438 / 7.1091 | 16.8426 / 15.4932 | 18.5179 / 17.2778 | 6.8306 / 6.8519 |
| spiral-staged-cache | 7.5210 / 7.4063 | 17.3990 / 15.6381 | 20.4462 / 17.5638 | 7.2558 / 7.0606 |
| spiral-staged-plain | 7.6165 / 7.4988 | 17.5774 / 16.2582 | 20.5108 / 19.4373 | 7.5275 / 7.1907 |
| vector-direct-complex | 1.1936 / 1.2678 | 2.7927 / 3.0333 | 4.5756 / 4.5761 | 0.5794 / 0.5921 |
| vector-direct-simple | 0.8749 / 0.6524 | 2.0593 / 1.5607 | 3.5941 / 4.4270 | 0.9300 / 0.7766 |
| vector-staged-complex | 1.0689 / 1.1723 | 3.0490 / 3.0251 | 4.0058 / 4.6444 | 0.8439 / 0.7528 |
| vector-staged-simple | 0.9826 / 0.6474 | 2.4257 / 2.6660 | 4.1008 / 3.9913 | 1.0124 / 0.8327 |

| Spiral mode / frame | Matched-index median wall before / after (ms) | Median slot wait before / after (ms) |
| --- | ---: | ---: |
| spiral-direct-cache / 127 | 17.2257 / 15.9462 | 0.0190 / 0.0150 |
| spiral-direct-cache / 128 | 16.9677 / 16.2103 | 0.3912 / 0.4022 |
| spiral-direct-plain / 127 | 17.3850 / 16.2744 | 0.0176 / 0.0419 |
| spiral-direct-plain / 128 | 17.2137 / 16.2646 | 0.4186 / 0.4220 |
| spiral-staged-cache / 127 | 17.8841 / 16.5497 | 0.0289 / 0.0175 |
| spiral-staged-cache / 128 | 18.2637 / 16.4237 | 0.7493 / 0.7793 |
| spiral-staged-plain / 127 | 18.5129 / 16.4484 | 0.0407 / 0.0226 |
| spiral-staged-plain / 128 | 18.7498 / 17.3849 | 0.6936 / 0.7225 |

Simple-path run means improve in all eight pairs per upload mode. The main
STAGED/cache worst decreases 20.4462 -> 17.5638 ms, but native peaks are not
eliminated. DIRECT/cache and simple DIRECT absolute maxima increase. The
composed SVG controls also retain higher frame/GPU means. This revision is
not qualified as a universal GPU, startup or worst-frame improvement.

Every frame retains identical draw counts, logical upload bytes, native
geometry/index counts, submission counts and reservations where those counters
apply. Simple/composed vector draw allocations remain zero. Spiral upload
reservation stays 68681728 bytes DIRECT / 135790592 bytes STAGED; source/layer
image residency and instance budgets are unchanged.

### Matched SSE2 component probe and correction

The earlier exploratory review reported about 74% for an extracted planner.
That reference was included after the implementation header had undefined
`PUI_VK_INTEGER_SIMD`, silently selecting its scalar body. That percentage
was not an old-SSE2/new-SSE2 comparison and is not used in this qualification.
The actual frozen-executable frame timings and golden comparisons were
unaffected. Existing report drafts are preserved; this correction records
the qualification boundary without changing their historical bytes.

The corrected probe explicitly restores and asserts the guaranteed SSE2 gate
around the frozen original function. Both paths use the same compilation
target/flags and input: 8290686 indices captured from the actual largest
frame-127 Spiral draw. All three split tuples and all checksums match. The
probe creates no GPU work. Eight alternating groups of 24 complete scans per
version retain 384 samples, including the first scan.

| Matched complete index scan | Before SSE2 | After SSE2 |
| --- | ---: | ---: |
| Mean (ms) | 2.416136 | 1.180203 |
| Median (ms) | 2.398050 | 1.169950 |
| Maximum (ms) | 3.365300 | 1.669900 |

The corrected paired median reduction is **51.06%**, not 74%.
This component improvement does not imply the same reduction in total frame
time: geometry production, validation, copies, GPU work and waits remain.

### Same-executable controls

Four additional alternating pairs per case compare exact copies of the
current executables. These 32 runs / 5760 frames are retained separately
from the before/after cohort.

| Case | A / B pooled frame mean (ms) | A-B paired change | A / B maximum (ms) | A / B GPU mean (ms) |
| --- | ---: | ---: | ---: | ---: |
| null | 2.932054 / 2.907224 | -1.17% | 10.9216 / 11.0114 | n/a |
| spiral-staged-cache | 3.674053 / 3.660524 | -0.24% | 19.7807 / 17.2873 | 0.746660 / 0.743871 |
| vector-direct-complex | 0.847393 / 0.863864 | +0.27% | 3.8319 / 3.8799 | 0.812985 / 0.831077 |
| vector-staged-complex | 0.853438 / 0.932159 | +0.40% | 3.4670 / 4.0280 | 0.819961 / 0.899211 |

Identical copies can have materially different pooled means and peaks. The
additional cached Spiral control reaches 19.7807 ms despite the 17.5638 ms
main-cohort maximum; no permanent upper bound follows from either cohort.
The null before/after executables also have identical `.text`, `.data` and
`.pdata` sections, with three changed bytes in `.rdata`. This demonstrates
variation without a changed null algorithm, but does not establish a complete
cause for every native/GPU difference. Higher composed-control means remain
reported rather than normalized away.

All **31680 timed application frames** and the 384 corrected component samples
are retained. No foreground FPS or Task Manager utilization claim follows
from these unpaced offscreen measurements.

## Validation and evidence

Full unchanged Release and Debug `tools/run_tests.py --vulkan --format
[--release]` gates pass: 57 native cases each, all existing suites/goldens,
every example, installed core/SDL/Vulkan consumers, API/tutorial checks and
clang-format 22.1.3. All 25 portable Python tools tests pass, including the three
new fixture/completeness/readback/allocation guards. New C++ files also pass an
explicit formatting check.

- Final qualification compares **3600 complete images exactly**: 2160 original
  Spiral frames (before/after, both uploads, cache off/on and prepared/late),
  720 composed SVG frames and 720 simple-path frames. Every 180-frame sequence
  includes frame 0 and all timed peak indices. Timing runs have no readbacks;
  correctness uses the identical fixed-time workload and unchanged counters.
- Initial pre-fix simple captures add 360 full frames across DIRECT/STAGED.
  Both sets are identical; the immutable archive retains one 180-frame set and
  both hash maps. No previous fixture, assertion or tolerance was changed.
- Core/SDL/Vulkan/HarfBuzz-OFF boundaries, existing indexed STAGED tests and the
  explicit vector tutorial pass. An actual amalgamated native/HarfBuzz/STAGED
  consumer renders overlapping isolated SVGs, checks resulting color/alpha and
  reports zero validation errors. Both frozen/current fixed vector fixtures pass;
  both capture executables refuse an existing reference directory.
- Actual `pui_showcase_vulkan` checks run 1080 application frames across before/
  after SVG, 640x480 SVG and 5000-item Spiral cases. All three full BMP pairs are
  byte-identical. Candidate framebuffers were inspected for curve/stroke/gradient,
  clipping, text and layout quality. This is application-framebuffer/scripted
  evidence, not a live interaction or foreground-pacing certification.
- The protected audit preserves all 128 original test/oracle files, all 36
  initial report drafts, 360 old Spiral hashes, both new simple capture sets and
  all 242 measured source hashes. The pinned SDL submodule is clean.
- Build/check/timing phases are sequential. Preliminary harness compilation
  retries are retained separately and are not counted as completed gates.

[Machine-readable results, hashes and per-run peaks](perf/vulkan_vector_cpu.json),
[every retained application frame and corrected CPU scan](perf/vulkan_vector_cpu_frames.csv.gz),
[raw logs, commands, component input and application captures](perf/vulkan_vector_cpu_raw.zip),
and [bundle hashes](perf/vulkan_vector_cpu_bundle.json).

Reproduce on the qualified adapter with frozen baseline executables and the
immutable Spiral capture parent:

```powershell
python tools/run_tests.py --vulkan --format --release
python tools/run_tests.py --vulkan --format
python tools/compare_vulkan_vector_cpu.py `
  --before BASELINE_BIN_DIRECTORY --after out/build/x64-release/Release `
  --spiral-references IMMUTABLE_SPIRAL_PARENT `
  --simple-references tests/golden/vector_cpu/frames.zip `
  --vector-references tests/golden/vector_batch/frames.zip `
  --output NEW_RESULT_DIRECTORY --pairs 8
```

Fixed native fixture bytes qualify their recorded adapter/driver. They are never
regenerated to accept a backend difference. Ordinary same-device regression
comparisons and existing independent golden oracles remain separate.
