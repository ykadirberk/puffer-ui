# Vulkan backend qualification

23 October 2026, source base `de2e3fa` plus the Vulkan worktree changes.
Status: implemented and qualified on Windows/RX 9070 XT in the current worktree.
Full Debug/Release and the selected V0-V4/V6 gates passed; V5 remains a conditional
future phase. Existing SDL test bodies, golden references and submodule sources
are unchanged. This is not a Linux/macOS, cross-API pixel-parity or power certificate.

## Requirements and implementation evidence

| Requirement | Implementation / evidence |
| --- | --- |
| Additional, optional renderer | `PUFFERUI_WITH_VULKAN=OFF` by default; `pufferui::vulkan` is a separate implementation variant and installed component. The SDL_Renderer path remains the default. |
| SDL remains window manager | `sdl3_vulkan_host.inl` supplies surfaces, output sizes and cursors; the existing SDL bootstrap/pump retains input, clipboard, chrome, accessibility and overlays. Native tests assert no SDL_Renderer is created. |
| Application-owned device | `vulkan_native_device` borrows instance/physical-device/device/queue/family, accepts engine dispatch, and checks declared enabled dynamic-rendering/synchronization2/timeline features. Contract and installed tests destroy a borrowed child, then render/read with the still-live owner. |
| Engine headers/loaders | Separate `pufferui/vulkan.h`; main declarations remain SDK-free. A `VK_NO_PROTOTYPES` installed consumer and a header compile probe cover public-header compatibility; backend dispatch remains per instance/device. |
| External image display | `import_vulkan_image` wraps producer image/view without taking ownership; `release_vulkan_image` flushes pending cache uses and returns a completion timeline value. Tests use binary and positive timeline ready semaphores, cache off/on, and destroy producer resources before reading the completed result. |
| Shorthand setup | `sdl3_vulkan_app_init`, `create_sdl3_vulkan_device`, sized `create_vulkan_surface`; runnable `pui_ex_vulkan` and `pui_showcase_vulkan`. |
| Core render contract | Original triangle/index order, float-color conversion, separate RGB/alpha blend factors, no culling, integer scissor, ordered texture updates, per-target clips, bilinear copy, synchronous readback and deferred destruction. Both upload modes run channel/sampling/clip/target tests. |
| Bounded upload and synchronization | Mapped pages with actual allocation requirements, shared upload/local-buffer/readback budget, slot timelines and retirement. DIRECT and STAGED preserve the same pixels. Fault injection covers create/allocate/map/descriptor/retry/submit/acquire and local-buffer failure; explicit non-coherent paths are forced through a dispatch wrapper. |
| WSI lifecycle | Per-surface slots/acquire semaphores, per-image present semaphores, supported SDR format/present modes, two windows, outside popup/overlay, rapid resize, injected logical DPI, deterministic zero-size suspension/resume and close. |
| Showcase identity and VSync | The actual Vulkan device selects the `vulkan showcase` sidebar label. A separate actual-showcase regression covers interactive ON, selftest OFF, initial backend-policy disagreement and off/on transitions. A focused visible RX9070XT probe records FIFO/Immediate/FIFO and wall-clock FPS. |
| Exact optional cache | Canonical fields, generation/content/sampler dependencies, two hash filters plus exact byte comparison, conservative dirty replay, effect-prefix full replay and submit-success publication. Collision, generation wrap, budget fallback and no-clear alpha accumulation regressions remain independent. |
| Real workload equality | Every 180-frame 1150/5000 Spiral sequence compares cache off/on and direct/staged. A separate 180-frame real-widget sequence covers caret/text input, scroll, multi-page atlas updates, popup close, blur, gamma/DPI and surface recreation, also with staged uploads. |
| Consumption and maintenance | Core-only Vulkan build without SDL, in-tree/installed core/SDL/Vulkan consumers, amalgamated and native-header-first consumers; shader source/compiler/SPIR-V receipt, atomic example, README capture, tour, API/tutorial/format checks. |

## Design decisions and boundaries

The original [design](vulkan_renderer_design.md) remains the architectural
reference. The implementation selects its reference triangle path and initial
conservative CPU raster cache. Shader arithmetic, shapes, blur kernels and
primitive order are not replaced by an SDF/compute painter.

The updated request suggests 2D SDF drawing and the Slug path integrated into
HarfBuzz. These are optional follow-up renderer paths, not prerequisites for
native Vulkan integration. The existing `render_device::draw` receives only
triangles and atlas UVs; it has no analytic rounded-rect/curve or glyph-outline
commands from which to reconstruct an SDF or Slug program losslessly. Replacing
that contract in this backend would silently couple layout/text changes to the
new renderer. The selected reference path therefore continues to consume the
current stream. A future opt-in analytic primitive interface needs a triangle
fallback and independent quality/CPU/GPU measurements. A future outline-text
path needs glyph-ID/outline/shaping integration and separate coverage, gamma,
clipping, fallback-font and caret/layout tests.

HarfBuzz's current upstream documents `libharfbuzz-gpu` as an experimental
outline-to-GPU encoder using the Slug algorithm, with GLSL among its shader
outputs; it is distinct from the shaping library and the current bitmap atlas
([upstream documentation](https://github.com/harfbuzz/harfbuzz#readme),
[public GPU API](https://github.com/harfbuzz/harfbuzz/blob/main/src/hb-gpu.h)).
No dependency or Slug implementation is claimed by this revision.

- Cross-API SDL/D3D11 versus Vulkan pixel equality was explicitly waived by the
  implementation request. The default SDL renderer is not replaced. Existing
  SDL goldens stay fixed; cached Vulkan must remain exactly equal to uncached
  Vulkan on the same device.
- The cache uses whole-draw payload identities and a conservative dirty bounding
  rectangle aligned to 32-pixel tiles. It does not claim per-triangle tile
  locality, independent tile programs, GPU binning or indirect GPU dirty dispatch.
  Changed blur/target-prefix chains replay fully. Cache remains opt-in.
- Upload pages provide explicit suballocation and budget backpressure without a
  VMA dependency. Images use dedicated allocations, counted separately; the
  upload budget is not a total-process or total-image-memory limit.
- Pipelines are created before first UI drawing and retained per backend/device
  and format. No persistent disk pipeline-cache format is exposed.
- A different exclusive graphics/present queue family is rejected explicitly.
  External producer images must use the UI family or valid concurrent sharing;
  raw handles from another VkDevice are not interchangeable with this device.
- Windows native execution is qualified here. Linux has a separate lavapipe
  correctness workflow; its presence is not evidence of a completed Linux run.
  macOS/MoltenVK and other vendors/drivers need separate qualification.
- DPI in the WSI regression is injected logical scale. Zero framebuffer extent
  is provided by a real-SDL-surface test host to avoid asynchronous OS timing.
  These are not physical mixed-monitor or desktop-compositor latency claims.
- Forced non-coherent tests exercise explicit flush/invalidate code on real
  valid mappings; they do not establish that this hardware has a naturally
  non-coherent memory type.

The V5 GPU-preprocessing and upload/cache decisions below use the retained
measurements, not Vulkan's name or the article's hardware/power numbers.

## Validation receipts

[The validation receipt](perf/vulkan_validation.json) pins source/test hashes,
the generated test registry, completed commands, log hashes, device identities,
red/green startup regression and the visible pacing probe. Final commands:

| Check | Terminal result |
| --- | --- |
| `python tools/run_tests.py --vulkan --format` | PASS, 415.375s |
| `python tools/run_tests.py --vulkan --release --format` | PASS, 239.046s |
| Explicit format of every new native slice/test/example/consumer | PASS, clang-format 22.1.3 |
| Native-header-first and regenerated amalgamated consumers | PASS |
| SDL-free `vulkan_selftest` registry | PASS, six targets/seven cases |
| Native SDL/D3D11 demo build and own-window smoke | PASS |
| Native showcase fuzz 731, 180 frames, validation enabled | PASS |
| Actual RX9070XT showcase capture, reviewed | PASS |

The full registry has nine native executables and fifteen cases, including six
180-frame Spiral comparisons per configuration and direct/staged interactive
sequences. `pui_add_vulkan_test` registers each executable/case once; the generated
JSON drives the Python runner, and the same CMake registry drives CI/selftests.
The unchanged checks include 22 SDL golden scenes, 16 performance fingerprints,
24 core fingerprints, four 1150/four 5000 Spiral golden frames and ten peak frames,
core/pump/performance/showcase regressions, examples, installed/external consumers,
tutorial snippets and API drift. The receipt pins all 55 original tracked files
under `tests/`; BMPs also match HEAD byte-for-byte. No golden `--update` was used.

Native conformance/fault/WSI/image suites assert zero Vulkan validation errors.
Spiral compares every frame, including the geometry peak and Add transitions,
with exact RGBA equality on the same device. Synchronization validation is on
for correctness runs and off for timing. Existing D3D11 implementation slices
and the pinned SDL submodule remain unchanged.

The first capture attempt through Windows PowerShell failed its script
authorization check. It was retained as an attempt log; PowerShell 7, already used
by the project's checks, ran the capture successfully. No execution policy changed.

## Measurement protocol

Timing runs start only after builds, tests and captures finish. The comparison
tool freezes executables, alternates mode order, keeps an A/A control and retains
all raw frames and outliers. Cache/reference and upload/reference image runs are
separate from timing; readback/capture overhead is not folded into clean timing.

- Spiral: 1360x860, hidden native window, vsync off, 64-size tiles, 1150/5000
  initial items, 180 frames, dt=1/60, actual keyboard Adds90/120/150.
- Interaction: offscreen 320x240, resized by recreation to352x256 at100; 180
  frames, dt=1/30, text input30/130, atlas pressure20-23, wheel40-42,
  blur60-69, logical DPI1.5 at80-99, popup90-93, text gamma1.2 from140.
  Surface recreation is inside the measured frame. Initial device/font setup
  is outside per-frame timing and included in process elapsed time.
- Frame0, every maximum, warm p95/p99 and process elapsed are retained separately.
  A smaller steady average cannot certify all-frame or foreground FPS.
- GPU query samples describe delayed completed submissions. Their deltas are not
  aligned to the CPU frame that harvests them; queue/present/slot waits and GPU
  intervals must not be added as independent elapsed totals.
- The initial default-loader series used AMD Radeon(TM) Graphics (integrated),
  while SDL/D3D11 used RX9070XT. Those are explicitly separate historical series.
  Final Vulkan and DXGI queries both identify RX9070XT, vendor0x1002/device0x7550.
  Vulkan uses API1.4.359/driver8389024, SDK1.4.335.0, graphics family0. The Vulkan
  user-mode package is selected explicitly; equal adapter identity does not imply
  identical API driver paths.
- The system display-class Vulkan registry points to a nonexistent AMD manifest.
  Four existing DriverStore packages were probed without changing registration;
  all exposed RX9070XT. The final child processes use only
  `VK_DRIVER_FILES=C:\Windows\System32\DriverStore\FileRepository\amdvlk.inf_amd64_865a9c4fff871fca\amd-vulkan64.json`.
  Every timed Vulkan Spiral run must report the required RX9070XT device name.
  The receipt retains the loader diagnostics and queried IDs. No driver was
  installed, registry edited, or external desktop application changed.
- The requested Vulkan marker has two fewer glyphs than the original SDL sidebar
  subtitle: at frame127, Vulkan requests1,963,931 vertices/8,351,448 indices and
  SDL requests1,963,939/8,351,460. This is an application-workload comparison with
  the authorized marker, not an identical-command-stream backend microbenchmark.
  Geometry counts must match across repetitions/modes within each backend family.

## Results and completion audit

The final [JSON](perf/vulkan.json) and [raw CSV](perf/vulkan_frames.csv) retain
17,280 frames: 12,960 Spiral frames and 4,320 interaction frames. Six alternating
repetitions include uncached direct, cached direct, uncached staged and an A/A
direct control; Spiral additionally includes null and SDL/D3D11. Executables,
actual showcase/helpers, backend, shaders, fonts, tool and runtime environment
are hashed. No build/test/capture overlapped timing; no maximum was discarded.

All values below are milliseconds. Add mean averages frames90-179 across runs.
The Add maximum and warm p95/p99 columns are the median of the six per-run
statistics; warm means frames60-179. Absolute maximum includes every frame0-179.

| Tiles | Mode | Add mean | Median Add max | Warm p95 | Warm p99 | Absolute max |
| ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 1150 | Vulkan direct/reference | 3.8620 | 9.6688 | 6.3790 | 7.7128 | 29.1026 |
| 1150 | Vulkan cached/direct | 4.1906 | 19.1983 | 7.6090 | 10.7187 | 30.0689 |
| 1150 | Vulkan staged | 4.1955 | 19.6255 | 7.7467 | 9.0457 | 30.3369 |
| 1150 | Vulkan direct A/A | 3.8341 | 9.5978 | 6.3505 | 7.6157 | 28.5922 |
| 1150 | Null | 2.0781 | 6.1295 | 2.6018 | 2.7015 | 6.3711 |
| 1150 | SDL/D3D11 | 9.2349 | 75.9282 | 27.9698 | 66.9657 | 77.9757 |
| 5000 | Vulkan direct/reference | 5.6779 | 25.5526 | 11.3487 | 23.5085 | 62.6968 |
| 5000 | Vulkan cached/direct | 5.9982 | 25.5043 | 17.9032 | 23.2287 | 36.6025 |
| 5000 | Vulkan staged | 6.3218 | 37.7965 | 17.8914 | 34.2672 | 44.9896 |
| 5000 | Vulkan direct A/A | 5.5301 | 27.3145 | 11.5070 | 23.5015 | 34.3311 |
| 5000 | Null | 3.1709 | 9.8609 | 5.8406 | 9.0068 | 10.5448 |
| 5000 | SDL/D3D11 | 18.0733 | 186.2714 | 80.2673 | 147.0255 | 189.0809 |

This configuration's Vulkan direct Add mean is 58.2%/68.6% below SDL/D3D11 at
1150/5000 items. This is bounded to this protocol/device/driver configuration,
with the subtitle difference above; it is not a universal backend speed ratio,
cross-API image certificate or all-frame FPS claim. Reference versus A/A differs
by approximately 0.7%/2.6%; a single maximum is not a stable performance guarantee.

The 5000 direct absolute maximum is run 1/frame 127, retained at 62.6968 ms:
62.2107ms build/record/upload,0.4282ms end,0.3239ms present,0.0071ms slot wait.
It carries96,255,536 upload bytes at the geometry peak. Build includes core work
and device flush/upload invoked during UI submission; this is not proof of one
specific CPU function or scheduler cause. In the SDL/D3D11 worst run6/frame127,
189.0809ms consists of33.9814ms build and155.0934ms end. Delayed GPU samples
cannot be subtracted from these CPU phases to infer a missing cause.

Frame0 medians are28.3357ms/34.1432ms for direct Vulkan and33.9301ms/41.3978ms
for SDL/D3D11 at1150/5000. Median complete process elapsed, including device/font/
pipeline setup and shutdown, is0.9375s/1.1955s for direct Vulkan,0.9765s/1.2345s
for cached direct,0.9840s/1.2425s for staged and1.6875s/2.8440s for SDL/D3D11.
Startup was not removed to advertise a smaller steady average.

The real-widget interaction sequence averages0.222951ms direct/reference,
0.227435ms cached,0.260921ms staged and0.225435ms A/A. Resize/recreation at100
averages10.3350/10.6093/12.0393/10.4373ms respectively. Some individual phases
look faster with cache, others slower; the net result and A/A variability do not
justify enabling cache automatically. The dataset retains each phase/run.

Observed maxima across final sampled frames: combined upload allocation ledger
266,755,976bytes (within256MiB), device-local upload subset129,644,456bytes,
images13,088,000bytes, peak cache metadata62,892,740bytes (within64MiB), and14
descriptor sets. The local-upload subset must not be added to the combined
ledger. Retired bytes sampled at frame boundaries are0; this does not assert
that no deferred retirement existed inside a frame or count total process RAM.

### Prior iGPU cache before/after evidence

[Initial JSON](perf/vulkan_initial.json)/[frames](perf/vulkan_initial_frames.csv)
and [optimized JSON](perf/vulkan_igpu_optimized.json)/[frames](perf/vulkan_igpu_optimized_frames.csv)
retain another17,280 frames each. These series use the default integrated GPU
for Vulkan and must not be relabeled as RX9070XT results. Separate observation
windows also changed controls, so a raw before/after average alone is not a
clean causal speedup claim.

| iGPU window / tiles | Direct Add mean | Cached Add mean | Direct A/A Add mean |
| --- | ---: | ---: | ---: |
| Initial / 1150 | 4.1738 | 15.0598 | 4.0176 |
| Optimized / 1150 | 4.5684 | 5.1766 | 4.7803 |
| Initial / 5000 | 6.4338 | 14.7077 | 5.7583 |
| Optimized / 5000 | 7.8131 | 8.5542 | 8.3332 |

Initial cache preparation serialized/hashed each byte and persisted too long on
large constantly-changing tapes. The optimized implementation keeps the same
canonical72-byte legacy oracle, copies the asserted packed vertex/index payload
on little-endian machines, hashes words while retaining exact full-byte equality,
and backs off after two large full misses. A red control with the old cache fails
the new full-miss bypass assertion; all43 cache/reference images remain exact.
The initial5000 staged928.6964ms and A/A252.2698ms frame0 outliers remain in
the data. The optimized observation window is slower for reference/null controls
too; no universal CPU gain or disappearance of all startup stalls is claimed.

### VSync and final choices

The actual-showcase startup regression fails before the fix: the selftest
bootstrap disables VSync but the HUD assumes ON. The fix explicitly establishes
interactive ON/selftest OFF when the native showcase initializes. It also
restores interactive startup policy when the caller previously disabled the
device, then tests off/on transitions against actual FIFO state. Existing SDL
behavior and its goldens remain unchanged.

In a separate visible RX9070XT probe on a180Hz display, each180-frame phase
remained focused180/180 frames: FIFO ON181.739FPS, Immediate OFF320.946FPS,
FIFO ON181.448FPS. The short FIFO samples may lead the display by a few queued
frames; this verifies the observed policy switch, not a universal latency cap.
The [reviewed showcase capture](img/readme/vulkan-showcase.png) is a deterministic
selftest image, so its60FPS is fixed simulation time and its HUD correctly says OFF.

DIRECT remains the default: staged Add mean is8.6%/11.3% higher in final1150/5000
tests. Cache remains opt-in: cached Add mean is8.5%/5.6% higher and the interaction
mean is2.0% higher. A smaller cached single worst frame is not used to override
these decisions. V5 GPU preprocessing was not implemented or timed. The CPU
cache's limited whole-draw locality and full-miss preparation costs inform only
the implemented CPU cache's defaults. These CPU-cache measurements are not evidence
that GPU binning/compute painting would be slower and do not reject that design.
It remains unimplemented work requiring its own experiment and exact tests.

The native triangle renderer is not the article's complete GPU painter. In
particular it has no compute coarse/fine binning, per-tile GPU command interpreter,
GPU-produced changed-tile dispatch count, indirect dispatch/draw, SDF painter,
shape groups or alpha barriers. The article describes those as parts of its
[GPU architecture](https://blickeditor.com/blog/caching-an-immediate-mode-ui).
The completion claim here covers the native backend and CPU raster cache only;
the article-style GPU renderer requires a separate implementation plan.

Selected V0-V4/V6 deliverables are complete for the qualified Windows device:
optional native renderer; SDL host; owned/borrowed setup; external images; typed
shorthands; complete render/blur/readback/synchronization contracts; bounded upload;
exact optional cache; native showcase; examples/docs/package/amalgamation; full
unchanged existing tests and new regressions; matched physical-device comparison
with all samples retained. SDF/Slug, GPU binning, disk pipeline cache, HDR,
cross-family exclusive presentation and other platforms remain explicit limits
or optional future work, not silently asserted implemented features.

### Commit verification and driver-launch helper

The [commit receipt](perf/vulkan_commit_validation.json) records a second full
Debug/Release verification after adding the offscreen `--vulkan-device-info`
diagnostic and `tools/run_vulkan_showcase.py`. Seven portable regressions cover
verified driver selection, stale registered package families, explicit overrides,
failure/timeout handling and argument forwarding. Both actual Debug and Release
launches selected RX9070XT without manually supplying either driver environment
variable and passed a validation-enabled twelve-frame showcase selftest.

The Debug runner passed completely. Every Release test, pixel comparison,
consumer and formatting check passed; its examples step initially could not
overwrite the already running showcase executable (Windows LNK1104). The same
Release configuration then built and selftested every example in the separate
`Release-commit-check` output directory, preserving the open application. The
receipt retains the failed attempt and the successful isolated retry separately.
After the user closed the application, the standard Release output configuration
was restored and every example built and selftested there too. The helper's
default executable path then passed both automatic device discovery and the
validation-enabled showcase selftest without a driver environment override.

The helper only selects an existing, probed manifest for its child process. It
does not repair the system driver's registry installation. Interactive VSync
remains ON and selftest VSync remains OFF. An additional default-loader probe
was unfocused in all sampled frames and is diagnostic only, not a pacing result.
The frozen timing datasets and their original validation receipt remain unchanged;
the native rendering/cache/VSync implementation and shader hashes still match
that receipt. These additional CLI/launcher checks are not new timing samples.
