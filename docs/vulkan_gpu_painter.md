# Analytic and integrated Vulkan painters

`INTEGRATED` combines native command operands, ordered GPU lists, exact RGBA8
output reuse, a native blend-function cache, Slug source-alpha residency, producer
dependencies, shape unions and a common-opacity scope. All Vulkan device factories
and the Vulkan showcase select it by default. HarfBuzz/Slug is enabled when built;
without HarfBuzz, integrated shapes use bitmap text. Explicit diagnostic modes
retain independent configurations. See [the integrated design](vulkan_integrated_painter.md)
and [the measurement and visual validation report](perf_vulkan_integrated.md).

With whole-program caching enabled, unchanged native operands are observed
without also encoding/running the tile painter. A GPU fixed-point certificate
covers every LOAD-dependent output before replay may skip the complete program.
Failed certificates select fine-grained reuse; its kernels, function atlas,
arenas and histories are prepared at the next completed frame boundary. Forced
tile mode remains available for numerical and coverage regressions. Group,
alpha and producer handling belongs to the same renderer in both paths.

The sections below describe the independent `SDF_RASTER`/`COMPUTE_SDF` paths.

The diagnostic `REFERENCE` painter retains the established triangle and bitmap
renderer. `COMMAND_SHADOW` captures a private command tape while retaining that
renderer. `SDF_RASTER` replaces rounded fill fans with analytic quads and is the
independent visual reference for `COMPUTE_SDF`. SDF coverage and the explicitly
requested new font rendering can change the picture; old committed goldens are
never updated to hide these differences.

## Build and selection

Configure `PUFFERUI_WITH_VULKAN=ON`. Optional
`PUFFERUI_WITH_HARFBUZZ=ON` adds shaping and Slug curves; see
[font ownership, upstream provenance and limits](harfbuzz.md).

The default integrated configuration validates RGBA8 storage-image support and
at least 16 storage-buffer bindings per stage, including when fine-cache kernels
are deferred. Integrated vector text also requires sampled/storage `R32_UINT`.
These requirements are additional to the base Vulkan 1.3 device contract.

```cpp
pui::vulkan_device_opts options;
options.painter = pui::vulkan_painter_mode::SDF_RASTER;
options.vector_text = true; // requires the optional HarfBuzz build
options.tile_cache = options.whole_program_cache = false; // independent raster oracle
auto *device = pui::create_vulkan_device(options);
```

Set `painter=COMPUTE_SDF` to force compute eligibility. Re-enabling `tile_cache=true` adds
GPU history comparison. `adaptive_compute=true` allows conservative bypass:
cache must be enabled, at least 2048 commands must be present, and the batch
must contain only opaque supported commands. Legacy triangles, Slug glyphs,
translucency and small batches select SDF raster. Completed coverage below 75%
starts a 512-batch cooldown. This is a conservative heuristic with asynchronous
coverage feedback, not a learned GPU timing model or a guarantee of speedup.
Forced mode remains available so measurements cannot conceal compute costs.

The showcase accepts `--vulkan-painter reference|sdf|compute|adaptive|integrated` and
`--vector-text`. `pui_ex_vulkan_painter` is the standalone analytic example.
Interactive VSync startup policy is unchanged. Benchmark executables alone
disable VSync and use a hidden native WSI window.

## Geometry and ordered commands

The private `primitive_sink` records bounded 64-byte command headers. Rounded
fills bypass fan construction before tessellation, using four vertices and six
indices; core/SDL never activate this sink. Core vertex ABI remains 20 bytes.
The native SDF vertex format adds a shape index; there are no magic UV markers.
Rounded gradients retain the underlay and top layer, including their alpha.
Unsupported primitives retain their original triangle stream and submission
order. The shader uses the same SDF function for raster and compute.

Forced compute groups consecutive native draws into ordered programs. Texture
and scissor changes retain their native packets inside the same program; command
bounds use the exact integer native scissors. Target changes, copies, effects,
resource mutation, readback and submissions close the program. No final-frame
cache is substituted for a blur's prefix. Adaptive routing keeps its inexpensive
native-batch eligibility path. Texture/image glyphs and
arbitrary triangles are conservative unknown commands. The shader conservatively
invalidates compute ownership under their expanded bounds. One physical pixel
of expansion covers raster subpixel snapping at tile boundaries.

## GPU stages and numerical compatibility

The backend uploads current headers and shapes, then dispatches coarse bitset
construction, fine counting, prefix scan and ordered index emission. Lists
retain painter order. GPU-generated indirect arguments select painting work.
Capacity overflow leaves the route mask empty and the original raster program
paints the complete result; no truncated list is presented as a valid picture.

For each pixel the compute interpreter searches for a fully covered opaque
source that makes the remaining suffix independent of the previous destination.
It can then evaluate later fully covered RGBA8 fills. Partial coverage, Slug
curves, unknown geometry or unsafe edges revoke ownership until another opaque
supported source establishes it. Compute writes qualified pixels and their route
bits; the graphics shader replays the entire ordered program for other pixels.
The ownership mask is common to every primitive in that draw, so the two routes
never blend the same program twice.

Floating-point emulation of native RGBA8 blending was not byte-exact. Device
creation calibrates the finite source/destination/alpha domain using the native
graphics pipeline and a GPU-generated 16,842,752-byte lookup table. No CPU scene
readback or guessed rounding formula is used. Calibration costs startup time
and memory; benchmarks include process startup separately. Fractional coverage
and textured products remain on graphics. Calibration is specific to the
active device/driver; there is no cross-driver pixel equality claim.

## GPU cache and lifetime

Every completed frame slot owns reusable binning jobs and immutable previous
headers, shapes, ordered indices, hashes, route bits and saved pixels. Reuse may
compare a slot from two frames earlier; exact equality makes immediate previous
frame identity unnecessary. Two 32-bit hashes reject differences quickly;
full ordered command/payload comparison remains mandatory after a hash match.
Clip bits and the full target generation are compared separately, including
under forced hash collisions. Slug/unknown commands cannot supply cached colors;
their conservative invalidation region remains part of the ordered program.

GPU comparison produces dirty flags; compaction builds the dirty list and
indirect dispatch. Clean qualified pixels are restored from immutable history.
Dirty tiles run the interpreter. History is committed only after a valid list;
all prefix-dependent pixels still receive the full native draw. This is a
specialized suffix cache, not a general arbitrary-triangle retained renderer.
The separate opt-in whole-program cache additionally requires exact canonical
CPU identity and a certified fixed point for stateful output images; see
[whole native program reuse](vulkan.md#whole-native-program-reuse). It does not
infer pixel equality from a hash or freeze an accumulating translucent pass.

Actual Vulkan allocation requirements count against the 64 MiB GPU metadata
budget, including the blend table. Saved image allocations have the separate
`cache_budget_bytes` budget (64 MiB by default). A history allocation shortfall
disables cache for that job; a metadata/list shortfall selects raster for that
draw. Explicit GPU/OOM/device failures retain the existing error contract.

Shape metadata, Slug blobs and vertex/index data use one upload reservation.
A budget-induced submission can occur before that reservation but cannot free
metadata between descriptor setup and its consuming draw. Descriptor pools reset
only after the owning slot completes. Compute/transfer/graphics dependencies and
image layouts are explicit. There is no scene/dirty-list CPU readback. Delayed
completed control counters provide telemetry and the optional adaptive coverage
heuristic. The separate whole-program cache also consumes an exact, four-byte
GPU fixed-point certificate after its submission completes; it never infers
unchanged pixels from geometry or a hash alone.

## Validation and measurement

Fine list construction uses a portable 64-lane workgroup per tile. Lanes count
candidate bitset words together and emit ordered chunks with exclusive prefixes;
the list remains in submission order. Prefix scanning and dirty compaction use
bounded workgroup scans, with separate overflow propagation. Two-dimensional
dispatches respect both advertised workgroup count limits. None of these paths
adds a GPU allocation or relies on an optional subgroup size/feature.

The suffix painter retains forward ordered blending with calibrated finite
RGBA8 rounding. A reverse opaque-source search followed by forward suffix
replay was tested separately and rejected: this scene's translucent layers
increased compute GPU time by 3.4%. Fewer evaluated hidden sources alone did not
establish a speedup. The suffix cache remains conservative for unknown/AA pixels;
it is not a general full-scene painter and still needs native raster replay.

`tools/profile_vulkan_painter.py` builds an optional Windows diagnostic against
the current implementation or `--implementation-root` of a frozen compatible
tree. It records bounded per-dispatch/draw GPU timestamps and ordinary/aligned
C++ allocation stacks plus this executable's C malloc/calloc/realloc imports,
resolving symbols after rendering. An interposition selftest checks all five
paths without double-counting C++ new. Other DLL/driver heap, aligned C heap and
free/liveness tracing remain outside its scope. Its extra queries and stack
capture affect timing; use the separate ordinary benchmark for acceptance.
Use `--heap-only --program` for whole-program cache workloads. These can discard
an unsubmitted command buffer, so per-command timestamp queries would never
execute; the heap-only mode deliberately installs no such queries.

`pui_vulkan_program_bench` adds settled and local-change variants of that actual
showcase. `--idle` freezes time and disables Adds; `--local-change` changes the
first tile's hue at 90/120/150. `tools/compare_vulkan_program.py` alternates frozen
executables, checks every semantic geometry tuple and retains all frame timings,
process elapsed time, Windows process CPU/cycles and the CPU/cycles of the whole
frame window. Startup and shutdown remain in the process metrics. GPU readback
is an independent `--reference` validation mode and is excluded from timings.
Both executables must use the same compiler, optimization, linker and heap
manifest settings; a default `/DEBUG` link changes optimization defaults.

`tools/measure_vulkan_gpu.py` samples Windows per-process GPU engine and memory
counters with native VSync requested and an explicit, identical 60 Hz harness
cap. It preserves every raw sample/status, adapter LUID, physical engine, focus
and visibility flag. It never averages distinct adapters or engines into one
utilization number. Requested VSync or a visible window alone does not establish
foreground pacing. Compare usage percentages only at matched frame rates;
uncapped throughput can increase utilization even when each frame costs less.

Existing core, SDL, Vulkan, interaction, package and bitmap golden tests remain
unchanged. New command/binning/cache tests include hash collisions, removed
commands, clip/target changes, fractional tile edges, dispatch-limit overflow,
tiny upload budgets, native/staged equality and actual compute/cache use.
HarfBuzz tests include ligatures, joining, combining marks, font-store relocation
and overlay bitmap fallback from the actual shaped glyph ID.

`pui_vulkan_compute_spiral_tests` uses the actual showcase at 1360x860, size64,
1150 or 5000 initial tiles, 180 fixed-dt frames and actual keyboard Adds at
90/120/150. Capture mode refuses existing directories or files. Freeze the old
REFERENCE and new SDF/Slug raster captures separately; compare compute/cache
and adaptive against the latter. Every frame includes all measured peak indices
and their neighbours. `--long-run` adds a separate 3600-frame/60-Add continuation.
`--growth` separately grows the model from zero to 5000 items. `--golden`
checks all immutable native fingerprints in `tests/golden/vulkan_painter_rx9070xt.inl`;
it requires the explicitly qualified adapter/driver and visual mode, and fails
outside that domain rather than claiming cross-driver parity.

```powershell
python tools/run_tests.py --vulkan --format
python tools/run_tests.py --vulkan --release --format
python tools/compare_vulkan_compute.py --baseline OLD/pui_vulkan_spiral_tests.exe --candidate NEW/pui_vulkan_painter_bench.exe --output NEW_DIRECTORY --runs 7
```

The timing tool retains every raw frame, process elapsed time, maxima and the
union of each run/mode's top ten frames plus two neighbours. R0/A-A/current
REFERENCE/SDF/forced compute/cache/adaptive remain separate series. GPU timestamp
totals are delayed completed submissions, not a per-stage frame attribution.
Do not add them to overlapped CPU time or infer a universal frametime guarantee.
