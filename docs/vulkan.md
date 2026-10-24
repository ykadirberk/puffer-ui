# Native Vulkan renderer

PufferUI's optional Vulkan 1.3 backend implements `render_device` and
`render_surface`. SDL3 still manages windows, input, clipboard, system chrome,
accessibility and popup overlays. Vulkan records and presents the graphics;
this route does not create an `SDL_Renderer`. The existing SDL renderer remains
the default. Core-only and SDL-only applications need no Vulkan SDK.

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
