# Native HarfBuzz and Slug text

The optional Vulkan library defaults to HarfBuzz shaping and upstream Slug GPU
outline rendering. Shapes keep the first Vulkan backend's triangle mesh and
blur pyramid. Text has a separate graphics pipeline; it uses neither shape SDFs
nor compute painting. Core/SDL retain their bitmap atlas and dependencies.

HarfBuzz 15.0.0 is pinned to
`35f656a1a2bab548ea7f54a6dd3fba2b09680d14`. CMake uses its `harfbuzz` and
`harfbuzz-gpu` targets. The upstream fragment coverage implementation and complete
license are preserved in `src/shaders/vulkan/harfbuzz_slug.glsl` and
`docs/licenses/harfbuzz-slug.txt`. This is upstream open-source Slug code, not
the proprietary Slug library.

## Configuration and ownership

```powershell
cmake --preset x64-release -DPUFFERUI_WITH_VULKAN=ON -DPUFFERUI_WITH_HARFBUZZ=ON
cmake --build out/build/x64-release --target pui_showcase_vulkan
& .\out\build\x64-release\Release\pui_showcase_vulkan.exe
```

No Python launcher, painter flags or shader compiler are needed to run the
executable. An offline build can set
`FETCHCONTENT_SOURCE_DIR_PUFFERUI_HARFBUZZ` to the pinned checkout. Installed
Vulkan consumers find the installed HarfBuzz CMake package. Core/SDL consumers
do not load that dependency. Explicit `PUFFERUI_WITH_HARFBUZZ=OFF` remains
supported; the native factory uses bitmap text in that build.

`vulkan_device_opts::vector_text` defaults true and enables the native path in
a HarfBuzz build. Set it false for a bitmap comparison. The selected context's
`painter->vector_text()` reports the effective representation. Measurement and
drawing share glyph IDs, clusters, offsets and advances from the same shaped
run. Context-owned font bytes outlive their HarfBuzz objects. Font objects remain
stable when the font table grows. Upstream encoders recycle temporary blobs
after copying them into the owning outline cache.

The examples pass the executable's asset root into the library bootstrap, so
Visual Studio/output-directory launches and project-root launches load the
same bundled DejaVu fonts. Windows and Linux system-font paths are fallbacks.
System fonts can have different metrics; they are not the bundled-font oracle.

## Drawing and lifetime

Each glyph instance is 64 bytes: pixel bounds, baseline, scale, gamma, float
color and an outline location. Vulkan generates the six quad vertices in its
vertex shader. A retained 2048-instance CPU batch flushes before another mesh,
clip, target, clear, readback or submission can change the ordered result.
The tests compare batching with one native draw per glyph, including overlaps.

Encoded outlines live in append-only mapped storage-buffer chunks. Ordinary
chunks are 512 KiB; an oversized outline can get a larger chunk. The device
limits this storage to 16 MiB and 32 chunks, subject to the configured upload
budget and Vulkan storage-buffer limit. A chunk gets one immutable descriptor.
Submitted prefixes are never overwritten. New allocations align to
`nonCoherentAtomSize`; noncoherent appends flush only their disjoint new range.
Repeated text uploads instances rather than copying the outline prefix.
The initial chunk, descriptors and CPU batch are prepared at device creation.
Rare residency exhaustion retains shaped bitmap fallback; device allocation
failures remain explicit errors. Oversized text in pixels still uses its same
scale-independent outline and native Slug coverage.

Geometry uploads have separate reusable per-slot pages. Surface creation
prepares the initial page. Each surface retains its observed per-frame byte
high-water mark and prepares the next reusable slot at the frame boundary;
steady small scenes do not repeatedly wait for a too-small initial arena.
`prepare_geometry` can reserve a larger bounded
working set before drawing. Very large hints are bounded by visible pixels
and the upload budget. Its return value certifies the full requested size;
false permits normal geometric fallback. This is advisory preparation, not a
promise of allocation-free arbitrary workloads.

The original optional retained-pixel cache records mesh operations only. A
native-text frame replays its ordered prefix and bypasses that cache before
the first glyph. There is no command tape or whole-scene/function cache.

## Validation boundaries

The original bitmap suite/goldens remain independent. Native bitmap Spiral
frames compare with the first Vulkan renderer. A separate text-only domain
compares with the previous native Slug shader; it does not substitute a shape
SDF image for a triangle reference. Instanced/scalar, direct/staged and
cache-policy comparisons retain exact RGBA equality. The actual 5000-item
Spiral test checks every one of 180 frames and draw-buffer allocation counters.

Paragraph bidi and cluster-aware editing are not implied. A missing primary
glyph uses the established fallback-font path. Popup overlay recorders retain
the actual shaped glyph ID when they need bitmap replay. Linux fallback paths
are source-covered; runtime qualification here is on Windows.
