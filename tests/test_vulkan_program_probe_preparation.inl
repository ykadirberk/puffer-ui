// Deferring comparator setup moved a 15.8ms cost into frame 4 (the fifth frame).
// Keep cache resources prepared before drawing, including a later LOAD probe.
PUI_TEST(whole_program_prepares_probe_resources_before_the_first_frame)
{
    for (bool changing : {false, true})
    {
        std::vector<std::vector<u8>> expected;
        for (i32 variant = 0; variant < 3; ++variant)
        {
            program_scene scene(vulkan_painter_mode::SDF_RASTER, variant == 2, false, variant != 0);
            if (!scene.win) return;
            auto &surface = *static_cast<vk_detail::surface *>(scene.surface);
            if (variant)
            {
                CHECK(surface.retained[0] && surface.retained[1] && surface.program_cache);
                CHECK(scene.native->program_compare_pipeline);
                CHECK(surface.program_cache->before.buffer && surface.program_cache->pool);
            }
            for (i32 frame = 0; frame < 12; ++frame)
            {
                const auto before = vulkan_statistics(scene.base);
                begin_frame(scene.ctx, *scene.win, frame / 60.0, 1.0 / 60.0);
                {
                    ui u(scene.ctx);
                    u.draw_rect({0, 0, 131, 97}, {17, 31, 71, 255});
                    u.draw_rounded_rect({7.25f, 9.75f, 73.5f, 51.25f},
                                        {static_cast<u8>(changing ? 30 + frame : 61), 51, 91, 179},
                                        11);
                }
                end_frame(scene.ctx);
                const auto after = vulkan_statistics(scene.base);
                CHECK(after.draw_memory_allocations == before.draw_memory_allocations);
                CHECK(after.draw_resource_creations == before.draw_resource_creations);
                auto actual = scene.pixels();
                if (!variant)
                    expected.push_back(std::move(actual));
                else
                {
                    CHECK(actual == expected[static_cast<usize>(frame)]);
                    CHECK(scene.native->program_compare_pipeline);
                    CHECK(surface.program_cache->before.buffer && surface.program_cache->pool);
                }
            }
            if (variant && !changing) CHECK(vulkan_statistics(scene.base).whole_program_hits > 0);
        }
    }
}

PUI_TEST(whole_program_load_probe_does_not_allocate_in_later_frames_or_freeze_alpha)
{
    std::vector<std::vector<u8>> expected;
    for (i32 variant = 0; variant < 3; ++variant)
    {
        program_scene scene(vulkan_painter_mode::SDF_RASTER, variant == 2, false, variant != 0);
        if (!scene.win) return;
        scene.base->set_target(scene.surface->scene_target());
        scene.base->clear({0, 0, 0, 255});
        CHECK(vulkan_wait_idle(scene.base));
        const auto prepared_before_frames = vulkan_statistics(scene.base);
        const vertex vertices[] = {{0, 0, 0, 0, {200, 100, 50, 53}},
                                   {131, 0, 0, 0, {200, 100, 50, 53}},
                                   {131, 97, 0, 0, {200, 100, 50, 53}},
                                   {0, 97, 0, 0, {200, 100, 50, 53}}};
        constexpr i32 indices[] = {0, 1, 2, 0, 2, 3};
        for (i32 frame = 0; frame < 12; ++frame)
        {
            scene.surface->make_current(*scene.base);
            const auto prepared = vulkan_statistics(scene.base);
            scene.base->set_clip(nullptr);
            scene.base->draw(scene.native->white, vertices, 4, indices, 6);
            scene.surface->present();
            const auto after = vulkan_statistics(scene.base);
            CHECK(after.gpu_metadata_allocations ==
                  prepared_before_frames.gpu_metadata_allocations);
            CHECK(after.descriptor_sets == prepared_before_frames.descriptor_sets);
            CHECK(after.draw_memory_allocations == prepared.draw_memory_allocations);
            CHECK(after.draw_resource_creations == prepared.draw_resource_creations);
            CHECK(!after.whole_program_hits);
            auto actual = scene.pixels();
            if (!variant)
                expected.push_back(std::move(actual));
            else
                CHECK(actual == expected[static_cast<usize>(frame)]);
        }
        if (variant) CHECK(vulkan_statistics(scene.base).whole_program_probes > 0);
    }
}

PUI_TEST(whole_program_probe_budget_rejection_keeps_complete_output_and_releases_partial_storage)
{
    std::vector<std::vector<u8>> expected;
    for (i32 variant = 0; variant < 3; ++variant)
    {
        program_scene scene(vulkan_painter_mode::SDF_RASTER, variant == 2, false, variant != 0);
        if (!scene.win) return;
        scene.base->set_target(scene.surface->scene_target());
        scene.base->clear({0, 0, 0, 255});
        CHECK(vulkan_wait_idle(scene.base));
        auto &surface = *static_cast<vk_detail::surface *>(scene.surface);
        if (variant) scene.native->release_program_cache(surface);
        const auto baseline = vulkan_statistics(scene.base);
        // Enough for the snapshot, but not its readable slot controls. Exercise
        // partial preparation and retirement, not only first-buffer rejection.
        vk_detail::gpu_bin_job allocator;
        allocator.dev = scene.native;
        vk_detail::page snapshot;
        CHECK(allocator.storage(snapshot, 131 * 97 * 4, vk_detail::storage_role::DEVICE));
        const u64 snapshot_allocation = snapshot.allocation;
        allocator.free_storage(snapshot);
        scene.native->options.gpu_command_budget_bytes =
            baseline.gpu_metadata_allocated_bytes + snapshot_allocation;
        const auto allocations_before = vulkan_statistics(scene.base).gpu_metadata_allocations;
        if (variant)
        {
            CHECK(scene.native->prepare_program_cache(surface));
            CHECK(!surface.program_cache && !surface.retained[0]);
        }
        const vertex vertices[] = {{0, 0, 0, 0, {200, 100, 50, 53}},
                                   {131, 0, 0, 0, {200, 100, 50, 53}},
                                   {131, 97, 0, 0, {200, 100, 50, 53}},
                                   {0, 97, 0, 0, {200, 100, 50, 53}}};
        constexpr i32 indices[] = {0, 1, 2, 0, 2, 3};
        for (i32 frame = 0; frame < 12; ++frame)
        {
            scene.surface->make_current(*scene.base);
            const auto before = vulkan_statistics(scene.base);
            scene.base->set_clip(nullptr);
            scene.base->draw(scene.native->white, vertices, 4, indices, 6);
            scene.surface->present();
            const auto after = vulkan_statistics(scene.base);
            CHECK(after.draw_memory_allocations == before.draw_memory_allocations);
            CHECK(after.draw_resource_creations == before.draw_resource_creations);
            CHECK(after.gpu_metadata_allocated_bytes == baseline.gpu_metadata_allocated_bytes);
            CHECK(!after.whole_program_hits && !after.whole_program_probes);
            auto actual = scene.pixels();
            if (!variant)
                expected.push_back(std::move(actual));
            else
                CHECK(actual == expected[static_cast<usize>(frame)]);
        }
        if (variant)
        {
            CHECK(!surface.program_cache && !surface.retained[0]);
            CHECK(vulkan_statistics(scene.base).gpu_metadata_allocations == allocations_before + 1);
        }
    }
}
