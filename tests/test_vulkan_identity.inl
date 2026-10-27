#include "vulkan_test_options.h"
// Acceleration tags must not invalidate an unchanged image. Cold uploads are
// initialization, not evidence that an otherwise static program keeps changing.
#define PUFFERUI_OUTPUT_CACHE_HELPERS_ONLY
#include "test_vulkan_output_cache.inl"
#undef PUFFERUI_OUTPUT_CACHE_HELPERS_ONLY

namespace
{
struct changing_glyph_tags : vk_detail::primitive_recorder
{
    u32 epoch = 1, mutations = 0;
    bool glyph(context *c, font_handle font, u32 index, vec2 baseline, f32 scale,
               color tint) override
    {
        const usize before = owner->sdf_shapes.size();
        const bool result = primitive_recorder::glyph(c, font, index, baseline, scale, tint);
        if (result && owner->sdf_shapes.size() > before)
        {
            // An expired cell takes the original Slug path. Changing this
            // generation cannot change the shape, tint or resulting pixels.
            owner->sdf_shapes.back().corners[1] ^= 0x40000000u + epoch;
            ++mutations;
        }
        return result;
    }
};
} // namespace

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
    bool staged = false, tiles_only = false, feedback = false;
    for (i32 i = 1; i < argc; ++i)
        if (!std::strcmp(argv[i], "--staged"))
            staged = true;
        else if (!std::strcmp(argv[i], "--tiles-only"))
            tiles_only = true;
        else if (!std::strcmp(argv[i], "--feedback"))
            feedback = true;
        else
            return 1;
    vulkan_device_opts options = test_detail::reference_options();
    options.painter = vulkan_painter_mode::INTEGRATED;
    options.vector_text = true;
    options.tile_cache = true;
    options.whole_program_cache = !tiles_only;
    options.validation = true;
    options.error_callback = diagnostic;
    options.upload_mode = staged ? vulkan_upload_mode::STAGED : vulkan_upload_mode::DIRECT;
    auto *candidate = create_vulkan_device(options);
    REQUIRE(candidate);
    auto &d = *static_cast<vk_detail::device *>(candidate);
    const auto native = vulkan_handles(candidate);
    options.native = &native;
    options.painter = vulkan_painter_mode::SDF_RASTER;
    options.tile_cache = options.whole_program_cache = false;
    auto *reference = create_vulkan_device(options);
    REQUIRE(reference);
    fixture scenes[2];
    REQUIRE(scenes[0].initialize(candidate) && scenes[1].initialize(reference));
    texture_handle feedback_images[2]{};
    if (feedback)
        for (u32 i = 0; i < 2; ++i)
        {
            feedback_images[i] = scenes[i].device->create_target(WIDTH, HEIGHT);
            REQUIRE(feedback_images[i]);
        }
    changing_glyph_tags recorder;
    recorder.owner = &d;
    auto *painter = static_cast<renderer_detail::mesh_painter *>(scenes[0].ctx->painter);
    auto *original = painter->native;
    painter->native = &recorder;
    u32 first_hit = 24, early_hits = 0, tile_hits = 0;
    std::vector<u8> previous;
    for (u32 frame = 0; frame < 24; ++frame)
    {
        const auto before = vulkan_statistics(candidate);
        recorder.epoch = frame + 1;
        for (u32 i = 0; i < 2; ++i)
        {
            auto &scene = scenes[i];
            set_current_context(scene.ctx);
            begin_frame(scene.ctx, *scene.win, 0, 1.0 / 60.0);
            // The device selects this frame's policy during make_current.
            // Synchronize the test's replacement sink after that boundary.
            if (i == 0) recorder.reset();
            if (feedback) scene.device->set_target(feedback_images[i]);
            if (!feedback || !frame) scene.device->clear({17, 43, 79, 113});
            scene.ctx->active_theme.text_gamma = feedback || frame < 16 ? 1.0f : 1.7f;
            {
                ui u(scene.ctx);
                auto style = u.text_style(19.5f, scene.font);
                u.draw_rounded_rect({3, 4, 117, 70},
                                    {37, 113, 53, static_cast<u8>(feedback ? 5 : 137)}, 8);
                u.text({11.375f + (!feedback && frame >= 20 ? 0.25f : 0), 9.75f, 100, 32}, "Ag Q",
                       {241, 217, 183, 173});
            }
            if (feedback)
            {
                detail::dl_flush(scene.ctx);
                scene.device->set_target(scene.surface->scene_target());
                ui u(scene.ctx);
                u.image({0, 0, WIDTH, HEIGHT}, {feedback_images[i], WIDTH, HEIGHT});
            }
            end_frame(scene.ctx);
            REQUIRE(!violation_count(scene.ctx));
            REQUIRE(scene.device->read_pixels(scene.surface->scene_target(), 0, 0, WIDTH, HEIGHT,
                                              scene.pixels.data()));
        }
        REQUIRE(scenes[0].pixels == scenes[1].pixels);
        if ((!feedback && (frame == 16 || frame == 20)) || (feedback && frame == 4))
            REQUIRE(previous != scenes[0].pixels);
        previous = scenes[0].pixels;
        const auto after = vulkan_statistics(candidate);
        if (after.whole_program_hits > before.whole_program_hits)
        {
            first_hit = std::min(first_hit, frame);
            if (frame < 16) ++early_hits;
        }
        if (after.gpu_sdf_cached_pixels - before.gpu_sdf_cached_pixels == PIXELS) ++tile_hits;
    }
    REQUIRE(recorder.mutations > 48);
    if (feedback)
        REQUIRE(!early_hits && d.stats.gpu_sdf_batches && d.stats.gpu_sdf_pixels);
    else if (tiles_only)
        REQUIRE(tile_hits >= 12);
    else
    {
        REQUIRE(first_hit <= 8 && early_hits >= 8);
        REQUIRE(!d.stats.gpu_sdf_batches);
        REQUIRE(!d.sdf_functions && !d.sdf_compute_layout);
        for (const auto &slot : static_cast<vk_detail::surface *>(scenes[0].surface)->slots)
            REQUIRE(!slot.sdf_resources);
    }
    REQUIRE(!d.stats.draw_memory_allocations && !d.stats.draw_resource_creations);
    REQUIRE(!errors && vulkan_last_result(candidate) == VK_SUCCESS);
    painter->native = original;
    for (u32 i = 0; i < 2; ++i)
        if (feedback_images[i]) scenes[i].device->destroy_texture(feedback_images[i]);
    for (auto &scene : scenes) scene.release();
    destroy_vulkan_device(reference);
    destroy_vulkan_device(candidate);
    std::printf(
        "all Vulkan image identity tests passed (24 exact frames; first=%u early=%u tiles=%u)\n",
        first_hit, early_hits, tile_hits);
    return 0;
}
#undef REQUIRE
