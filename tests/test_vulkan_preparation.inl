#include "vulkan_test_options.h"
// Resource-lifetime regressions; original tests and golden oracles stay fixed.
#undef near
#undef far
#undef small
#define PUFFERUI_COMPUTE_SPIRAL_HELPERS_ONLY
#include "test_vulkan_compute_spiral.cpp"
#undef PUFFERUI_COMPUTE_SPIRAL_HELPERS_ONLY
#include "test_util.h"
#include <bit>

namespace
{
struct prepared_scene
{
    render_device *base = nullptr;
    vk_detail::device *native = nullptr;
    render_surface *surface = nullptr;
    context *ctx = nullptr;
    window *win = nullptr;
    std::vector<u8> pixels;
    explicit prepared_scene(vulkan_device_opts options)
    {
        options.validation = true;
        options.error_callback = diagnostic;
        base = create_vulkan_device(options);
        CHECK(base);
        if (!base) return;
        native = static_cast<vk_detail::device *>(base);
        surface = create_vulkan_surface(base, {.width = 132, .height = 68});
        CHECK(surface);
        if (!surface) return;
        ctx = create_context(base, surface);
        win = add_window(ctx, nullptr, surface, {0, 0, 132, 68});
        pixels.resize(132 * 68 * 4);
    }
    bool paint(i32 count = 1)
    {
        if (!win) return false;
        begin_frame(ctx, *win, 0, 1.0 / 60.0);
        {
            ui u(ctx);
            for (i32 i = 0; i < count; ++i)
            {
                detail::clip_push(ctx, {0, 0, 132.0f - (i % 2), 68});
                u.draw_rect({0, 0, 132, 68}, {10, 20, 30, 255});
                u.draw_rounded_rect({8, 6, 116, 56}, {200, 40, 80, 255}, 7);
                u.draw_rect({16, 14, 100, 40}, {20, 160, 90, 128});
                detail::clip_pop(ctx);
            }
        }
        end_frame(ctx);
        return base->read_pixels(surface->scene_target(), 0, 0, 132, 68, pixels.data());
    }
    ~prepared_scene()
    {
        if (ctx) destroy_context(ctx);
        if (surface) base->destroy_surface(surface);
        if (base) destroy_vulkan_device(base);
    }
};
vulkan_device_opts preparation_options(bool cache = true)
{
    vulkan_device_opts options = test_detail::reference_options();
    options.painter = vulkan_painter_mode::COMPUTE_SDF;
    options.tile_cache = cache;
    return options;
}
PFN_vkCmdCopyImage original_copy_image = nullptr;
u64 snapshot_copies = 0;
VKAPI_ATTR void VKAPI_CALL count_copy_image(VkCommandBuffer command, VkImage source,
                                            VkImageLayout source_layout, VkImage destination,
                                            VkImageLayout destination_layout, u32 count,
                                            const VkImageCopy *regions)
{
    ++snapshot_copies;
    original_copy_image(command, source, source_layout, destination, destination_layout, count,
                        regions);
}
} // namespace

PUI_TEST(prepared_compute_kernels_are_device_owned_and_cache_programs_are_lazy)
{
    for (bool cache : {false, true})
    {
        prepared_scene scene(preparation_options(cache));
        if (!scene.native || !scene.surface) return;
        auto &d = *scene.native;
        const auto before = vulkan_statistics(scene.base);
        CHECK(before.gpu_compute_pipeline_creations == (cache ? 11 : 7));
        for (auto &slot : static_cast<vk_detail::surface *>(scene.surface)->slots)
            for (auto *job : slot.sdf_resources->jobs)
            {
                CHECK(job->bin.layout == d.bin_pipeline_layout);
                CHECK(job->painter == d.sdf_pipelines[0]);
                CHECK((job->compare != VK_NULL_HANDLE) == cache);
            }
        for (i32 frame = 0; frame < 6; ++frame) CHECK(scene.paint());
        const auto after = vulkan_statistics(scene.base);
        CHECK(before.gpu_compute_pipeline_creations == after.gpu_compute_pipeline_creations);
        CHECK(before.gpu_metadata_allocations == after.gpu_metadata_allocations);
        CHECK(after.draw_memory_allocations == before.draw_memory_allocations);
        CHECK(after.gpu_sdf_batches && !after.validation_errors);
    }
}

PUI_TEST(prepared_history_survives_readback_submissions_and_uses_banked_views)
{
    prepared_scene scene(preparation_options());
    if (!scene.native || !scene.surface) return;
    std::vector<u8> expected;
    for (i32 frame = 0; frame < 8; ++frame)
    {
        CHECK(scene.paint());
        if (!frame) expected = scene.pixels;
        CHECK(scene.pixels == expected);
        auto &s = *static_cast<vk_detail::surface *>(scene.surface);
        for (auto *job : s.slots[s.slot].sdf_jobs)
        {
            CHECK(job->bin.buffers[0].borrowed && job->mask.borrowed);
            CHECK(job->history[0].buffer == job->bin.buffers[0].buffer);
            CHECK(job->history[4].offset == job->mask.offset);
            if (frame >= 2) CHECK(job->previous[0].buffer != job->history[0].buffer);
            const auto &mask = job->mask;
            if (!mask.coherent)
            {
                VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
                range.memory = mask.memory;
                range.size = VK_WHOLE_SIZE;
                CHECK(scene.native->vkInvalidateMappedMemoryRanges(scene.native->native.device, 1,
                                                                   &range) == VK_SUCCESS);
            }
            u32 owned = 0;
            for (usize i = 0; i < (132 * 68 + 31) / 32; ++i)
                owned += std::popcount(reinterpret_cast<const u32 *>(mask.mapped)[i]);
            const auto *control = reinterpret_cast<const u32 *>(job->bin.buffers[4].mapped);
            CHECK(owned == control[6] + control[8]); // independent mask oracle for reduced counters
        }
    }
    CHECK(scene.native->stats.gpu_sdf_cached_pixels && scene.native->stats.gpu_sdf_cache_hits);
}

PUI_TEST(prepared_cache_writes_owned_pixels_without_full_image_snapshots)
{
    prepared_scene scene(preparation_options());
    if (!scene.native || !scene.surface) return;
    original_copy_image = scene.native->vkCmdCopyImage;
    snapshot_copies = 0;
    scene.native->vkCmdCopyImage = count_copy_image;
    for (i32 frame = 0; frame < 8; ++frame) CHECK(scene.paint());
    CHECK(!snapshot_copies);
    CHECK(scene.native->stats.gpu_sdf_cached_pixels > 0);
    scene.native->vkCmdCopyImage = original_copy_image;
}

PUI_TEST(prepared_cache_budget_rejection_does_not_retry_or_prepare_history_banks)
{
    auto options = preparation_options();
    options.cache_budget_bytes = 1;
    prepared_scene scene(options);
    if (!scene.native || !scene.surface) return;
    const auto attempts = scene.native->stats.gpu_sdf_cache_image_attempts;
    CHECK(attempts == 1);
    for (auto &slot : static_cast<vk_detail::surface *>(scene.surface)->slots)
        CHECK(slot.sdf_resources && !slot.sdf_resources->banks[1].buffer);
    for (i32 frame = 0; frame < 8; ++frame) CHECK(scene.paint());
    CHECK(scene.native->stats.gpu_sdf_cache_image_attempts == attempts);
    CHECK(scene.native->stats.gpu_sdf_pixels && !scene.native->stats.gpu_sdf_cache_image_bytes);
}

PUI_TEST(prepared_runtime_cache_transitions_prepare_kernels_outside_draw_and_release_images)
{
    prepared_scene scene(preparation_options(false));
    if (!scene.ctx) return;
    CHECK(scene.paint());
    const auto reference = scene.pixels;
    CHECK(scene.native->stats.gpu_compute_pipeline_creations == 7);
    for (bool enabled : {true, false, true})
    {
        vulkan_set_cache(scene.base, enabled);
        const auto allocations = scene.native->stats.draw_memory_allocations;
        for (i32 frame = 0; frame < 6; ++frame)
        {
            CHECK(scene.paint());
            CHECK(scene.pixels == reference);
            CHECK(scene.native->stats.draw_memory_allocations == allocations);
        }
        CHECK(scene.native->stats.gpu_compute_pipeline_creations == 11);
        CHECK((scene.native->stats.gpu_sdf_cache_image_bytes != 0) == enabled);
    }
    CHECK(scene.native->stats.gpu_sdf_cached_pixels > 0);
}

PUI_TEST(prepared_job_and_descriptor_exhaustion_preserves_complete_raster)
{
    prepared_scene scene(preparation_options());
    if (!scene.native || !scene.surface) return;
    CHECK(scene.paint());
    const auto expected = scene.pixels;
    CHECK(scene.paint(301)); // exceeds both prepared compute jobs and graphics sets
    CHECK(scene.pixels == expected);
    CHECK(scene.native->stats.gpu_sdf_capacity_bypasses > 0);
    CHECK(scene.native->stats.budget_waits > 0);
    CHECK(!scene.native->stats.validation_errors);
}

static void qualify_spiral_allocations(vulkan_upload_mode upload, bool adaptive = false)
{
    g_start_page = PAGE_LAYOUT;
    g_no_hud = true;
    auto options = preparation_options();
    options.upload_mode = upload;
    options.adaptive_compute = adaptive;
#if defined(PUFFERUI_ENABLE_HARFBUZZ)
    options.vector_text = true;
#endif
    options.validation = true;
    options.error_callback = diagnostic;
    auto *base = create_vulkan_device(options);
    CHECK(base);
    if (!base) return;
    example_app app;
    CHECK(initialize(app, base, 5000));
    if (!app.ctx)
    {
        destroy_vulkan_device(base);
        return;
    }
    auto &d = *static_cast<vk_detail::device *>(base);
    const auto allocations = d.stats.draw_memory_allocations;
    const auto metadata = d.stats.gpu_metadata_allocations;
    const auto pipelines = d.stats.gpu_compute_pipeline_creations;
    std::vector<u8> unused;
    for (i32 frame = 0; frame < FRAMES; ++frame)
    {
        CHECK(render(app, frame, unused, false));
        CHECK(d.stats.draw_memory_allocations == allocations);
        CHECK(d.stats.gpu_metadata_allocations == metadata);
        CHECK(d.stats.gpu_compute_pipeline_creations == pipelines);
        CHECK(!d.stats.gpu_sdf_capacity_bypasses);
    }
    CHECK(g_app.tiles.size() == 5003);
    if (adaptive)
    {
        CHECK(!d.stats.gpu_sdf_batches && !d.stats.gpu_sdf_cache_image_bytes);
        for (const auto &slot : static_cast<vk_detail::surface *>(app.surface)->slots)
            CHECK(!slot.sdf_resources);
    }
    else
        CHECK(d.stats.gpu_sdf_batches > 180);
    for (const auto &slot : static_cast<vk_detail::surface *>(app.surface)->slots)
        if (const auto *resources = slot.sdf_resources)
            std::fprintf(stderr, "prepared arena capacity=%llu peak=%llu current=%llu\n",
                         static_cast<unsigned long long>(resources->banks[0].capacity),
                         static_cast<unsigned long long>(resources->peak_required_bytes),
                         static_cast<unsigned long long>(resources->required_bytes));
    std::fprintf(
        stderr,
        "resident upload=%llu metadata=%llu images=%llu glyph_bytes=%llu glyph_batches=%llu\n",
        d.stats.peak_upload_allocated_bytes, d.stats.peak_gpu_metadata_allocated_bytes,
        d.stats.image_bytes, d.stats.glyph_upload_bytes, d.stats.glyph_resident_batches);
    CHECK(vulkan_wait_idle(base));
    destroy_context(app.ctx);
    base->destroy_surface(app.surface);
    CHECK(!d.stats.validation_errors && !diagnostic_errors);
    destroy_vulkan_device(base);
}

PUI_TEST(prepared_actual_5000_spiral_records_all_frames_without_draw_allocations)
{
    qualify_spiral_allocations(vulkan_upload_mode::DIRECT);
}
PUI_TEST(prepared_actual_5000_spiral_staged_records_all_frames_without_draw_allocations)
{
    qualify_spiral_allocations(vulkan_upload_mode::STAGED);
}
PUI_TEST(prepared_adaptive_spiral_keeps_unused_history_images_and_arenas_unallocated)
{
    qualify_spiral_allocations(vulkan_upload_mode::DIRECT, true);
}

PUI_TEST(prepared_adaptive_program_requests_resources_at_the_next_frame_boundary)
{
    auto options = preparation_options();
    options.adaptive_compute = true;
    prepared_scene scene(options);
    if (!scene.ctx) return;
    std::vector<u8> expected;
    for (i32 frame = 0; frame < 4; ++frame)
    {
        begin_frame(scene.ctx, *scene.win, 0, 1.0 / 60.0);
        const auto allocations = scene.native->stats.draw_memory_allocations;
        {
            ui u(scene.ctx);
            for (i32 i = 0; i < 2048; ++i) u.draw_rect({0, 0, 132, 68}, {10, 20, 30, 255});
        }
        end_frame(scene.ctx);
        CHECK(scene.base->read_pixels(scene.surface->scene_target(), 0, 0, 132, 68,
                                      scene.pixels.data()));
        CHECK(scene.native->stats.draw_memory_allocations == allocations);
        if (!frame)
        {
            expected = scene.pixels;
            CHECK(!scene.native->stats.gpu_sdf_batches);
        }
        else
        {
            CHECK(scene.pixels == expected);
            CHECK(scene.native->stats.gpu_sdf_batches > 0);
        }
    }
}

#if defined(PUFFERUI_ENABLE_HARFBUZZ)
PUI_TEST(prepared_slug_uploads_only_new_outlines_and_preserves_fallback_pixels)
{
    auto options = preparation_options(false);
    options.painter = vulkan_painter_mode::SDF_RASTER;
    options.vector_text = true;
    prepared_scene scene(options);
    CHECK(scene.ctx);
    if (!scene.ctx) return;
    auto font = load_font(scene.ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
    CHECK(font != FONT_INVALID);
    auto theme = default_dark();
    theme.font = font;
    set_theme(scene.ctx, theme);
    std::vector<u8> reference;
    u64 first_upload = 0;
    for (i32 frame = 0; frame < 8; ++frame)
    {
        // Force the portable per-submission upload fallback after three frames.
        const auto capacity = scene.native->slug_storage.used;
        const auto old_bytes = scene.native->slug_data.size();
        const auto old_upload = scene.native->stats.glyph_upload_bytes;
        if (frame == 3) scene.native->slug_storage.used = 0;
        begin_frame(scene.ctx, *scene.win, 0, 1.0 / 60.0);
        {
            ui u(scene.ctx);
            u.draw_rect({0, 0, 132, 68}, {10, 20, 30, 255});
            u.text({8, 8, 120, 48}, frame >= 6 ? "ffi a\xcc\x81 123 Z" : "ffi a\xcc\x81 123",
                   {255, 255, 255, 255});
        }
        end_frame(scene.ctx);
        CHECK(scene.base->read_pixels(scene.surface->scene_target(), 0, 0, 132, 68,
                                      scene.pixels.data()));
        if (frame == 0)
        {
            reference = scene.pixels;
            first_upload = scene.native->stats.glyph_upload_bytes;
            CHECK(first_upload > 0 && scene.native->stats.glyph_resident_batches > 0);
        }
        else if (frame == 6)
        {
            const auto added = scene.native->slug_data.size() - old_bytes;
            CHECK(added > 0 && added < scene.native->slug_data.size());
            CHECK(scene.native->stats.glyph_upload_bytes - old_upload == added);
            CHECK(scene.native->slug_committed == scene.native->slug_data.size());
            CHECK(scene.native->slug_storage.device_buffer);
            reference = scene.pixels;
        }
        else
            CHECK(scene.pixels == reference);
        if (frame < 3) CHECK(scene.native->stats.glyph_upload_bytes == first_upload);
        if (frame == 3)
        {
            CHECK(scene.native->stats.glyph_upload_bytes > first_upload);
            scene.native->slug_storage.used = capacity;
        }
    }
}
#endif

PUI_TEST(prepared_kawase_clamps_odd_roi_and_reuses_resources)
{
    auto options = preparation_options(false);
    options.painter = vulkan_painter_mode::REFERENCE;
    options.blur = vulkan_blur_mode::KAWASE;
    prepared_scene scene(options);
    if (!scene.ctx) return;
    const auto images = scene.native->stats.image_bytes;
    for (i32 frame = 0; frame < 8; ++frame)
    {
        begin_frame(scene.ctx, *scene.win, 0, 1.0 / 60.0);
        {
            ui u(scene.ctx);
            u.draw_rect({0, 0, 132, 68},
                        frame % 2 ? color{240, 10, 30, 255} : color{10, 240, 30, 255});
            u.draw_rect({13, 7, 103, 51}, {80, 120, 160, 255});
            detail::dl_flush(scene.ctx);
            paint_detail::blur_result result;
            CHECK(scene.native->kawase_blur(
                static_cast<vk_detail::texture *>(scene.surface->scene_target()), {13, 7, 103, 51},
                24, result));
            CHECK(result.image);
            std::vector<u8> blurred(52 * 26 * 4);
            CHECK(scene.base->read_pixels(result.image, 0, 0, 52, 26, blurred.data()));
            for (usize i = 0; i < blurred.size(); i += 4)
                CHECK(blurred[i] == 80 && blurred[i + 1] == 120 && blurred[i + 2] == 160 &&
                      blurred[i + 3] == 255);
        }
        end_frame(scene.ctx);
        CHECK(scene.native->stats.image_bytes == images);
    }
    CHECK(scene.native->stats.kawase_passes == 56);
    CHECK(!scene.native->stats.validation_errors);
}

int main()
{
    for (const auto &test : test_registry()) test.fn();
    if (!g_failures) std::puts("all native resource preparation regressions passed");
    return g_failures ? 1 : 0;
}
