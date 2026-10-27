// Visiting another page must not evict already resident vector outlines.
// Included after the independent preparation tests; their bodies stay fixed.
#if defined(PUFFERUI_ENABLE_HARFBUZZ)
PUI_TEST(prepared_slug_overflow_keeps_old_page_and_shape_only_batches_resident)
{
    std::vector<std::vector<u8>> expected;
    for (i32 variant = 0; variant < 4; ++variant)
    {
        auto options = preparation_options(false);
        options.painter =
            variant == 3 ? vulkan_painter_mode::COMPUTE_SDF : vulkan_painter_mode::SDF_RASTER;
        options.vector_text = true;
        options.upload_mode =
            variant == 2 ? vulkan_upload_mode::STAGED : vulkan_upload_mode::DIRECT;
        prepared_scene scene(options);
        CHECK(scene.ctx);
        if (!scene.ctx) return;
        const auto font = load_font(scene.ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
        CHECK(font != FONT_INVALID);
        auto theme = default_dark();
        theme.font = font;
        set_theme(scene.ctx, theme);
        const u64 original_capacity = scene.native->slug_storage.capacity;
        for (i32 frame = 0; frame < 8; ++frame)
        {
            // Exercise the real append/fallback path with a small resident
            // budget. Capacity is restored before destroying the allocation.
            if (frame == 2 && variant)
                scene.native->slug_storage.capacity = scene.native->slug_storage.used;
            const auto before = vulkan_statistics(scene.base);
            begin_frame(scene.ctx, *scene.win, 0, 1.0 / 60.0);
            {
                ui u(scene.ctx);
                u.draw_rect({0, 0, 132, 68}, {10, 20, 30, 255});
                u.text({5, 5, 122, 28}, frame == 2 || frame == 3 ? "W Omega Z 789" : "Home ffi 123",
                       {241, 223, 207, 227});
                detail::dl_flush(scene.ctx);
                const u64 shapes_before = scene.native->stats.glyph_upload_bytes;
                u.draw_rounded_rect({6.25f, 40.5f, 110.5f, 20.25f}, {31, 171, 137, 193}, 7);
                detail::dl_flush(scene.ctx);
                CHECK(scene.native->stats.glyph_upload_bytes == shapes_before);
            }
            end_frame(scene.ctx);
            CHECK(scene.base->read_pixels(scene.surface->scene_target(), 0, 0, 132, 68,
                                          scene.pixels.data()));
            if (!variant)
                expected.push_back(scene.pixels);
            else
                CHECK(scene.pixels == expected[static_cast<usize>(frame)]);
            const auto after = vulkan_statistics(scene.base);
            if (variant && (frame == 2 || frame == 3))
            {
                CHECK(scene.native->slug_data.size() > scene.native->slug_storage.used);
                CHECK(after.glyph_upload_bytes > before.glyph_upload_bytes);
            }
            if (frame >= 4) CHECK(after.glyph_upload_bytes == before.glyph_upload_bytes);
            CHECK(after.draw_memory_allocations == before.draw_memory_allocations);
            CHECK(after.draw_resource_creations == before.draw_resource_creations);
            CHECK(!after.validation_errors && !violation_count(scene.ctx));
        }
        scene.native->slug_storage.capacity = original_capacity;
    }
}
#endif
