// Independent ordered-list oracle plus cooperative scan/compaction regressions.
#define main original_binning_main
#include "test_vulkan_binning.inl"
#undef main

PUI_TEST(cooperative_binning_scan_preserves_non_power_of_two_partition_boundaries)
{
    using namespace paint_detail;
    command_tape tape;
    CHECK(tape.prepare(97, 0, 97 * sizeof(command)));
    for (u32 i = 0; i < 97; ++i)
    {
        command c;
        c.x0 = static_cast<f32>((i * 37) % 173);
        c.y0 = 0;
        c.x1 = c.x0 + static_cast<f32>(1 + (i * 11) % 41);
        c.y1 = 3;
        CHECK(tape.append(c, {}));
    }
    for (u32 tiles : {1u, 63u, 64u, 65u, 127u, 129u, 257u, 1161u, 4099u})
        compare_gpu(tape, tiles, 1, 1, 64, 97 * tiles, false);
    compare_gpu(tape, 1360, 860, 32, 256, 97 * 1161, false);
    compare_gpu(tape, 4099, 1, 1, 64, 1, true);
}

PUI_TEST(cooperative_fine_lists_preserve_order_across_chunks_and_dispatch_rows)
{
    paint_detail::command_tape tape;
    CHECK(tape.prepare(4099, 0, 4099 * sizeof(paint_detail::command)));
    for (u32 i = 0; i < 4099; ++i)
    {
        paint_detail::command command;
        command.x0 = static_cast<f32>((i * 31) % 133);
        command.x1 = command.x0 + static_cast<f32>(i % 17 + 1);
        command.y0 = 0;
        command.y1 = 3;
        CHECK(tape.append(command, {}));
    }
    // Restrict the advertised grid width so the real GPU exercises row
    // transitions without requiring an enormous render target or list buffer.
    const u32 saved = gpu_device->properties.limits.maxComputeWorkGroupCount[0];
    gpu_device->properties.limits.maxComputeWorkGroupCount[0] = 65;
    compare_gpu(tape, 257, 3, 1, 64, 4099 * 257 * 3, false);
    compare_gpu(tape, 257, 3, 1, 64, 1, true);
    gpu_device->properties.limits.maxComputeWorkGroupCount[0] = saved;
}

PUI_TEST(reused_bin_storage_overwrites_live_ranges_without_clearing_unused_indices)
{
    paint_detail::command_tape tape;
    CHECK(tape.prepare(97, 0, 97 * sizeof(paint_detail::command)));
    vk_detail::gpu_bin_job job;
    for (u32 count : {97u, 1u, 0u, 65u})
    {
        tape.commands.clear();
        for (u32 i = 0; i < count; ++i)
        {
            paint_detail::command command;
            command.x0 = static_cast<f32>(i % 19);
            command.y0 = 2;
            command.x1 = command.x0 + 7;
            command.y1 = 17;
            CHECK(tape.append(command, {}));
        }
        if (!job.dev)
            CHECK(job.initialize(*gpu_device, tape, 71, 49, 8, 32, 97 * 63));
        else
            CHECK(job.prepare_again(tape, 97 * 63));
        for (u32 index : {1u, 2u, 3u})
        {
            auto &buffer = job.buffers[index];
            std::memset(buffer.mapped, 0x5a, static_cast<usize>(buffer.capacity));
            buffer.used = buffer.capacity;
            CHECK(gpu_device->flush_page(buffer));
        }
        CHECK(job.record());
        paint_detail::tile_lists actual, expected;
        bool overflow = true;
        CHECK(job.readback(actual, overflow) && !overflow);
        CHECK(paint_detail::bin_reference(tape, 71, 49, 8, 97 * 63, expected));
        CHECK(actual.indices == expected.indices && actual.offsets == expected.offsets);
        const auto *indices = reinterpret_cast<const u32 *>(job.buffers[3].mapped);
        CHECK(indices[97 * 63 - 1] == 0x5a5a5a5au);
    }
    job.release();
}

#if defined(PUFFERUI_ENABLE_HARFBUZZ)
PUI_TEST(recycled_glyph_encoder_preserves_cached_bytes_and_matches_fresh_blob_encoding)
{
    null_device device;
    auto *c = create_context(&device, device.create_surface());
    const auto font = load_font(c, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
    CHECK(font != FONT_INVALID);
    auto *native = detail::harfbuzz_font(c, font);
    auto *reference = hb_gpu_draw_create_or_fail();
    CHECK(native && reference);
    font_data::outline *retained = nullptr;
    std::vector<u8> retained_bytes;
    for (u32 codepoint = 33; codepoint < 128; ++codepoint)
    {
        hb_codepoint_t glyph = 0;
        if (!hb_font_get_nominal_glyph(native, codepoint, &glyph)) continue;
        hb_gpu_draw_clear(reference);
        CHECK(hb_gpu_draw_glyph_or_fail(reference, native, glyph));
        hb_glyph_extents_t extents{};
        auto *blob = hb_gpu_draw_encode(reference, &extents);
        CHECK(blob);
        unsigned length = 0;
        const auto *bytes = hb_blob_get_data(blob, &length);
        auto *actual = detail::harfbuzz_outline(c, font, glyph);
        CHECK(actual && actual->data.size() == length);
        if (actual)
        {
            CHECK(std::memcmp(&actual->extents, &extents, sizeof(extents)) == 0);
            CHECK(!length || std::memcmp(actual->data.data(), bytes, length) == 0);
            if (!retained)
            {
                retained = actual;
                retained_bytes.assign(actual->data.begin(), actual->data.end());
            }
        }
        hb_blob_destroy(blob); // independent reference deliberately does not recycle
    }
    CHECK(retained && retained->data.size() == retained_bytes.size());
    CHECK(retained &&
          std::memcmp(retained->data.data(), retained_bytes.data(), retained_bytes.size()) == 0);
    hb_gpu_draw_destroy(reference);
    destroy_context(c);
}
#endif

PUI_TEST(cooperative_scan_cannot_wrap_large_unsigned_counts_into_valid_lists)
{
    paint_detail::command_tape tape;
    CHECK(tape.prepare(0, 0, 0));
    vk_detail::gpu_bin_job job;
    CHECK(job.initialize(*gpu_device, tape, 129, 1, 1, 64, 0));
    for (u32 capacity : {0u, 1u, 0xffffffffu})
    {
        CHECK(gpu_device->commands());
        std::memset(job.buffers[2].mapped, 0, static_cast<usize>(job.buffers[2].capacity));
        std::memset(job.buffers[4].mapped, 0, static_cast<usize>(job.buffers[4].capacity));
        auto *work = reinterpret_cast<u32 *>(job.buffers[2].mapped);
        work[0] = 0xfffffffeu;
        work[128] = 7; // overflow spans distinct workgroup partitions
        job.parameters[10] = capacity;
        job.buffers[2].used = job.buffers[2].capacity;
        job.buffers[4].used = job.buffers[4].capacity;
        CHECK(gpu_device->flush_page(job.buffers[2]));
        CHECK(gpu_device->flush_page(job.buffers[4]));
        job.barrier(VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
        const auto command = gpu_device->frame->command;
        gpu_device->vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, job.layout, 0,
                                            1, &job.descriptor, 0, nullptr);
        gpu_device->vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, job.pipelines[2]);
        gpu_device->vkCmdPushConstants(command, job.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                       sizeof(job.parameters), job.parameters);
        gpu_device->vkCmdDispatch(command, 1, 1, 1);
        job.barrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
        job.serial = gpu_device->submit();
        CHECK(job.serial && gpu_device->wait(job.serial));
        if (!job.buffers[4].coherent)
        {
            VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
            range.memory = job.buffers[4].memory;
            range.size = VK_WHOLE_SIZE;
            CHECK(gpu_device->vkInvalidateMappedMemoryRanges(gpu_device->native.device, 1,
                                                             &range) == VK_SUCCESS);
        }
        const auto *control = reinterpret_cast<const u32 *>(job.buffers[4].mapped);
        CHECK(control[3] == 1 && control[0] == 0 && control[4] <= capacity);
    }
    job.release();
}

PUI_TEST(cooperative_dirty_compaction_matches_ordered_cpu_flags_for_real_cache_frames)
{
    vulkan_device_opts options;
    options.painter = vulkan_painter_mode::COMPUTE_SDF;
    options.tile_cache = true;
    options.validation = true;
    options.error_callback = gpu_diagnostic;
    auto *base = create_vulkan_device(options);
    CHECK(base);
    if (!base) return;
    auto &d = *static_cast<vk_detail::device *>(base);
    auto *surface = create_vulkan_surface(base, {.width = 1360, .height = 860});
    CHECK(surface);
    if (!surface)
    {
        destroy_vulkan_device(base);
        return;
    }
    auto *c = create_context(base, surface);
    auto *window = add_window(c, nullptr, surface, {0, 0, 1360, 860});
    for (i32 f = 0; f < 5; ++f)
    {
        begin_frame(c, *window, f / 60.0, 1.0 / 60.0);
        ui u(c);
        u.draw_rect({0, 0, 1360, 860}, {20, 40, 60, 255});
        u.draw_rect({31, 31, 67, 67}, {static_cast<u8>(f == 4 ? 101 : 100), 20, 30, 255});
        end_frame(c);
        CHECK(vulkan_wait_idle(base));
        auto &s = *static_cast<vk_detail::surface *>(surface);
        CHECK(!s.slots[s.slot].sdf_jobs.empty());
        if (s.slots[s.slot].sdf_jobs.empty()) break;
        auto &job = *s.slots[s.slot].sdf_jobs.front();
        for (const auto *page : {&job.state, &job.bin.buffers[4]})
            if (!page->coherent)
            {
                VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
                range.memory = page->memory;
                range.size = VK_WHOLE_SIZE;
                CHECK(d.vkInvalidateMappedMemoryRanges(d.native.device, 1, &range) == VK_SUCCESS);
            }
        const auto *state = reinterpret_cast<const u32 *>(job.state.mapped);
        const auto *control = reinterpret_cast<const u32 *>(job.bin.buffers[4].mapped);
        const u32 tiles = job.bin.parameters[11];
        u32 count = 0;
        for (u32 tile = 0; tile < tiles; ++tile)
            if (state[tile]) CHECK(state[tiles * 3 + count++] == tile);
        CHECK(control[0] * control[1] == count);
        if (f < 2) CHECK(count == tiles);
        if (f == 2 || f == 3) CHECK(count == 0);
        if (f == 4) CHECK(count > 0 && count < tiles);
    }
    CHECK(!violation_count(c) && !vulkan_statistics(base).validation_errors);
    destroy_context(c);
    base->destroy_surface(surface);
    destroy_vulkan_device(base);
}

PUI_TEST(ordered_translucent_suffix_preserves_native_blends_and_raster_ownership)
{
    std::vector<u8> reference;
    for (i32 mode = 0; mode < 3; ++mode)
    {
        vulkan_device_opts options;
        options.painter =
            mode == 0 ? vulkan_painter_mode::SDF_RASTER : vulkan_painter_mode::COMPUTE_SDF;
        options.tile_cache = mode == 2;
        options.validation = true;
        options.error_callback = gpu_diagnostic;
        auto *device = create_vulkan_device(options);
        CHECK(device);
        if (!device) return;
        auto *surface = create_vulkan_surface(device, {.width = 83, .height = 69});
        CHECK(surface);
        if (!surface)
        {
            destroy_vulkan_device(device);
            return;
        }
        auto *c = create_context(device, surface);
        auto *window = add_window(c, nullptr, surface, {0, 0, 83, 69});
        for (i32 frame = 0; frame < 8; ++frame)
        {
            begin_frame(c, *window, frame / 60.0, 1.0 / 60.0);
            ui u(c);
            u.draw_rect({0, 0, 83, 69}, {10, 30, 70, 255});
            // Hidden opaque/translucent prefixes, later AA edges, gradients,
            // unknown geometry and alpha barriers exercise the ownership rule.
            for (i32 i = 0; i < 75; ++i)
                u.draw_rect({2, 2, 79, 65}, {static_cast<u8>(i * 3), 17, 91, 103});
            u.draw_rounded_rect({3.25f, 4.75f, 70, 55}, {60, 80, 140, 255}, 8);
            if (frame % 4 == 0) u.draw_rect({7, 8, 60, 48}, {150, 20, 30, 255});
            const vertex triangle[] = {{8, 8, 0, 0, {91, 30, 60, 170}},
                                       {73, 9, 0, 0, {30, 91, 60, 170}},
                                       {42, 62, 0, 0, {60, 30, 91, 170}}};
            const i32 indices[] = {0, 1, 2};
            u.draw_triangles(nullptr, triangle, 3, indices, 3);
            if (frame % 4 != 1) u.draw_rect({17, 15, 35, 29}, {29, 41, 73, 255});
            u.draw_rounded_gradient({.area = {11.5f, 13.25f, 54, 43},
                                     .radius = 4,
                                     .top_left = {11, 59, 83, 51},
                                     .top_right = {79, 47, 29, 137},
                                     .bottom_right = {17, 31, 103, 211},
                                     .bottom_left = {71, 13, 43, 93}});
            for (i32 i = 0; i < 13; ++i)
                u.draw_rect({15, 16, 41, 27},
                            {static_cast<u8>(i * 19), 73, 51, static_cast<u8>(i * 17)});
            end_frame(c);
            std::vector<u8> pixels(83 * 69 * 4);
            CHECK(device->read_pixels(surface->scene_target(), 0, 0, 83, 69, pixels.data()));
            if (mode == 0)
                reference.insert(reference.end(), pixels.begin(), pixels.end());
            else
                CHECK(std::memcmp(pixels.data(), reference.data() + pixels.size() * frame,
                                  pixels.size()) == 0);
        }
        CHECK(!violation_count(c) && !vulkan_statistics(device).validation_errors);
        if (mode) CHECK(vulkan_statistics(device).gpu_sdf_pixels > 0);
        destroy_context(c);
        device->destroy_surface(surface);
        destroy_vulkan_device(device);
    }
}

int main()
{
    return original_binning_main();
}
