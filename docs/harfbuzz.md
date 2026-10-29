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

`glyph_coverage_budget_bytes` defaults to 4 MiB; zero disables the exact native
coverage cache. It uses one R32 float image, never an R8/approximate distance
field. Its identity includes the outline chunk/location, exact pixel bounds,
absolute baseline, scale and raw gamma bits. Absolute positions are retained:
equal fractional phases alone do not prove identical float subtraction.
Tint and clipping stay on each original glyph draw. Pixel centers are rebuilt
with integer coordinates when filling a cache rectangle; the final draw uses
the original quad, order and straight-alpha blending with `texelFetch` coverage.

New operands first draw directly. Repeated exact operands request an image at
the next surface frame boundary, and admitted glyphs evaluate Slug once into
that image instead of drawing Slug and also evaluating it for the cache. The
four bounded image regions retire by timeline: a retiring region temporarily
uses direct Slug until its previous reads complete. Reuse changes its generation.
A reclaimed region admits new glyphs before an older pressure report can retire
it again, so a full cache can recover without losing its new admissions.
No cache eviction waits for the GPU or allocates during glyph submission.
Oversized/changing text, unsupported format features, allocation granularity
above the budget and metadata misses retain direct native Slug. Real Vulkan
allocation errors remain explicit failures. `image_bytes` includes the cache;
`glyph_coverage_bytes` exposes its actual allocation separately. Internal scratch
does not create a public texture handle or a bitmap text-atlas page.

Short native runs use bounded 8192-entry probe tables with exact text/font/size
identity. Per-font shaping caches keep glyph IDs, clusters and advances/offsets
in font units, so a size change repeats the original per-glyph scaling and sum
without calling HarfBuzz again. The font-unit logical payload budget is 4 MiB
per font; the scaled-layout payload budget is 32 MiB per context. Table metadata
and bounded pool rounding are additional storage. Font objects and their tables
are prepared at `load_font`; cold glyph outlines stay lazy. Long strings and
budget misses keep the original route. Bitmap quantized-size eviction history
and custom-shaper behavior remain independent.

Geometry uploads have separate reusable per-slot pages. Surface creation
prepares the initial page. Each surface retains its observed per-frame byte
high-water mark and prepares the next reusable slot at the frame boundary;
steady small scenes do not repeatedly wait for a too-small initial arena.
Ordinary slots start at 512 KiB and automatic preparation leaves bounded growth
headroom. Shape vertices upload their original 20 bytes directly. Integer byte
attributes reconstruct the exact original binary32 channel values in the vertex
shader, including division rounding; UNORM conversion is not substituted.
`compact_vertices=false` keeps the independent original 32-byte float-color
upload as a diagnostic/reference path.
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
`pui_vulkan_native_optimization_tests` additionally compares compact/float colors
and 192 coverage/uncached frames per upload mode. GPU copies capture those
frames before presentation and CPU comparison waits until the entire sequence
finishes, preserving the ordinary frames-in-flight lifetime test. CPU checks
cover 5000 labels, size churn, font identity and real shaping-call counts.

Paragraph bidi and cluster-aware editing are not implied. A missing primary
glyph uses the established fallback-font path. Popup overlay recorders retain
the actual shaped glyph ID when they need bitmap replay. Linux fallback paths
are source-covered; runtime qualification here is on Windows.
