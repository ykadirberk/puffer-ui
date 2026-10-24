# Optional analytic Vulkan painter

The default `REFERENCE` painter retains the established triangle and bitmap
renderer. `COMMAND_SHADOW` captures a private command tape while retaining that
renderer. `SDF_RASTER` replaces rounded fill fans with analytic quads and is the
independent visual reference for `COMPUTE_SDF`. SDF coverage and the explicitly
requested new font rendering can change the picture; old committed goldens are
never updated to hide these differences.

## Build and selection

Configure `PUFFERUI_WITH_VULKAN=ON`. Optional
`PUFFERUI_WITH_HARFBUZZ=ON` adds shaping and Slug curves; see
[font ownership, upstream provenance and limits](harfbuzz.md).

```cpp
pui::vulkan_device_opts options;
options.painter = pui::vulkan_painter_mode::SDF_RASTER;
options.vector_text = true; // requires the optional HarfBuzz build
auto *device = pui::create_vulkan_device(options);
```

Set `painter=COMPUTE_SDF` to force compute eligibility. `tile_cache=true` adds
GPU history comparison. `adaptive_compute=true` allows conservative bypass:
cache must be enabled, at least 2048 commands must be present, and the batch
must contain only opaque supported commands. Legacy triangles, Slug glyphs,
translucency and small batches select SDF raster. Completed coverage below 75%
starts a 512-batch cooldown. This is a conservative heuristic with asynchronous
coverage feedback, not a learned GPU timing model or a guarantee of speedup.
Forced mode remains available so measurements cannot conceal compute costs.

The showcase accepts `--vulkan-painter reference|sdf|compute|adaptive` and
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

Each actual device draw is an epoch. Target, clip, texture update, copy, blur
and overlay transitions retain their established device-call boundaries. No
final-frame cache is substituted for a blur's prefix. Texture/image glyphs and
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

Actual Vulkan allocation requirements count against the 64 MiB GPU metadata
budget, including the blend table. Saved image allocations have the separate
`cache_budget_bytes` budget (64 MiB by default). A history allocation shortfall
disables cache for that job; a metadata/list shortfall selects raster for that
draw. Explicit GPU/OOM/device failures retain the existing error contract.

Shape metadata, Slug blobs and vertex/index data use one upload reservation.
A budget-induced submission can occur before that reservation but cannot free
metadata between descriptor setup and its consuming draw. Descriptor pools reset
only after the owning slot completes. Compute/transfer/graphics dependencies and
image layouts are explicit. There is no scene/dirty-list CPU readback; delayed
completed control counters are harvested solely for telemetry and the optional
adaptive coverage heuristic.

## Validation and measurement

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
