#include "vulkan_test_options.h"
// Exact full native-output cache: unknown meshes, images, bitmap/Slug edges,
// LOAD pixels, late texture updates, state changes and bounded native fallback.
#undef near
#undef far
#undef small
#include <cstdio>
using namespace pui;
namespace
{
constexpr u32 WIDTH = 128, HEIGHT = 96, PIXELS = WIDTH * HEIGHT, FRAMES = 40;
u32 errors = 0;
void diagnostic(i32 code, const char *message, void *)
{
    std::fprintf(stderr, "Vulkan%d %s\n", code, message);
    ++errors;
}
struct fixture
{
    render_device *device = nullptr;
    render_surface *surface = nullptr;
    context *ctx = nullptr;
    window *win = nullptr;
    texture_handle image = nullptr;
    font_handle font = FONT_INVALID;
    std::vector<u8> pixels, base;
    bool initialize(render_device *owner)
    {
        device = owner;
        surface = create_vulkan_surface(device, {.width = WIDTH, .height = HEIGHT});
        if (!surface) return false;
        ctx = create_context(device, surface);
        win = add_window(ctx, nullptr, surface, {0, 0, WIDTH, HEIGHT});
        font = load_font(ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
        pixels.resize(PIXELS * 4);
        base.resize(PIXELS * 4);
        for (u32 pixel = 0; pixel < PIXELS; ++pixel)
        {
            base[pixel * 4] = static_cast<u8>(pixel * 37);
            base[pixel * 4 + 1] = static_cast<u8>(pixel * 13);
            base[pixel * 4 + 2] = static_cast<u8>(pixel * 101);
            base[pixel * 4 + 3] = static_cast<u8>(127 + pixel % 128);
        }
        u8 source[8 * 8 * 4];
        for (u32 pixel = 0; pixel < 64; ++pixel)
        {
            source[pixel * 4] = static_cast<u8>(pixel * 53);
            source[pixel * 4 + 1] = static_cast<u8>(pixel * 11);
            source[pixel * 4 + 2] = static_cast<u8>(pixel * 31);
            source[pixel * 4 + 3] = static_cast<u8>(40 + pixel * 3);
        }
        image = device->create_texture(8, 8, source);
        return font != FONT_INVALID && image;
    }
    bool render(u32 frame)
    {
        set_current_context(ctx);
        auto &d = *static_cast<vk_detail::device *>(device);
        d.sdf_cache_faults = frame == 26 ? 3u : 1u; // force collisions throughout
        if (frame == 22) ++static_cast<vk_detail::texture *>(surface->scene_target())->generation;
        if (frame == 10) base[((HEIGHT - 1) * WIDTH + WIDTH - 1) * 4] ^= 127;
        if (frame == 12)
        {
            const u8 updated[4] = {37, 211, 93, 151};
            device->update_texture(image, 3, 4, 1, 1, updated);
        }
        begin_frame(ctx, *win, 0, 1.0 / 60.0);
        device->update_texture(surface->scene_target(), 0, 0, WIDTH, HEIGHT, base.data());
        if (frame == 30) d.sdf_tape.payload_limit = 0;
        {
            ui u(ctx);
            if (frame == 16 || frame == 17) detail::clip_push(ctx, {0, 0, 96, HEIGHT});
            auto text_style = u.text_style(18, font);
            u.draw_rounded_rect({7.25f, 6.5f, 77.5f, 31.75f}, {231, 49, 107, 137}, 8.25f);
            if (frame < 20)
                u.text({12.375f, 5.75f, 78, 25}, "Ag Q Î© Å", {241, 217, 183, 173});
            vertex mesh[] = {{8, 40, 0, 0, {230, 17, 71, 127}},
                             {frame < 8 ? 40.0f : 29.0f, 40, 0, 0, {13, 211, 57, 199}},
                             {40, 72, 0, 0, {31, 73, 227, 89}},
                             {8, 72, 0, 0, {239, 197, 29, 171}}};
            if (frame >= 6) mesh[0].c.g = 149;
            const i32 original[] = {0, 1, 2, 0, 2, 3}, changed[] = {0, 1, 3, 1, 2, 3};
            u.draw_triangles(nullptr, mesh, 4, frame < 14 ? original : changed, 6);
            u.image({98, 4, 24, 24}, {image, 8, 8});
            // The mesh was recorded with the old revision, but its buffered
            // native draw observes this update. Submission-time sealing matters.
            if (frame == 32 || frame == 34)
            {
                const u8 updated[4] = {static_cast<u8>(frame * 7), 41, 201, 239};
                device->update_texture(image, 1, 1, 1, 1, updated);
            }
            if (frame == 16 || frame == 17) detail::clip_pop(ctx);
        }
        end_frame(ctx);
        return !violation_count(ctx) && vulkan_last_result(device) == VK_SUCCESS &&
               device->read_pixels(surface->scene_target(), 0, 0, WIDTH, HEIGHT, pixels.data());
    }
    void release()
    {
        if (image) device->destroy_texture(image);
        if (ctx) destroy_context(ctx);
        if (surface) device->destroy_surface(surface);
        device = nullptr;
        ctx = nullptr;
        surface = nullptr;
        image = nullptr;
    }
};
} // namespace
#if !defined(PUFFERUI_OUTPUT_CACHE_HELPERS_ONLY)
#define REQUIRE(x)                                                                                 \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            std::fprintf(stderr, "FAIL line%d %s\n", __LINE__, #x);                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
int main(int argc, char **argv)
{
    bool staged = false, vectors = false, flattened = false;
    for (i32 i = 1; i < argc; ++i)
        if (!std::strcmp(argv[i], "--staged"))
            staged = true;
        else if (!std::strcmp(argv[i], "--vector-text"))
            vectors = true;
        else if (!std::strcmp(argv[i], "--flattened"))
            flattened = true;
        else
            return 1;
    vulkan_device_opts options = test_detail::reference_options();
    options.painter = vulkan_painter_mode::INTEGRATED;
    options.tile_cache = true;
    options.validation = true;
    options.error_callback = diagnostic;
#if defined(PUFFERUI_ENABLE_HARFBUZZ)
    options.vector_text = vectors;
#endif
    options.upload_mode = staged ? vulkan_upload_mode::STAGED : vulkan_upload_mode::DIRECT;
    auto *candidate = create_vulkan_device(options);
    REQUIRE(candidate);
    auto &d = *static_cast<vk_detail::device *>(candidate);
    if (flattened) d.properties.limits.maxComputeWorkGroupCount[0] = 3;
    const auto native = vulkan_handles(candidate);
    options.native = &native;
    options.painter = vulkan_painter_mode::SDF_RASTER;
    options.tile_cache = false;
    auto *reference = create_vulkan_device(options);
    REQUIRE(reference);
    fixture cases[2];
    REQUIRE(cases[0].initialize(candidate) && cases[1].initialize(reference));
    std::vector<u8> previous;
    u64 restored = 0, all_hit_frames = 0;
    for (u32 frame = 0; frame < FRAMES; ++frame)
    {
        const auto before = vulkan_statistics(candidate);
#if defined(PUFFERUI_VULKAN_OUTPUT_CACHE_TESTS)
        const u64 replayed_before = d.native_replay_counts[0],
                  skipped_before = d.native_replay_counts[1];
#endif
        REQUIRE(cases[0].render(frame) && cases[1].render(frame));
        if (cases[0].pixels != cases[1].pixels)
        {
            usize at = 0;
            while (cases[0].pixels[at] == cases[1].pixels[at]) ++at;
            std::fprintf(
                stderr,
                "native output frame=%u mismatch=(%zu,%zu) channel=%zu actual=%u native=%u\n",
                frame, (at / 4) % WIDTH, (at / 4) / WIDTH, at % 4, cases[0].pixels[at],
                cases[1].pixels[at]);
            return 1;
        }
        if (frame == 6 || frame == 8 || frame == 10 || frame == 12 || frame == 14 || frame == 16 ||
            frame == 18 || frame == 20 || frame == 32 || frame == 34)
            REQUIRE(previous != cases[0].pixels); // every negative fixture has visible work
        previous = cases[0].pixels;
        const auto after = vulkan_statistics(candidate);
        const u64 cached = after.gpu_sdf_cached_pixels - before.gpu_sdf_cached_pixels;
        restored += cached;
        if (cached == PIXELS) ++all_hit_frames;
        if (frame == 5 || frame == 39)
        {
            if (cached != PIXELS)
                std::fprintf(
                    stderr,
                    "frame=%u full cache got=%llu expected=%u hits=%llu misses=%llu batches=%llu\n",
                    frame, cached, PIXELS, after.gpu_sdf_cache_hits - before.gpu_sdf_cache_hits,
                    after.gpu_sdf_cache_misses - before.gpu_sdf_cache_misses,
                    after.gpu_sdf_batches - before.gpu_sdf_batches);
            REQUIRE(cached == PIXELS); // includes all native AA/text/unknown/base pixels
#if defined(PUFFERUI_VULKAN_OUTPUT_CACHE_TESTS)
            REQUIRE(d.native_replay_counts[0] == replayed_before &&
                    d.native_replay_counts[1] > skipped_before);
            auto *surface = static_cast<vk_detail::surface *>(cases[0].surface);
            u64 latest = 0;
            for (const auto &slot : surface->slots)
                for (auto *job : slot.sdf_jobs) latest = std::max(latest, job->bin.serial);
            u32 checked = 0;
            for (const auto &slot : surface->slots)
                for (auto *job : slot.sdf_jobs)
                {
                    if (job->bin.serial != latest) continue;
                    if (!job->state.coherent)
                    {
                        VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
                        range.memory = job->state.memory;
                        range.size = VK_WHOLE_SIZE;
                        REQUIRE(d.vkInvalidateMappedMemoryRanges(d.native.device, 1, &range) ==
                                VK_SUCCESS);
                    }
                    const auto *args = reinterpret_cast<const VkDrawIndexedIndirectCommand *>(
                        job->state.mapped + static_cast<u64>(job->bin.parameters[11]) * 16);
                    for (u32 packet = 0; packet < 64; ++packet)
                        if (args[packet].indexCount)
                        {
                            REQUIRE(!args[packet].instanceCount && !args[packet].firstIndex &&
                                    !args[packet].vertexOffset && !args[packet].firstInstance);
                            ++checked;
                        }
                }
            REQUIRE(checked > 0);
#endif
        }
    }
    REQUIRE(vulkan_wait_idle(candidate) && vulkan_wait_idle(reference));
    const auto stats = vulkan_statistics(candidate);
    REQUIRE(restored && all_hit_frames && !stats.validation_errors && !errors);
    REQUIRE(!stats.draw_memory_allocations && !stats.draw_resource_creations);
    cases[1].release();
    cases[0].release();
    destroy_vulkan_device(reference);
    destroy_vulkan_device(candidate);
    std::fprintf(stderr,
                 "native_output frames=%u compared_bytes=%llu all_hit_frames=%llu "
                 "restored_pixels=%llu staged=%u vector=%u flattened=%u validation_errors=%u\n",
                 FRAMES, u64(FRAMES) * PIXELS * 4, all_hit_frames, restored, staged, vectors,
                 flattened, errors);
    return 0;
}
#undef REQUIRE
#endif
