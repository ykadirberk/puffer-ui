# Optional HarfBuzz shaping and Slug GPU text

The native Vulkan variant can use HarfBuzz shaping and HarfBuzz's GPU outline
renderer. This is separate from the existing stb bitmap-atlas renderer. Core
and SDL consumers retain their existing implementation and dependencies.

The dependency is HarfBuzz **15.0.0**, pinned to commit
`35f656a1a2bab548ea7f54a6dd3fba2b09680d14`. The project uses the upstream
`harfbuzz` and `harfbuzz-gpu` targets. It does not use the proprietary Slug
library or claim to implement HarfBuzz's coverage algorithm independently.

Build with `PUFFERUI_WITH_VULKAN=ON` and `PUFFERUI_WITH_HARFBUZZ=ON`. The latter
is optional and defaults off. FetchContent checks out the pinned official
source. An offline build may set
`FETCHCONTENT_SOURCE_DIR_PUFFERUI_HARFBUZZ` to an existing checkout of that
commit. Installed Vulkan packages built with this feature find the installed
HarfBuzz CMake package; core/SDL-only consumers do not acquire that dependency.

With HarfBuzz built, `vulkan_device_opts{}` enables `vector_text=true` with the
default integrated painter. Set `vector_text=false` for bitmap text, or explicitly
select `vector_text=true` with another SDF-capable diagnostic painter.
An explicit vector-text request in a build without HarfBuzz fails device
creation with a diagnostic. It does not silently report the legacy renderer
as GPU vector text. Legacy rendering remains selected when the option is false.

The text layout cache stores HarfBuzz glyph IDs, UTF-8 clusters, offsets and
advances. Measuring and drawing share the shaped run. HarfBuzz font objects
borrow context-owned font bytes; objects are destroyed before those bytes.
GPU outlines are encoded once per font/glyph, then cached separately from
size-specific layout. Size and DPI scale the same outline instead of requiring
a new coverage bitmap for every size/subpixel bin.

CPU outline nodes/blobs and shaped vector-text runs use context/font-owned
retained size-class pools. Moves into the layout and victim caches transfer
storage together with its allocator; they do not copy a run into another heap
allocation. Free blocks remain reusable until the owner is destroyed. Oversized
paragraph/outline allocations still use the upstream heap. Cache lookup,
eviction and float metric arithmetic retain their established representation.
Before shaping a vector run, the renderer applies its existing vertical
paint-bounds rejection. Completely clipped labels do not create outlines or
displace visible shaped runs. This early admission uses the exact-size vector
cache domain; legacy quantized-size caches and installed custom shapers retain
their original invocation and cache history. Measurement calls still shape the
requested run.
After copying an encoded outline into its owning cache, the temporary blob is
returned to HarfBuzz's encoder recycling stash. A scope guard also returns it
if the cache allocation throws. Independent fresh-blob encoding regressions
check the retained bytes, extents and cache lifetime.

Encoded outlines are additionally resident in one append-only device-local
buffer shared by frame slots. Coherent host staging transfers only newly
encountered glyphs before their next consuming batch. Repeated text no longer
copies every previously encoded glyph at each batch. Each of the device and
staging buffers reserves at most 512 KiB (and at most one eighth of the upload
budget). A larger collection keeps its complete resident glyph prefix: only
batches that actually reference a glyph beyond that prefix use the existing
per-submission copy path. Returning to an older page keeps its resident glyphs,
and shape-only batches do not copy unrelated outline data. Noncoherent host
memory uses the per-submission path for glyph batches. CPU shaping/layout caches
remain independent. This
changes transfer/lifetime policy, not the outline encoding, coverage shader,
font metrics, DPI or gamma.

`src/shaders/vulkan/harfbuzz_slug.glsl` is the pinned upstream
`src/hb-gpu-fragment.glsl`, with its complete copyright and permission notice.
The Vulkan adapter fetches packed RGBA16I texels from a storage buffer and
supplies the known pixel-to-font-unit scale. The curve coverage algorithm is
upstream Slug code. `shape_common.glsl` shares the coverage entry between the
raster and compute programs. The shader generator records source/include and
SPIR-V hashes as well as per-shader command flags. The complete permission
notice is also retained in the generated/amalgamated shader header and installed
as `share/pufferui/licenses/harfbuzz-slug.txt`.

Current qualification includes actual GPU rendering of the DejaVuSans `ffi`
ligature, an Arabic joining run, a combining-mark cluster, repeated frame-slot
reuse and a check that the vector-text scene creates no bitmap glyph atlas.
The independent 180-frame interaction regression additionally covers a real
text field/caret, scroll, DPI/gamma, popup and surface recreation, in direct and
staged upload modes. All 180 frames at 1150 and 5000 Spiral items compare against
a separately frozen SDF/Slug raster binary and immutable native fingerprints.
A 3600-frame/60-Add continuation also compares compute/cache against raster.
Font-store relocation, shaped-glyph overlay fallback and AddressSanitizer have
separate regressions. See [the qualification report](vulkan_gpu_painter_qualification.md)
for the exact local validation domain and measured costs. These cases do not
certify every Unicode editing operation or platform.

The new text path can change glyph coverage and substitutions as requested.
Existing bitmap goldens are not regenerated to accommodate it. A separate
SDF/Slug raster reference must be captured and frozen before compute/cache
qualification. Cached and uncached new-path images must match exactly.

Limitations must remain explicit: HarfBuzz shaping is not a Unicode paragraph
bidi implementation. The adapter shapes one guessed-script run; mixed scripts
need separate runs and there is no paragraph itemizer. The atomic uses separate
Latin and Arabic calls for that reason. The existing editor's caret model is not automatically
replaced by a cluster-aware editor. A run with a missing primary-font glyph
currently uses the established fallback-font bitmap path. Color-font paint,
full paragraph bidi and cluster editing need their own support and tests;
joining/ligature rendering tests alone do not certify them.

Upstream sources: [HarfBuzz](https://github.com/harfbuzz/harfbuzz),
[GPU API at the pinned commit](https://github.com/harfbuzz/harfbuzz/blob/35f656a1a2bab548ea7f54a6dd3fba2b09680d14/src/hb-gpu.h),
[shader and license notice](https://github.com/harfbuzz/harfbuzz/blob/35f656a1a2bab548ea7f54a6dd3fba2b09680d14/src/hb-gpu-fragment.glsl).
