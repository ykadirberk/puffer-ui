// Expensive canonical recording must back off on continuously changing pages.
PUI_TEST(whole_program_backoff_renders_every_frame_and_recovers_after_the_page_settles)
{
    std::vector<std::vector<u8>> expected;
    for (i32 variant = 0; variant < 3; ++variant)
    {
        program_scene scene(vulkan_painter_mode::SDF_RASTER, variant == 2, false, variant != 0);
        if (!scene.win) return;
        for (i32 frame = 0; frame < 180; ++frame)
        {
            const auto before = vulkan_statistics(scene.base);
            begin_frame(scene.ctx, *scene.win, frame / 60.0, 1.0 / 60.0);
            {
                ui u(scene.ctx);
                u.draw_rect({0, 0, 131, 97}, {17, 31, 71, 255});
                u.draw_rounded_rect({5.25f, 7.75f, 101, 75},
                                    {static_cast<u8>(frame < 32 ? frame * 7 : 95), 51, 91, 179},
                                    11);
                if (frame == 170) u.draw_rect({31, 23, 17, 19}, {207, 113, 43, 191});
            }
            end_frame(scene.ctx);
            const auto after = vulkan_statistics(scene.base);
            if (variant && (frame == 12 || frame == 24))
            {
                CHECK(after.whole_program_misses == before.whole_program_misses);
                CHECK(after.draws > before.draws);
            }
            if (variant && frame >= 148 && frame < 170)
                CHECK(after.whole_program_hits > before.whole_program_hits);
            if (variant && (frame == 170 || frame == 171))
                CHECK(after.whole_program_hits == before.whole_program_hits);
            CHECK(after.draw_memory_allocations == before.draw_memory_allocations);
            CHECK(after.draw_resource_creations == before.draw_resource_creations);
            auto actual = scene.pixels();
            if (!variant)
                expected.push_back(std::move(actual));
            else
                CHECK(actual == expected[static_cast<usize>(frame)]);
        }
    }
}
