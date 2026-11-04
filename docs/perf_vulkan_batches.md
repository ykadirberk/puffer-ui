# Exact Slug scene batching and consolidated STAGED uploads

This revision optimizes the 480-draw composed SVG workload and the STAGED
5000-item Spiral. It preserves the original Slug operands, shaders, native
pixels, shape/text geometry, upload budgets and ordered composition. The
baseline is `48db13f08a2b897f37ffe381b31391c6cfd4721b`.

## What changed

The old composed scene drew five operations separately for each of 96 instances.
Two isolated layers forced repeated target switches. STAGED additionally ended
rendering and copied each small instance/mesh/glyph upload with two buffer
barriers, including a whole-buffer input-read dependency.

Already isolated complete sources can now occupy up to 16 independent prepared
layer tiles. Each scene step draws the tiles together. Integer translations keep
absolute pixel centers and every inverse-transform/paint/clip/Slug operand exact.
The final instanced source-over composites retain application order, including
overlapping destinations. The measured scene issues **30 draws rather than
480**. Opaque, non-isolated sources retain their original immediate path.
Resource/clip/target transitions and a full batch flush pending work. Preparation
reduces tile count to honor limits and actual image allocation alignment.

STAGED geometry remains append-only in each completed slot. At submission the
renderer copies each populated page once in a preceding command buffer and
publishes its writes to vertex/index input with one memory dependency. Rendering
can continue across geometry uploads. Both command buffers use the original
slot pool/timeline serial and bounded continuation ownership. Mapped data is
flushed before submission, and GPU timestamps start before the copy buffer.
There is no extra geometry arena, buffer budget, background worker or per-draw
allocation. DIRECT retains mapped inputs.

The dependency follows Vulkan's
[submission-order and memory-dependency rules](https://docs.vulkan.org/spec/latest/chapters/synchronization.html)
and the
[transfer-to-vertex-input example](https://docs.vulkan.org/guide/latest/synchronization_examples.html).
Command buffers alone are not a memory dependency; the explicit transfer/input
barrier is required.

## Measurement protocol

Windows x64 Release/MSVC, AMD Radeon RX 9070 XT, vendor 4098/device 30032,
driver **8389024**. The historical SVG report used driver 8388996; this receipt
remeasures the frozen before binary and candidate on the current driver.
Historical cross-driver timings are not used as the baseline.

Eight alternating pairs per case cover the actual 180-frame 5000→5003 Spiral,
DIRECT/STAGED with cache off/on, a null control, and native Slug simple/composed
scenes. All setup, cold and outlier samples remain. Timing performs no readback
and does not certify foreground FPS or Task Manager utilization. A separate
three-version ablation isolates consolidated uploads from scene batching.

All values below pool every frame of the eight alternating pairs, in milliseconds.

| Workload | CPU mean before → after | CPU change | GPU mean before → after | GPU change |
| --- | --- | --- | --- | --- |
| null | 2.926105 → 2.984993 | +2.01% | n/a | n/a |
| spiral-direct-cache | 3.597092 → 3.641350 | +1.23% | 0.641514 → 0.633321 | -1.28% |
| spiral-direct-plain | 3.573884 → 3.608540 | +0.97% | 0.644265 → 0.647813 | +0.55% |
| spiral-staged-cache | 3.664592 → 3.675818 | +0.31% | 1.020552 → 0.784887 | -23.09% |
| spiral-staged-plain | 3.683561 → 3.633759 | -1.35% | 1.021057 → 0.782446 | -23.37% |
| vector-direct-complex | 3.315675 → 1.116484 | -66.33% | 3.316263 → 1.093559 | -67.02% |
| vector-direct-simple | 0.799187 → 0.822605 | +2.93% | 0.267314 → 0.267821 | +0.19% |
| vector-staged-complex | 4.586741 → 1.082636 | -76.40% | 4.601710 → 1.047876 | -77.23% |
| vector-staged-simple | 0.797884 → 0.812815 | +1.87% | 0.271273 → 0.273737 | +0.91% |

| Spiral mode | p95 before → after | p99 before → after | Maximum before → after | Frame 0 median before → after |
| --- | --- | --- | --- | --- |
| spiral-direct-cache | 6.0848 → 6.0296 | 16.0599 → 16.1430 | 20.9659 → 18.8279 | 5.6314 → 5.7622 |
| spiral-direct-plain | 6.4539 → 6.1599 | 15.7564 → 15.8764 | 19.9065 → 18.8030 | 5.6945 → 5.6035 |
| spiral-staged-cache | 6.9776 → 6.4815 | 16.0248 → 16.3289 | 17.1673 → 18.6338 | 5.8805 → 6.0329 |
| spiral-staged-plain | 6.2676 → 6.4164 | 15.8283 → 15.8888 | 18.7954 → 17.9027 | 5.8302 → 5.8133 |

The STAGED GPU reduction is repeatable across plain/cache and the separate
ablation. CPU results are mixed: STAGED plain improves 1.35%; cached mean is
+0.31%, and its absolute worst increases 17.1673→18.6338 ms. DIRECT CPU means
increase 0.97–1.23%. Simple-path CPU means increase 1.87–2.93%. These raw costs
remain in the qualification; there is no universal CPU or worst-frame speedup.
The null control also increases 2.01% despite byte-identical code, writable-data
and unwind sections in its before/after executables; its only read-only section
differences are three bytes in the debug-directory timestamp. That establishes
timing variability
outside a changed null algorithm, but does not prove that it explains every
native CPU difference. No control-normalized speedup is substituted for the
measured values. Run/frame identities for all peaks are retained.

| STAGED ablation | GPU baseline | GPU uploads only | GPU final combined |
| --- | --- | --- | --- |
| spiral-staged-cache | 0.965508 | 0.765873 | 0.754935 |
| spiral-staged-plain | 0.959899 | 0.747421 | 0.760887 |
| vector-staged-complex | 4.584421 | 3.051296 | 0.964614 |

The uploads-only binary retains the old 480-draw composition. It reduces
composed STAGED GPU cost from 4.584421 to 3.051296 ms; scene batching reduces
it further to 0.964614 ms in that series. CPU ablation samples are retained
separately; cached Spiral CPU remains mixed. This isolates concrete transfer/
pass overhead rather than attributing all timing changes to HarfBuzz shaping.

Every native vector draw/flush sample reports zero counted C++ allocations and
zero draw-time upload-buffer allocations. Spiral vertices, indices, draw counts
and **logical** upload bytes match before/after at every frame. Consolidated
copies also transfer existing 32-byte alignment gaps (at most 31 bytes per
append); logical upload counters are not claimed to measure physical PCIe bytes.
Reserved Spiral upload memory stays 68,681,728 bytes DIRECT / 135,790,592 STAGED.

| Composed SVG residency | Before | After |
| --- | --- | --- |
| Vulkan image allocations (bytes) | 1,253,632 | 4,325,632 |
| Renderer path accounting (bytes) | 233,728 | 3,324,032 |

Prepared tiles add 3,072,000 image bytes (about 2.93 MiB) in this scene, plus
18,304 bytes of fixed CPU batch scratch. This is an explicit bounded memory/
GPU-time tradeoff within the unchanged 64 MiB vector cache. Simple sources
create no layer images; old Spiral scenes create no vector assets or targets.
Native Slug remains analytic coverage and is not replaced with the portable
prepared-texture representation.

The earlier candidate and all diagnostic/recheck samples remain in the raw
evidence. Its simple-path CPU increase prompted a queue-discriminator/layout
refinement; the final table above still reports the residual observed increase.
Interrupted build/validation attempts are retained and excluded from pass counts.
One preliminary reference-only launch overlapped linking and was stopped before
timing; the final series starts only after the candidate build completed.

## Exact visual and regression validation

- Full Release and Debug `tools/run_tests.py --vulkan --format [--release]`
  pass, including 55 native cases per configuration, all original suites and
  goldens, installed core/SDL/Vulkan consumers, every example, API/tutorial
  checks and clang-format 22.1.3. All 22 Python tool regressions pass. The new
  untracked C++ sources also pass an explicit formatting check.
- The independent batch suite checks actual upload/submit/render commands,
  one copy per populated slot page, 4096-byte forced arenas under a 512 KiB
  budget, continuation ownership, overlapping isolated instances, mixed
  single-paint/composed sources, mesh ordering, clips, reflection, transparent
  and opaque fallbacks, 1x/2x/4x DPI and aligned tile-budget fallback.
- Final frozen-binary qualification compares **2880 complete images exactly**:
  2160 old Spiral frames (before/after, DIRECT/STAGED, cache off/on, prepared
  and late routes), plus 720 candidate composed SVG frames. Every 180-frame
  sequence includes frame 0 and all measured peak indices. Timing itself has
  no readbacks; the golden runs use the identical fixed-time model and stream
  counters. The first candidate's complete comparisons and timings remain in
  the receipt instead of being discarded.
- The new immutable SVG fixture contains all 180 full 512x512 baseline frames;
  both upload-mode captures and 360 baseline identity comparisons succeeded.
  Existing committed fixtures, assertions and tolerances were never changed.
- HarfBuzz/SDL/Vulkan OFF boundaries pass. HarfBuzz-OFF native indexed STAGED
  tests and the explicit vector tutorial pass. An actual amalgamated-header
  consumer compiles, draws overlapping isolated Slug instances with STAGED
  uploads, checks their resulting color/alpha and reports zero validation errors.
- Final actual `pui_showcase_vulkan` checks run 1080 application frames across
  three before/after cases: SVG gallery, 640x480 SVG gallery and 5000-item Spiral.
  All three complete BMP pairs are byte-identical; their candidate framebuffers
  were inspected for curve/stroke/gradient/clip quality and layout integrity.
- The live SVG window opened, but an unrelated Windows Security network-permission
  dialog for UnrealGame covered it. No security-dialog input was issued. Live
  interactive controls are **not certified** by that observation; the full
  application framebuffers and scripted tests above provide the final visual
  evidence. The owned test job was stopped and its window closed, leaving the
  security prompt untouched. No foreground pacing claim follows from this check.
- The protected audit verifies 124 existing test/oracle files, all 32 unrelated
  report drafts, 11 shader files, all 238 timed source files and 360 original
  Spiral reference hashes unchanged. The pinned SDL submodule stays clean.

## Reproduction and evidence

The independent native regression executable supports `--staged`, immutable
`--reference DIRECTORY` and `--capture-reference NEW_DIRECTORY`. Capture refuses
existing directories. The composed fixture archive preserves all 180 complete
baseline RGBA frames, their hashes and the frozen executable identity. DIRECT
and STAGED baseline captures are identical. Fixed native fixtures qualify this
adapter/driver; ordinary same-adapter immediate/batched regressions remain
independent of those immutable files.

```powershell
python tools/run_tests.py --vulkan --format --release
python tools/run_tests.py --vulkan --format
python tools/compare_vulkan_batches.py `
  --before BASELINE_BIN_DIRECTORY --after out/build/x64-release/Release `
  --spiral-references IMMUTABLE_SPIRAL_PARENT `
  --vector-references tests/golden/vector_batch/frames.zip `
  --output NEW_RESULT_DIRECTORY --pairs 8
```

[Summary, source/binary hashes and per-run peaks](perf/vulkan_batches.json),
[all 66600 retained timed frames](perf/vulkan_batches_frames.csv.gz),
[raw commands, logs, diagnostics and application captures](perf/vulkan_batches_raw.zip),
[bundle hashes](perf/vulkan_batches_bundle.json), and
[the immutable 180-frame SVG fixture](../tests/golden/vector_batch/frames.zip).

The final main series contains 25920 frames; the separate final ablation adds
6480. The first candidate contributes another 32400; the simple-path recheck
adds 1440 and the initial diagnostic adds 360. No cold or outlier frame was
dropped. Raw logs retain interrupted attempts separately; they are not counted
as completed suites or measurements. The raw bundle includes the three reviewed
application BMPs and immutable-reference hash receipts, without duplicating the
multi-gigabyte historical Spiral corpus.
