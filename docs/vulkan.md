# Native Vulkan renderer

PufferUI's optional Vulkan 1.3 backend implements `render_device` and
`render_surface`. SDL3 still manages windows, input, clipboard, system chrome,
accessibility and popup overlays. Vulkan records and presents the graphics;
this route does not create an `SDL_Renderer`. The existing SDL renderer remains
the default. Core-only and SDL-only applications need no Vulkan SDK.

This branch starts from the first native Vulkan renderer (`8c62fc4`). Shapes use
triangle geometry; backdrop filtering uses Gaussian or Dual Kawase according to
the pixel radius. With HarfBuzz built, the default factory
shapes text with HarfBuzz and draws upstream Slug outlines through a separate
instanced graphics pipeline. There is no SDF shape painter or compute/tile
interpreter. See [HarfBuzz text](harfbuzz.md) and the
[explicit ownership tutorial](tutorial/10-vulkan-without-shorthand.md).

## Build and run

Install a Vulkan SDK for compilation and a Vulkan-capable driver for execution.
On Windows, run CMake from the MSVC developer environment. The SDK's
`VULKAN_SDK` environment variable lets CMake find its headers and loader library.

```powershell
cmake --preset x64-release -DPUFFERUI_WITH_VULKAN=ON
cmake --build out/build/x64-release --target pui_ex_vulkan pui_showcase_vulkan
out/build/x64-release/Release/pui_ex_vulkan.exe
out/build/x64-release/Release/pui_showcase_vulkan.exe
```

In a CMake subdirectory build, link `pufferui::vulkan`. For an installed package:

```cmake
find_package(pufferui CONFIG REQUIRED COMPONENTS vulkan)
target_link_libraries(my_app PRIVATE pufferui::vulkan)
```

Only requesting that component loads the Vulkan dependency/export. The core and
SDL targets from the same installation do not acquire a Vulkan SDK dependency.
`PUFFERUI_WITH_SDL3=OFF` builds the Vulkan variant for offscreen/custom-host use
without SDL. Link one implementation variant per executable; do not combine
the core, SDL and Vulkan implementation libraries in the same executable.

Checked-in SPIR-V is compiled into the implementation. End users need a runtime
loader/driver, not a shader compiler or SDK installation. To regenerate shaders
after editing `src/shaders/vulkan/ui.vert` or `ui.frag`, run:

```powershell
python tools/compile_vulkan_shaders.py
```

The generator records compiler identity, flags and source/SPIR-V SHA-256 hashes.
It requires the SDK's `glslangValidator`. Validation mode additionally requires
`VK_LAYER_KHRONOS_validation`; it is intentionally separate from timed runs.

## Backdrop blur

The native painter selects the filter automatically for `ui::blur`. The existing
radius argument is in logical units; selection uses `radius * render_scale` in
framebuffer pixels:

| Pixel radius | Vulkan filter |
| --- | --- |
| Up to and including 8 px | Separable Gaussian, horizontal then vertical |
| Greater than 8 px | Dual Kawase downsample and upsample chain |

For example, `u.blur(area, 8)` uses Gaussian at scale 1 and Dual Kawase at scale
1.5 or 2. The existing no-effect rules for a logical radius below 0.5, zero alpha,
empty clips and host overlays still apply.

Gaussian uses the [CSS blur strength convention](https://drafts.csswg.org/filter-effects/#funcdef-filter-blur):
the logical radius is the standard deviation, so framebuffer `sigma` is
`radius * render_scale`. The discrete kernel is normalized and truncated at
`ceil(3 * sigma)` on each side. This matches CSS strength units; it does not
promise pixel identity with a browser's color space, edge policy or approximation.
Adjacent taps share bilinear fetches: the 49-tap maximum becomes 25 fetches
per axis. All coefficients keep binary32 precision; the fragment constants fit
the Vulkan minimum 128-byte push-constant budget. Weights are retained when the
radius is unchanged. Dual Kawase keeps its existing spacing and uses
five-fetch reduction and eight-fetch reconstruction kernels, with two to six
pyramid levels and radius-dependent sample spacing. Its kernel follows the
[Arm dual-filter presentation](https://community.arm.com/cfs-file/__key/communityserver-blogs-components-weblogfiles/00-00-00-20-66/siggraph2015_2D00_mmg_2D00_marius_2D00_notes.pdf).
Kawase approximates a Gaussian; crossing the filter threshold is not promised
to be pixel-identical between the two algorithms. Its earlier `radius / 3`
variance target remains unchanged to preserve existing wide-blur images. Only
Gaussian adopts CSS sigma units in this revision, so strength is not continuous
across the 8 px algorithm boundary.

Each window retains two RGBA8 scratch targets for the affected region, including
the sampling margin. They grow lazily in 32-pixel steps and are reused across
blur calls and frames. The first target's capacity is tracked independently:
a small Gaussian after a large Kawase does not promote it to the full size of
the retained second target. Applications that know their blur needs can prepare
both images once, before their first draw in a window's frame:

```cpp
// Include <pufferui/vulkan.h>; call after begin_frame and before painting.
// The default covers any Kawase region; this hint also bounds Gaussian masks.
pui::vulkan_prepare_blur(ctx, {.gaussian_area = {0, 0, 160, 100}});
```

The hint reserves at most scene-sized images, coalesces growth and preserves
ordinary lazy fallback for unknown regions. The native showcase calls it before
painting. The first target can remain half-size when the hinted Gaussian fits
that capacity. A viewport shrink retires oversized targets at a frame boundary, using
the normal GPU completion lifetime. Every filter pass clamps reads to the
written source region, so odd extents and smaller subsequent blurs cannot sample
old padding. Gaussian bounds include pixel centers in the rounded mask's outer
half-logical-pixel AA fringe before adding kernel support, including high DPI.
Its final composite converts filtered associated RGB to straight RGB before the
normal alpha blend; fractional source alpha is applied once. Opaque samples,
ordinary primitives and the Kawase composite keep their existing arithmetic.
Alpha, per-corner masks, scene clipping and draw order remain part of the
composite. Only the final filter pass crops its scissor to the consumer mask plus
the AA/bilinear apron; intermediate passes retain their full sampling padding.
Quad extents, UV arithmetic and source clamps stay unchanged.
The first rendering scope of each overwrite-only filter uses `DONT_CARE` for
its previous scratch contents. If a quad spans upload submissions, subsequent
scopes use `LOAD` to preserve earlier triangles. Scene/glyph rendering keeps
its ordinary load behavior. Effect tapes always replay their full prefix, so
this discard cannot erase pixels required by partial scene replay.
Filter kernels, final-pass clips, Gaussian composite mode and all normalized operands are part of
the retained command identity. Unchanged chains reuse final pixels; changed
chains replay their scene prefix and filter passes in full order.

Filtering is Vulkan-specific; SDL and explicitly selected mesh painters retain
their existing blur. The shader specializations are compiled at device setup;
the four checked-in graphics stages remain the only shader binaries.
`pui_vulkan_blur_tests` checks independent CPU kernels, the exact 8 px boundary,
DPI selection, target reuse and 108 complete cached/reference frames in each
upload mode. `pui_vulkan_gaussian_tests` adds 36 independent full-image/mask
oracles at scales 1, 1.25, 1.5, 2, 3 and 4, comparing 288 complete cached/uncached
frames and 72 idle cache hits in each upload mode. Its immutable capture and
comparison options also verify corrected goldens and pre-fix compatibility
images. Existing golden files remain unchanged.
`pui_vulkan_blur_optimization_tests` adds dense/sparse chunk equality, rejection
of a late invalid index before uploading any prefix, bounded frame-slot
continuation, scratch allocation/preparation and final-pass work checks in both
upload modes, both vertex representations and one/two frame-slot configurations.
It also forces a filter quad across rendering scopes and checks both exact
pixels and the first-discard/subsequent-load contract.
See the [blur optimization report](perf_vulkan_blur_optimizations.md) for current
before/after results, immutable all-frame Spiral comparisons and CSS goldens.
The earlier [Gaussian correction receipt](perf/vulkan_gaussian_validation.json) and
[correction validation logs and goldens](perf/vulkan_gaussian_validation_raw.zip)
record the former 15 px policy and the alpha/DPI fixes. The earlier
[2026-11-01 validation receipt](perf/vulkan_blur_validation.json) and
[complete validation logs](perf/vulkan_blur_validation_raw.zip) record the
initial 8 px policy's Release/Debug suites and remain historical evidence.

## Device selection

The backend prefers a compatible discrete physical device, or uses the explicit
`vulkan_device_opts::physical_device_index`. This cannot expose a device whose
driver manifest is missing from the system loader's discovery. An earlier
qualification required a machine-specific child-process manifest override:

```powershell
$env:VK_DRIVER_FILES = 'C:\Windows\System32\DriverStore\FileRepository\amdvlk.inf_amd64_865a9c4fff871fca\amd-vulkan64.json'
out/build/x64-release/Release/pui_showcase_vulkan.exe
```

This historical path is not a portable driver location. The current native
qualification selects the RX 9070 XT without a manifest override. Ordinary
launches use the executable directly, including from Visual Studio; Python is
optional. `tools/run_vulkan_showcase.py --dry-run` can diagnose stale discovery
and verify a child-process workaround. The backend never modifies registration.
`VK_DRIVER_FILES` is the loader's documented process-level driver selection
mechanism ([Khronos driver discovery](https://github.com/KhronosGroup/Vulkan-Loader/blob/main/docs/LoaderDriverInterface.md)).
For measured runs, `tools/compare_vulkan.py --require-device "AMD Radeon RX 9070 XT"`
fails if any Spiral Vulkan run reports a different physical device.

## SDL shorthand

The main header is sufficient for ordinary application setup:

```cpp
#include <pufferui/pufferui.h>
using namespace pui;

int main()
{
    sdl3_app app;
    if (!sdl3_vulkan_app_init(app, {.title = "Native Vulkan", .width = 960, .height = 640}))
        return 1;
    while (sdl3_app_pump(app))
    {
        sdl3_app_tick(app);
        begin_frame(app.ctx, *app.win, app.now, app.dt);
        {
            ui u(app.ctx);
            u.text(rect::make(16, 16, app.width - 32.0f, u.control_h()),
                   "Vulkan + SDL", u.th().text);
        }
        end_frame(app.ctx);
    }
    sdl3_app_shutdown(app);
}
```

`sdl3_app` options such as `hidden`, `asset_dir` and `wait_when_idle` are set
before initialization. Alternatively set `app.use_vulkan = true`, fill
`app.vulkan`, and call the existing argc/argv `sdl3_app_init`. The Vulkan
shorthand and generic path share the same SDL lifecycle and event routing.
`sdl3_app_open_window` / `sdl3_app_close_window` work on the additional windows.
`end_frame` presents once; applications must not present a second time.

The shorthand owns SDL initialization and calls `SDL_Quit` during shutdown.
An engine that already owns SDL should instead create its own
`SDL_WINDOW_VULKAN` windows, call `create_sdl3_vulkan_device`, create surfaces,
and route events with `pump_sdl_events`. Destroy contexts first, then surfaces,
then the backend, then native windows. The lower-level factory does not own the
engine's SDL initialization or native windows.

## Application-owned Vulkan device

Include `<pufferui/vulkan.h>` only where native SDK types are needed. A borrowed
device uses the same rendering implementation and its own backend resources:

```cpp
vulkan_native_device native;
native.instance = engine_instance;
native.physical_device = engine_physical_device;
native.device = engine_device;
native.queue = engine_graphics_queue;
native.queue_family = engine_graphics_family;
native.dynamic_rendering = true;
native.synchronization2 = true;
native.timeline_semaphore = true;
native.get_instance_proc_addr = engine_get_instance_proc_addr; // optional
native.get_device_proc_addr = engine_get_device_proc_addr;     // optional

vulkan_device_opts options;
options.native = &native;
render_device *ui_device = create_sdl3_vulkan_device(options);
render_surface *surface = create_vulkan_surface(ui_device, {.window = sdl_window});
```

The three booleans declare features the engine **actually enabled** when it
created the device. Feature availability is not enablement. The graphics queue
must support the chosen surface's presentation, and the engine must have enabled
`VK_KHR_swapchain` plus the platform instance extensions returned by
`SDL_Vulkan_GetInstanceExtensions` before creating its instance/device. For pure
offscreen use, call `create_vulkan_device` without an SDL host and
`create_vulkan_surface(device, {.width = 640, .height = 480})` without a window.

The options/native struct is copied during factory creation; its stack storage
need not outlive the call. The actual instance, physical device, device and queue
must outlive the backend. `destroy_vulkan_device` destroys only backend-owned
objects when these handles are borrowed. It does not destroy the engine device
or instance. Serialize engine queue submissions with PufferUI calls on the UI
thread; the backend does not synchronize unrelated engine host access to a queue.
Per-instance/per-device dispatch permits an engine loader and avoids mutable
global dispatch tables.
Public native headers also support `VK_NO_PROTOTYPES` consumers. A custom host
can return its loader entry point; the default null entry point selects the
backend's linked loader. The backend implementation TU itself uses Vulkan
prototypes; engines can link the prebuilt target without sharing that choice.

Owned setup prefers a compatible discrete GPU; set `physical_device_index` to
an enumeration index to select explicitly. Unsupported features, formats or
presentation queues fail with diagnostics; there is no silent renderer fallback.
`vulkan_handles(device)` exposes the selected handles for resource producers.

## Display an external VkImage

The image/view and its allocation belong to the application. The image must be
a single-sample 2D sampled image on the same VkDevice, with a compatible color
view and declared format/extent. The initial release supports the backend's queue
family or concurrent sharing (`VK_QUEUE_FAMILY_IGNORED`); it does not perform
ownership transfers from a different exclusive queue family.

```cpp
vulkan_image_opts source;
source.image = producer_image;
source.view = producer_view;
source.width = image_width;
source.height = image_height;
source.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
source.ready = producer_ready_semaphore;
source.ready_value = producer_timeline_value; // 0 for a binary semaphore

texture_handle borrowed = import_vulkan_image(ui_device, source);
image_handle picture{borrowed, image_width, image_height};
// During a UI frame:
u.image(image_rect, picture);

// After the last use, before the producer writes to or destroys its image:
u64 finished = release_vulkan_image(ui_device, borrowed);
if (!finished || !vulkan_wait(ui_device, finished))
    handle_backend_error();
// The application may now reuse/destroy producer_image and producer_view.
```

The producer must finish writing, provide a ready semaphore, and keep the image,
view and ready semaphore alive until release completes. Without a ready
semaphore it must establish completion before import. A binary ready semaphore
is consumed once. A timeline ready semaphore uses a positive `ready_value`.
The image remains read-only to the producer while imported. Release flushes
pending cache uses, transitions to the requested final layout and returns a value
on `vulkan_completion_semaphore(ui_device)`. An engine can wait on that timeline
in a submission instead of blocking its CPU with `vulkan_wait`. Reimport after
release to publish new producer content; pointer identity is not a content revision.

Do not pass a raw VkImage as a `texture_handle`. Import creates a backend wrapper
and descriptors; release retires them without destroying the producer image,
view or allocation. Keep the backend alive while using its completion semaphore.
`examples/atomic/vulkan.cpp` is a complete producer/upload/import/display/release
example with explicit destruction before backend shutdown.

## Uploads, cache and error handling

`vulkan_device_opts` selects the geometry upload path:

- `DIRECT` (default): persistently mapped host-visible geometry buffers.
- `STAGED`: mapped staging plus transfer to a device-local-preferred geometry
  buffer, with explicit transfer/input synchronization. It doubles the geometry
  storage requirement and is an explicit device/workload measurement choice.

Native shape vertices default to 20-byte position/UV/integer-byte-color inputs.
The shader reconstructs the original exact float channel bits. Set
`compact_vertices=false` for the retained 32-byte float-color reference. Native
Slug also has a bounded exact R32 coverage cache; use
`glyph_coverage_budget_bytes=0` for direct curve evaluation. See
[the native text contract](harfbuzz.md) for identities, budgets and retirement.

`upload_budget_bytes` defaults to 256 MiB and bounds actual Vulkan allocation
requirements for upload pages, optional local geometry and temporary readback.
Completed slots are reclaimed first; backpressure waits for necessary work
instead of growing without limit. Large indexed draws can be split into ordered
triangle spans to reuse a prepared page; vertex arithmetic and primitive order
remain unchanged. The complete indexed input is validated before recording any
prefix; proven subspans skip the duplicate bounds scan. A full prepared arena
can continue in another existing arena of the same capacity. Single-slot and
differently sized configurations keep the bounded wait/reuse fallback.

Validated dense spans accept fitting blocks of triangles, refining a boundary
with the original scalar rule. This preserves the exact greedy split and sparse
fallback without allocating planner storage. Dense subspans can upload original
indices and apply their base through `vkCmdDrawIndexed`'s signed vertex offset.
This path stays within the guaranteed 24-bit source-index range, so borrowed
devices need no additional feature enablement. Negative/internal bases, signed
boundary cases and larger original ranges retain CPU rebasing. Vertex selection,
primitive order, upload byte counts and the complete input check stay unchanged.

Acquire semaphores remain owned by logical frames and wait for their final
submission. Command pools/upload arenas track their own last submission;
the next logical frame can choose an older compatible pool instead of waiting
for the previous frame's final pool. This preserves the configured memory budget
and acquire lifetime while retaining pipelining after a streamed frame.
Large geometry pages do not replace the small upload pages. Non-coherent mappings are
flushed and readbacks invalidated explicitly. Images use dedicated allocations
and submission-based retirement; they are counted separately by `image_bytes` /
`retired_bytes` and are not included in the upload budget. This implementation
uses explicit page suballocation without a VMA dependency; it does not claim to
be a general-purpose image heap allocator.

The optional CPU raster cache is selected by `tile_cache` or
`vulkan_set_cache(device, enabled)`. It defaults off.
Call `vulkan_set_cache` between frames; the choice is applied when the next
surface frame begins. Canonical field serialization,
generation/content/sampler dependencies, two hash filters and exact byte
comparison protect hits. Whole identical frames reuse scene pixels. Changed
simple clear/draw scenes can replay a conservative dirty rectangle aligned to
32-pixel tiles; changed blur/target chains use full ordered replay. It is not
per-triangle GPU binning. Full-miss hysteresis and the 64 MiB default metadata
budget can bypass caching; no geometry or blur samples are dropped. Large tapes
(at least 1 MiB of estimated recording storage) back off for 64 frames after
two full misses; small tapes retain the 8-miss/16-frame policy. Packed payload
copying is guarded by exact field-offset/size assertions and little-endian
detection; other layouts retain field serialization. Word-wise hash filters
still require exact canonical-byte equality. A changing
5000-item Spiral may benefit little or become slower from bookkeeping, so cache
is not enabled globally by assumption.

Set `error_callback` and `user` for factory/recording errors. The first backend
failure is sticky and exposed by `vulkan_last_result` / `vulkan_last_error`.
Stop/recreate a failed backend; do not assume a partial frame was presented.
`vulkan_statistics` reports uploads, actual allocations, retirements, descriptors,
cache outcomes, waits and phase times.
`cache_prepare_ms` measures recording/canonicalization/hash/copy CPU time;
`cache_ms` measures comparison/replay flushing. Nested flush time is excluded
from preparation, and `cache_bypasses` counts adaptive cooldown frames.
GPU timestamp samples are cumulative, delayed completed submissions, not the
current CPU frame; do not add overlapped
CPU/GPU intervals. `vulkan_present_mode(surface)` reports the selected WSI mode
or `VK_PRESENT_MODE_MAX_ENUM_KHR` for offscreen/non-Vulkan surfaces.

`pui_vulkan_indexed_tests` pins exact scalar/block split tuples, bounded serial
capacity checks and actual uploaded index bytes/native vertex offsets. It
compares full pixels for ordinary, negative, INT_MIN, 24-bit-boundary and large
bases, both vertex representations and one/two frame slots in direct/staged
mode. See the [indexed upload report](perf_vulkan_indexed_optimizations.md) for
alternating production timings and immutable all-frame Spiral comparisons.

Native index validation and large-span min/max preparation use SSE2 when the
compilation target guarantees it. Other targets retain scalar validation and
the original block planner. Complete input validation still precedes every GPU
prefix; signed-limit bases, unaligned buffers, tails, sparse fallback and exact
greedy split boundaries are preserved. No floating-point flags, shader, arena
count or upload budget changes are involved. `pui_vulkan_integer_tests` checks
independent scalar oracles, protected-page tails, bounded work, pre-prefix
rejection and translucent dense/sparse pixels in direct/staged, 20/32-byte and
one/two-slot configurations. See the
[integer preparation report](perf_vulkan_integer_optimizations.md) for unchanged
all-frame Spiral goldens and alternating production measurements.

## Support and verification

The initial API targets Vulkan 1.3 on Windows/Linux with dynamic rendering,
synchronization2 and timeline semaphores. Windows native execution and validation
are tested locally. A separate Linux lavapipe CI job provides an offscreen
correctness route; a workflow definition alone does not prove a Linux run.
macOS/MoltenVK and separate graphics/present queue families are not qualified.
Resize uses conservative queue completion for old WSI resources at rare lifecycle
points; normal frames do not call global device idle. SDL popup overlays share
the device while owning independent surfaces and frame slots.

```powershell
python tools/run_tests.py --vulkan --format
python tools/run_tests.py --vulkan --release --format
```

Dedicated native suites cover borrowed device lifetime, producer image
synchronization/release, ordered texture updates, exact channel conversion,
targets/clips/copy/readback, allocation and submit failures, cache collisions /
fallbacks and WSI. The Spiral harness compares all 180 cached/reference frames
at 1150 and 5000 tiles. `--upload-reference` compares direct/staged pixels;
`--staged` also tests staged cache/reference equality. `--bench [--cache]
[--staged] --tiles 5000` emits a separate timing CSV; `--offscreen` avoids WSI.

`pui_vulkan_interaction_tests` additionally compares all 180 frames of a real
textfield/caret, scroll, multi-page atlas, blur, popup close, DPI/gamma and
surface-recreation sequence. It runs both direct and staged paths in the test
runner. Its separate `--bench [--cache] [--staged]` mode avoids readbacks and
includes surface recreation in the measured frame. To retain alternating
Spiral, interaction, A/A and optional Windows SDL controls:

```powershell
python tools/compare_vulkan.py --output build-vulkan-measure --runs 6 --references --interaction
```

Run measurements after all builds/tests/captures finish. Use a new output
directory for each series. The tool freezes executables and retains all frames,
including cold starts and every maximum; different adapters are not a matched
backend comparison.

Existing SDL goldens and tests remain unchanged. Cross-API/GPU raster equality
is not assumed; Vulkan remains optional. The Vulkan cache must be exactly equal
to its uncached Vulkan reference. See [the architecture](vulkan_renderer_design.md)
and the qualification report for measured behavior and remaining platform limits.
