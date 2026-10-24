// White-box cache regressions: GPU history, collision and same-frame fallback.
#undef near
#undef far
#undef small
#include "test_util.h"
namespace
{
i32 sdf_cache_errors = 0;
void sdf_cache_diagnostic(i32 code, const char *message, void *)
{
    std::fprintf(stderr, "Vulkan %d: %s\n", code, message);
    ++sdf_cache_errors;
}
} // namespace
void check_sdf_history(u32 dispatch_limit, bool collide = false, bool boundary = false)
{
    vulkan_device_opts opts;
    opts.painter = vulkan_painter_mode::COMPUTE_SDF;
    opts.tile_cache = true;
    opts.validation = true;
    opts.error_callback = sdf_cache_diagnostic;
    auto *base = create_vulkan_device(opts);
    CHECK(base);
    if (!base) return;
    auto &d = *dynamic_cast<vk_detail::device *>(base);
    d.properties.limits.maxComputeWorkGroupCount[0] = dispatch_limit;
    d.sdf_cache_faults = collide ? 1u : 0u;
    auto *surface = create_vulkan_surface(base, {.width = 128, .height = 64});
    auto *ctx = create_context(base, surface);
    auto *win = add_window(ctx, nullptr, surface, {0, 0, 128, 64});
    std::vector<u8> pixels(128 * 64 * 4), previous;
    for (i32 frame = 0; frame < 5; ++frame)
    {
        if (boundary && frame == 3)
            ++static_cast<vk_detail::texture *>(surface->scene_target())->generation;
        begin_frame(ctx, *win, 0, 1.0 / 60.0);
        {
            ui u(ctx);
            if (boundary) detail::clip_push(ctx, {0, 0, frame < 3 ? 128.0f : 64.0f, 64});
            u.draw_rect({0, 0, 128, 64}, {10, 20, 30, 255});
            if (!collide || frame < 4)
                u.draw_rounded_rect(
                    {16, 8, 64, 40},
                    collide && frame >= 3 ? color{0, 0, 255, 255} : color{255, 0, 0, 255}, 8);
            if (boundary) detail::clip_pop(ctx);
        }
        end_frame(ctx);
        CHECK(base->read_pixels(surface->scene_target(), 0, 0, 128, 64, pixels.data()));
        if (!collide)
            CHECK(frame == 0 || pixels == previous);
        else
        {
            const usize at = (24 * 128 + 40) * 4;
            CHECK(pixels[at] == (frame < 3 ? 255 : frame == 3 ? 0 : 10));
            CHECK(pixels[at + 2] == (frame < 3 ? 0 : frame == 3 ? 255 : 30));
        }
        previous = pixels;
        auto *s = dynamic_cast<vk_detail::surface *>(surface);
        for (auto *job : s->slots[s->slot].sdf_jobs)
        {
            for (auto *buffer : {&job->bin.buffers[4], &job->state, &job->history[5]})
                if (!buffer->coherent)
                {
                    VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
                    range.memory = buffer->memory;
                    range.size = VK_WHOLE_SIZE;
                    CHECK(d.vkInvalidateMappedMemoryRanges(d.native.device, 1, &range) ==
                          VK_SUCCESS);
                }
            const auto *c = reinterpret_cast<const u32 *>(job->bin.buffers[4].mapped);
            const auto *state = reinterpret_cast<const u32 *>(job->state.mapped);
            const auto *meta = reinterpret_cast<const u32 *>(job->history[5].mapped);
            CHECK(!c[3] && meta[0] == 1);
            if (boundary && frame >= 3)
                CHECK(c[7] == 0); // hash collision cannot accept old boundary
            CHECK(c[5] == 0 || (c[0] > 0 && c[1] > 0 && c[2] == 1));
            CHECK(c[5] != 0 || (c[0] == 0 && c[1] == 0));
            CHECK(state[0] == 0 || state[job->bin.parameters[11] * 3] < job->bin.parameters[11]);
        }
    }
    const auto stats = vulkan_statistics(base);
    CHECK(stats.gpu_sdf_pixels > 0 && stats.gpu_sdf_cached_pixels > 0 &&
          stats.gpu_sdf_cache_hits > 0);
    CHECK(!stats.validation_errors && !sdf_cache_errors);
    destroy_context(ctx);
    base->destroy_surface(surface);
    destroy_vulkan_device(base);
}
PUI_TEST(sdf_gpu_history_reuses_independent_suffix_pixels)
{
    check_sdf_history(65535);
}
PUI_TEST(sdf_gpu_dirty_dispatch_handles_maximum_u32_without_ceil_overflow)
{
    check_sdf_history(0xffffffffu);
}
PUI_TEST(sdf_gpu_dirty_dispatch_flattens_multiple_rows)
{
    check_sdf_history(3);
}
PUI_TEST(sdf_gpu_exact_compare_rejects_hash_collisions_and_removed_commands)
{
    check_sdf_history(65535, true);
}
PUI_TEST(sdf_gpu_exact_compare_checks_clip_and_target_even_when_hashes_collide)
{
    check_sdf_history(65535, true, true);
}
PUI_TEST(sdf_upload_budget_split_keeps_metadata_and_geometry_in_one_submission)
{
    vulkan_device_opts opts;
    opts.painter = vulkan_painter_mode::SDF_RASTER;
    opts.validation = true;
    opts.error_callback = sdf_cache_diagnostic;
    opts.upload_budget_bytes = 2 * 1024 * 1024;
    auto *base = create_vulkan_device(opts);
    CHECK(base);
    if (!base) return;
    auto *surface = create_vulkan_surface(base, {.width = 32, .height = 32});
    auto *ctx = create_context(base, surface);
    auto *win = add_window(ctx, nullptr, surface, {0, 0, 32, 32});
    begin_frame(ctx, *win, 0, 1.0 / 60.0);
    {
        ui u(ctx);
        for (i32 i = 0; i < 5000; ++i) u.draw_rounded_rect({4, 4, 16, 16}, {255, 0, 0, 255}, 4);
    }
    end_frame(ctx);
    std::vector<u8> pixels(32 * 32 * 4);
    CHECK(base->read_pixels(surface->scene_target(), 0, 0, 32, 32, pixels.data()));
    CHECK(pixels[(12 * 32 + 12) * 4] == 255 && pixels[(12 * 32 + 12) * 4 + 1] == 0);
    const auto stats = vulkan_statistics(base);
    const auto &tape = static_cast<vk_detail::device *>(base)->sdf_tape;
    CHECK(!tape.commands.capacity() && !tape.payload.capacity());
    CHECK(stats.budget_waits > 0 && stats.peak_upload_allocated_bytes <= opts.upload_budget_bytes);
    CHECK(!stats.validation_errors && !violation_count(ctx));
    destroy_context(ctx);
    base->destroy_surface(surface);
    destroy_vulkan_device(base);
}
#if defined(PUFFERUI_ENABLE_HARFBUZZ)
PUI_TEST(harfbuzz_overlay_records_real_glyph_ids_instead_of_dropping_vector_text)
{
    vulkan_device_opts opts;
    opts.painter = vulkan_painter_mode::SDF_RASTER;
    opts.vector_text = true;
    opts.validation = true;
    opts.error_callback = sdf_cache_diagnostic;
    auto *base = create_vulkan_device(opts);
    CHECK(base);
    if (!base) return;
    auto *surface = create_vulkan_surface(base, {.width = 128, .height = 64});
    auto *ctx = create_context(base, surface);
    auto *win = add_window(ctx, nullptr, surface, {0, 0, 128, 64});
    const auto font = load_font(ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
    auto theme = default_dark();
    theme.font = font;
    set_theme(ctx, theme);
    begin_frame(ctx, *win, 0, 1.0 / 60.0);
    ui u(ctx);
    u.text({4, 4, 100, 24}, "ffi", {255, 255, 255, 255});
    detail::dl_flush(ctx);
    overlay_recorder recorder;
    recorder.real = base;
    ctx->device = &recorder;
    ctx->dl->painter = nullptr;
    ctx->dl->clip_known = ctx->dl->tex_known = false;
    ctx->overlay_depth = 1;
    ctx->overlay_area = {0, 0, 128, 64};
    u.text({4, 4, 100, 24}, "ffi", {255, 255, 255, 255});
    detail::dl_flush(ctx);
    CHECK(recorder.verts.size() == 4 && recorder.idx.size() == 6 && !ctx->ts->atlases.empty());
    CHECK(load_font(ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans-Bold.ttf") != FONT_INVALID);
    CHECK(ctx->ts->vector_enabled); // temporary wrapper must not switch the engine off
    ctx->device = base;
    ctx->dl->painter = paint_detail::vulkan_primitive_sink(base);
    ctx->dl->clip_known = ctx->dl->tex_known = false;
    ctx->overlay_depth = 0;
    end_frame(ctx);
    CHECK(!violation_count(ctx) && !vulkan_statistics(base).validation_errors);
    destroy_context(ctx);
    base->destroy_surface(surface);
    destroy_vulkan_device(base);
}
#endif
PUI_TEST(sdf_adaptive_uses_compute_for_large_opaque_program_and_bypasses_low_coverage)
{
    vulkan_device_opts opts;
    opts.painter = vulkan_painter_mode::COMPUTE_SDF;
    opts.tile_cache = opts.adaptive_compute = opts.validation = true;
    opts.error_callback = sdf_cache_diagnostic;
    auto *base = create_vulkan_device(opts);
    CHECK(base);
    if (!base) return;
    auto &d = *static_cast<vk_detail::device *>(base);
    auto *surface = create_vulkan_surface(base, {.width = 32, .height = 32});
    auto *ctx = create_context(base, surface);
    auto *win = add_window(ctx, nullptr, surface, {0, 0, 32, 32});
    std::vector<u8> pixels(32 * 32 * 4);
    for (i32 frame = 0; frame < 7; ++frame)
    {
        begin_frame(ctx, *win, 0, 1.0 / 60.0);
        {
            ui u(ctx);
            for (i32 i = 0; i < 2048; ++i)
                u.draw_rect(frame == 4 ? rect{0, 0, 4, 4} : rect{0, 0, 32, 32}, {255, 0, 0, 255});
        }
        end_frame(ctx);
        CHECK(base->read_pixels(surface->scene_target(), 0, 0, 32, 32, pixels.data()));
    }
    CHECK(vulkan_wait_idle(base));
    const auto stats = vulkan_statistics(base);
    CHECK(stats.gpu_sdf_pixels && stats.gpu_sdf_cached_pixels && stats.gpu_sdf_bypasses);
    CHECK(d.sdf_adaptive_cooldown && pixels[(16 * 32 + 16) * 4] == 255);
    CHECK(!stats.validation_errors && !violation_count(ctx));
    destroy_context(ctx);
    base->destroy_surface(surface);
    destroy_vulkan_device(base);
}
PUI_TEST(sdf_budget_shortfalls_replay_complete_raster_without_partial_scene)
{
    std::vector<u8> expected;
    for (i32 variant = 0; variant < 4; ++variant)
    {
        vulkan_device_opts opts;
        opts.painter = variant ? vulkan_painter_mode::COMPUTE_SDF : vulkan_painter_mode::SDF_RASTER;
        opts.tile_cache = true;
        opts.validation = true;
        opts.cache_budget_bytes = variant == 2 ? 1 : 64ull * 1024 * 1024;
        opts.error_callback = sdf_cache_diagnostic;
        auto *base = create_vulkan_device(opts);
        CHECK(base);
        if (!base) return;
        auto &d = *static_cast<vk_detail::device *>(base);
        if (variant == 1) d.options.gpu_command_budget_bytes = d.stats.gpu_metadata_allocated_bytes;
        if (variant == 3)
            d.sdf_cache_faults = 2; // GPU overflow: no compare/list reads or scene writes
        auto *surface = create_vulkan_surface(base, {.width = 32, .height = 32});
        auto *ctx = create_context(base, surface);
        auto *win = add_window(ctx, nullptr, surface, {0, 0, 32, 32});
        std::vector<u8> pixels(32 * 32 * 4);
        for (i32 frame = 0; frame < 4; ++frame)
        {
            begin_frame(ctx, *win, 0, 1.0 / 60.0);
            {
                ui u(ctx);
                u.draw_rect({0, 0, 32, 32}, {10, 20, 30, 255});
                u.draw_rounded_rect({4, 4, 24, 24}, {200, 10, 80, 255}, 5);
                u.draw_rect({8, 8, 16, 16}, {20, 150, 80, 128});
            }
            end_frame(ctx);
            CHECK(base->read_pixels(surface->scene_target(), 0, 0, 32, 32, pixels.data()));
            if (!variant && !frame) expected = pixels;
            CHECK(pixels == expected);
        }
        CHECK(vulkan_wait_idle(base));
        const auto stats = vulkan_statistics(base);
        if (variant == 1) CHECK(!stats.gpu_sdf_batches);
        if (variant == 2) CHECK(stats.gpu_sdf_pixels && !stats.gpu_sdf_cache_image_bytes);
        if (variant == 3)
            CHECK(stats.gpu_sdf_batches && !stats.gpu_sdf_pixels && !stats.gpu_sdf_cached_pixels);
        CHECK(stats.peak_gpu_metadata_allocated_bytes <= opts.gpu_command_budget_bytes);
        CHECK(!stats.validation_errors && !violation_count(ctx));
        destroy_context(ctx);
        base->destroy_surface(surface);
        destroy_vulkan_device(base);
    }
}
int main()
{
    for (const auto &test : test_registry()) test.fn();
    if (!g_failures) std::puts("all GPU SDF cache regressions passed");
    return g_failures ? 1 : 0;
}
