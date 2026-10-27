# Native Vulkan renderer

PufferUI's optional Vulkan 1.3 backend implements `render_device`, supplies a
complete `render_painter`, and implements `render_surface`. SDL3 still manages
windows, input, clipboard, system chrome,
accessibility and popup overlays. Vulkan records and presents the graphics;
this route does not create an `SDL_Renderer`. The existing SDL renderer remains
the default. Core-only and SDL-only applications need no Vulkan SDK.

Core submits semantic operations before geometry or glyph preparation. The
Vulkan painter factory selects native shape, Slug and blur handling according to
device options; core does not inspect Vulkan mode or supply an implicit geometry
fallback. The backend's explicit mesh algorithms retain all remaining operations
with their original pixels. See the [required painter contract](porting_a_backend.md#render_painter)
and [boundary validation report](perf_renderer_boundary.md).

## Prepared painter resources

The showcase's default `INTEGRATED` configuration combines whole-program/tile
reuse and optional HarfBuzz/Slug text. Run `pui_showcase_vulkan.exe` directly;
`--vulkan-painter sdf --vector-text` remains an independent raster control.
The library default remains `REFERENCE`.

Integrated whole-program observation uses native operands. It does not create
or execute fine-cache kernels merely to learn whether a page is static. A
completed GPU comparison that finds changing LOAD content requests fine reuse;
the next frame boundary prepares its shared kernels, native function atlas,
metadata arenas and image histories. Glyph cache generations are acceleration
state; exact bounds, outline, scale, gamma and tint remain image identity.
See [the integrated contract](vulkan_integrated_painter.md) and
[the retained validation evidence](perf_vulkan_integrated.md).

SDF surfaces prepare upload pages, graphics descriptor sets and (for compute)
bounded metadata arenas before recording UI draws. Compute kernels and their
layouts belong to the device; slots share those immutable programs. Cache kernels
are compiled only when tile caching was requested. Present-mode changes preserve
prepared resources when the pixel extent and cache budget are unchanged.

Adaptive surfaces initially reserve only raster uploads and descriptors. Their
first eligible compute program requests preparation for the next frame boundary;
that first program uses the same complete raster output. Scenes that always route
to raster therefore keep compute arenas, cache images and cache kernels unallocated.
Runtime `vulkan_set_cache` transitions likewise prepare/release history resources
at a completed frame boundary; stale histories are not reused during the transition.

Each compute slot has a current arena and, when eligible for caching, one previous
arena. Completed slot reuse switches banks; readback-only submissions do not age
the history. The cache retains views of commands, tile lists, shapes and ownership
instead of making five history copies. Qualified pixels are written directly to
the cache image, eliminating full-target snapshot copies. Cache image budget
rejection is decided during preparation and is not retried for each draw. Pixel
telemetry uses workgroup reduction rather than a global atomic for every pixel.
At most two programs per slot receive full-size cache images, even if the maximum
cache budget could hold more. After completion, the slot assigns those existing
image leases to the programs with the largest measured qualified pixel coverage;
equal scores keep their current owners. Moving a lease invalidates both histories.
Other programs paint normally. No image is allocated during admission or drawing.

GPU commands, work lists, indices and shapes use unmapped, device-local-preferred
storage. One host staging bank per completed slot supplies commands and shapes;
staging does not retain a previous-frame copy. Small readable route/control
regions prefer coherent local memory and retain a portable host-visible fallback.
The blend table is calibrated in host memory, copied once to unmapped local
storage, and its original host allocation is released. A migration budget
rejection keeps the calibrated table; a Vulkan allocation failure remains an
error. Actual allocation requirements for every role count against the metadata
budget. Moving bytes to local memory can increase dedicated usage while reducing
shared usage; the allocation counters are not a whole-driver residency estimate.

The initial reservation scales with target extent, with 64 KiB rounding, and stays
within the existing device budgets. Budgets are limits, not amounts that must be
filled. A metadata/job capacity shortfall selects complete raster replay without
changing the output. Graphics descriptor exhaustion submits and waits before
reusing already allocated sets. Tiny upload budgets keep the existing streaming
path and may require allocations/waits; zero-allocation qualification applies to
the measured prepared workload, not arbitrary unbounded UI geometry.

Vector text additionally keeps append-only glyph outlines in a device-local
buffer with coherent host staging (each at most 512 KiB and at most one eighth
of the upload budget). New glyphs append bytes; their new range is transferred
before the next consuming batch. Repeated text does not upload the accumulated
font data per batch. Oversized collections retain their complete resident glyph
prefix. Per-batch glyph references select residency; only a batch containing an
unresident glyph uploads the outline blob. A shape-only batch never uploads
unrelated font data. Noncoherent host heaps retain per-submission glyph uploads.
HarfBuzz shaping, outline encoding, font selection, gamma and DPI remain unchanged.

## Whole native program reuse

`whole_program_cache=true` optionally reuses a complete SDF raster or compute program.
The showcase's `--vulkan-painter sdf` and `--vulkan-painter compute` enable it.
The adaptive selector keeps
its raster fast path without automatically enabling this extra work; applications
can request the option explicitly. The library default is false. Two frame slots,
supported RGBA8 targets, a compute-capable queue for certificates and sufficient
preparation budgets are required. SDF raster reuse does not prepare the suffix
painter or its finite blend table. A rejected
candidate always renders the complete program.

The renderer prepares fixed operation metadata and two bounded CPU payload arenas.
Equal operation payloads retain their existing canonical bytes instead of copying
vertices, indices, shapes and packed spans again. A changed suffix writes only
after the equal prefix; arena recycling never overwrites a live prefix.
Canonical comparisons include the original vertices, indices, shapes, shape-span
mapping, clips, target generation, texture content revisions and producer order.
CPU history lives in ordinary CPU memory: reading mapped GPU upload pages was
measurably slower. Capacity exhaustion renders normally without growing history
from a draw call. Texture mutation, external images, resource retirement and a
submitted prefix invalidate the candidate.

After eight consecutive non-repeating programs, the renderer pauses canonical
recording for 120 frames and draws normally. It then observes consecutive frames
again, allowing a settled page to recover reuse. The pause bounds wasted CPU
copies on animated pages and changing performance HUDs. It neither skips a draw
nor treats an old certificate as evidence for the intervening frames. History
miss counters cover recorded candidates; frames outside admission render fully.

Identical geometry alone cannot certify blur scratch or a translucent LOAD pass.
After a program repeats, a GPU probe compares the full RGBA8 bytes of its stateful
outputs before and after execution. Full overwrites before any read remove an
image's initial-state dependency. A four-byte asynchronous completion result
certifies a fixed point; no scene pixels are read back to the CPU. Changing
geometry, inputs, output revisions or target lifetime discards that certificate.
An accumulating alpha pass continues rendering until its actual state stops
changing. The probe's bounded snapshot buffer, comparator pipeline and descriptor
sets are prepared before drawing and fall back to full rendering on budget/format
exhaustion. Preparing them only after a repeated LOAD program was observed moved
a compiler/allocation spike into a later frame in testing; that candidate was
rejected. Cache preparation remains outside the drawing loop.

A certified clear/raster program defers equal operations before upload, vertex
conversion, descriptors and GPU command recording. On a changed operation,
shortened tape or resource side effect, the renderer first replays the deferred
prefix in order with complete raster output, then executes the changed suffix.
This replay borrows prepared canonical CPU data and does not allocate. Other
effect kernels retain the conservative record/discard route.

A certified repeated program preserves its completed targets and submits only
presentation. Raster draws, binning, suffix painting and effect replay are skipped;
input, layout, animation and interaction still execute normally. Statistics keep
requested geometry separate from actual draws and expose `whole_program_hits`,
`whole_program_misses`, `whole_program_probes` and the probe's pixel count. GPU
timestamps include the real probe and presentation costs. Resource release waits
for completion; readback and window/target recreation retain their normal contract.

## Kawase blur

Set `vulkan_device_opts::blur = vulkan_blur_mode::KAWASE`, or run
`pui_showcase_vulkan --vulkan-blur kawase`. The standalone `pui_ex_vulkan_blur`
selects it by default. `LEGACY` remains the default for all other applications;
the core/SDL implementation and the existing legacy golden images are unchanged.

This native-only filter uses the dual-filter variant of Kawase: a weighted
five-tap downsample and eight-tap upsample over two to four levels. The weight
patterns follow [Arm's dual-filter presentation](https://community.arm.com/cfs-file/__key/communityserver-blogs-components-weblogfiles/00-00-00-20-66/siggraph2015_2D00_mmg_2D00_marius_2D00_notes.pdf).
The final image is half resolution. Radius controls pyramid depth and a bounded
sample offset; its appearance differs from the legacy blit approximation.

A surface prepares four reusable images and completed frame slots prepare 64
descriptor sets. Blur recording uses the already-rendered scene prefix, orders
sample/storage accesses, and dispatches only the requested region. Intermediate
regions use ceil division and clamp samples to their valid texel centers, so odd
sizes cannot sample unwritten or stale storage. The usual alpha, rounded composite,
active clip, overlay exclusion and logical-to-pixel scaling still apply. The
Kawase path works with the reference, SDF raster and compute painters. Cache replay
must match its independent SDF raster output exactly; it has separate validation
from the immutable legacy golden domain. Descriptor exhaustion waits and reuses
storage instead of growing it during a blur call.

Kawase is an opt-in quality/performance experiment. Its speed depends on radius,
covered area and GPU; neither compute painting nor this blur is automatically
faster than the corresponding raster/legacy path. Compare completed GPU time and
resident allocation totals separately from CPU frame time and uncapped GPU usage.

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

The example bootstrap reads `PUFFERUI_VULKAN_VALIDATION` once at startup.
Unset/empty, `0`, `false`, `off` and `no` disable the application's validation
request (case insensitive); `1` enables it. Other nonempty values preserve the
previous opt-in behavior. Previously even `0` enabled synchronization validation.
Loader/SDK settings can independently force layers; this flag does not override
them. Performance comparisons record the requested configuration separately.

The backend prefers a compatible discrete physical device, or uses the explicit
`vulkan_device_opts::physical_device_index`. This cannot expose a device whose
driver manifest is missing from the system loader's discovery. On this machine,
the RX 9070 XT registry entry points to a nonexistent manifest. Qualification
uses an existing AMD package only in the child process environment:

```powershell
$env:VK_DRIVER_FILES = 'C:\Windows\System32\DriverStore\FileRepository\amdvlk.inf_amd64_865a9c4fff871fca\amd-vulkan64.json'
out/build/x64-release/Release/pui_showcase_vulkan.exe
```

This path is a machine-specific qualification receipt, not a library setting or
a portable driver location. The backend never modifies driver registration.
`VK_DRIVER_FILES` is the loader's documented process-level driver selection
mechanism ([Khronos driver discovery](https://github.com/KhronosGroup/Vulkan-Loader/blob/main/docs/LoaderDriverInterface.md)).
For measured runs, `tools/compare_vulkan.py --require-device "AMD Radeon RX 9070 XT"`
fails if any Spiral Vulkan run reports a different physical device.

## Actual showcase frame diagnostics

`pui_showcase_vulkan --vulkan-painter sdf --vector-text --perf-trace NEW.csv`
records the real application's page/settings, item/effect counts, requested and
executed draws, upload/resource counters and native presentation waits. The file
must not exist. The recorder prepares a bounded arena before the UI starts and
does not allocate or write the file while observing a frame. Close the application
normally to flush the CSV. It preserves the first 2048 observations and the latest
63488 observations; stderr explicitly reports any dropped middle frames.

`loop_ms` is the interval between consecutive showcase callback starts, including
event pumping and native presentation. The model and `build_ms` describe the
previous callback. Native cumulative counters are differenced at these boundaries;
GPU completion counters are delayed and `gpu_samples` must accompany their time.
The final rendered frame has no next callback and is explicitly unobserved. This
is a live diagnostic rather than the separate all-frame benchmark protocol. No
notes, card titles or other user text are recorded. The default without the flag
does not prepare recorder storage.

To open the reported scene directly, add `--page 1 --tiles 500 --lab-mode 2
--lab-size 64`. The same normal application remains interactive. Initial layout
selection and tile creation run before the UI loop; these flags do not alter the
default scene. Layout mode numbers are 0 Grid, 1 List, 2 Masonry, 3 Orbit and 4
Spiral. Tile count accepts 14..5000 and size accepts 64..180. VSync retains its
normal startup policy and HUD control.

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

`upload_budget_bytes` defaults to 256 MiB and bounds actual Vulkan allocation
requirements for upload pages, optional local geometry and temporary readback.
Completed slots are reclaimed first; backpressure waits for necessary work
instead of growing without limit. One original draw remains contiguous. Large
geometry pages do not replace the small upload pages. Non-coherent mappings are
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

Optional analytic shapes, HarfBuzz/Slug text, GPU command binning and compute
painting are described in [the GPU painter contract](vulkan_gpu_painter.md).
REFERENCE remains the default. The independent SDF raster path is the visual
reference for compute/cache; existing bitmap goldens are unchanged. The original
[detailed architecture](vulkan_compute_architecture.md) retains the design and
qualification protocol. Compute is opt-in and is not assumed faster than raster.
