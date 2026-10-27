#include "vulkan_test_options.h"
// Ordered program regressions. Existing test bodies and pixel oracles stay fixed.
#define main original_program_binning_main
#include "test_vulkan_binning.inl"
#undef main

namespace
{
struct program_scene
{
    render_device *base = nullptr;
    vk_detail::device *native = nullptr;
    render_surface *surface = nullptr;
    context *ctx = nullptr;
    window *win = nullptr;
    explicit program_scene(vulkan_painter_mode mode, bool staged = false, bool cache = false,
                           bool retained = false)
    {
        vulkan_device_opts options = test_detail::reference_options();
        options.painter = mode;
        options.tile_cache = cache;
        options.whole_program_cache = retained;
        options.upload_mode = staged ? vulkan_upload_mode::STAGED : vulkan_upload_mode::DIRECT;
        options.validation = true;
        options.error_callback = gpu_diagnostic;
        base = create_vulkan_device(options);
        CHECK(base);
        if (!base) return;
        native = static_cast<vk_detail::device *>(base);
        surface = create_vulkan_surface(base, {.width = 131, .height = 97});
        CHECK(surface);
        if (!surface) return;
        ctx = create_context(base, surface);
        win = add_window(ctx, nullptr, surface, {0, 0, 131, 97});
    }
    ~program_scene()
    {
        if (ctx) destroy_context(ctx);
        if (surface) base->destroy_surface(surface);
        if (base) destroy_vulkan_device(base);
    }
    std::vector<u8> pixels()
    {
        std::vector<u8> result(131 * 97 * 4);
        CHECK(base->read_pixels(surface->scene_target(), 0, 0, 131, 97, result.data()));
        CHECK(!violation_count(ctx) && !vulkan_statistics(base).validation_errors);
        return result;
    }
};
} // namespace

PUI_TEST(program_keeps_unsubmitted_shape_and_command_tails_after_prefix_flush)
{
    std::vector<u8> expected;
    for (bool compute : {false, true})
    {
        program_scene s(compute ? vulkan_painter_mode::COMPUTE_SDF
                                : vulkan_painter_mode::SDF_RASTER);
        if (!s.win) return;
        begin_frame(s.ctx, *s.win, 0, 1.0 / 60.0);
        ui u(s.ctx);
        u.draw_rounded_rect({2, 3, 60, 57}, {20, 70, 110, 255}, 7);
        detail::dl_flush(s.ctx);
        u.draw_rounded_rect({41, 32, 81, 61}, {170, 40, 20, 123}, 13);
        if (compute)
        {
            CHECK(s.native->program_count == 1 && s.native->program_shape_end == 1);
            CHECK(s.native->sdf_shapes.size() == 2 && s.native->sdf_spans.size() == 1);
            CHECK(s.native->flush_program());
            CHECK(s.native->sdf_shapes.size() == 1 && s.native->sdf_tape.commands.size() == 1);
            CHECK(s.native->sdf_spans[0].shape == 0);
            CHECK(s.native->sdf_tape.commands[0].resource == 0);
        }
        u.draw_rect({55, 41, 25, 20}, {50, 80, 20, 97});
        end_frame(s.ctx);
        auto result = s.pixels();
        if (!compute)
            expected = std::move(result);
        else
            CHECK(result == expected);
    }
}

PUI_TEST(program_coalesces_texture_and_fractional_clip_batches_in_native_order)
{
    std::vector<u8> expected;
    for (i32 variant = 0; variant < 3; ++variant)
    {
        program_scene s(variant ? vulkan_painter_mode::COMPUTE_SDF
                                : vulkan_painter_mode::SDF_RASTER,
                        variant == 2, variant != 0);
        if (!s.win) return;
        const u8 pixels[] = {220, 10, 70,  191, 20,  170, 30, 127,
                             70,  50, 210, 223, 170, 190, 20, 97};
        auto image = s.base->create_texture(2, 2, pixels);
        CHECK(image);
        begin_frame(s.ctx, *s.win, 0, 1.0 / 60.0);
        ui u(s.ctx);
        u.draw_rect({0, 0, 131, 97}, {17, 31, 71, 255});
        for (i32 i = 0; i < 73; ++i)
        {
            detail::clip_push(s.ctx, {-2.75f + i % 3, -1.25f, 130.75f - i % 5, 97.75f});
            u.draw_rounded_rect({5.25f + i % 7, 7.75f, 101, 75},
                                {static_cast<u8>(i * 3), 51, 91, 79}, static_cast<f32>(i % 11));
            u.draw_image(make_skin_image(image, 2, 2, {0, 0, 2, 2}), {17.5f, 19.5f, 73, 51});
            u.draw_rect({31.4999f, 23.5001f, 41, 37}, {83, 47, 29, 255});
            detail::clip_pop(s.ctx);
        }
        end_frame(s.ctx);
        auto result = s.pixels();
        if (!variant)
            expected = std::move(result);
        else
        {
            CHECK(result == expected);
            const auto stats = vulkan_statistics(s.base);
            CHECK(stats.gpu_sdf_batches > 0 && stats.gpu_sdf_batches < stats.requested_draws);
        }
        s.base->destroy_texture(image);
    }
}

PUI_TEST(program_flushes_before_copy_texture_mutation_filter_and_retirement)
{
    std::vector<u8> expected;
    for (i32 variant = 0; variant < 3; ++variant)
    {
        program_scene s(variant ? vulkan_painter_mode::COMPUTE_SDF
                                : vulkan_painter_mode::SDF_RASTER,
                        variant == 2, true, true);
        if (!s.win) return;
        const u8 red[] = {230, 20, 30, 255};
        const u8 green[] = {20, 210, 50, 173};
        auto image = s.base->create_texture(1, 1, red);
        auto copy = s.base->create_target(131, 97);
        begin_frame(s.ctx, *s.win, 0, 1.0 / 60.0);
        ui u(s.ctx);
        u.draw_rect({0, 0, 131, 97}, {10, 30, 70, 255});
        u.draw_image(make_skin_image(image, 1, 1, {0, 0, 1, 1}), {4, 5, 60, 60});
        detail::dl_flush(s.ctx);
        s.base->update_texture(image, 0, 0, 1, 1, green);
        u.draw_image(make_skin_image(image, 1, 1, {0, 0, 1, 1}), {52, 31, 63, 59});
        detail::dl_flush(s.ctx);
        s.base->set_texture_filter(image, true);
        s.base->blit(s.surface->scene_target(), nullptr, copy, nullptr);
        s.base->set_target(s.surface->scene_target());
        u.draw_rounded_rect({21, 13, 73, 61}, {91, 51, 27, 111}, 8);
        detail::dl_flush(s.ctx);
        s.base->destroy_texture(image);
        CHECK(vulkan_wait_idle(s.base)); // retire a referenced image before the frame ends
        const rect source{0, 0, 63, 47}, destination{66, 48, 63, 47};
        s.base->blit(copy, &source, s.surface->scene_target(), &destination);
        s.base->destroy_target(copy);
        end_frame(s.ctx);
        auto result = s.pixels();
        if (!variant)
            expected = std::move(result);
        else
            CHECK(result == expected);
    }
}

PUI_TEST(program_history_admission_moves_existing_images_and_invalidates_only_changed_owners)
{
    vk_detail::sdf_slot_resources resources;
    vk_detail::sdf_gpu_job jobs[8];
    vk_detail::texture images[4];
    resources.jobs.reserve(8);
    for (u32 i = 0; i < 8; ++i)
    {
        resources.jobs.push_back(&jobs[i]);
        jobs[i].published_bank = 7;
        jobs[i].eligible_pixels = i < 4 ? i + 1 : 0;
        if (i < 4) jobs[i].saved = &images[i];
    }
    jobs[6].eligible_pixels = 100;
    vk_detail::rebalance_sdf_history(resources);
    CHECK(!jobs[0].saved && jobs[6].saved == &images[0]);
    CHECK(jobs[0].published_bank == -1 && jobs[6].published_bank == -1);
    for (u32 i = 1; i < 4; ++i) CHECK(jobs[i].saved == &images[i] && jobs[i].published_bank == 7);
    // Equal scores prefer incumbents, so a quiet program cannot oscillate.
    for (auto &job : jobs) job.eligible_pixels = 0;
    vk_detail::rebalance_sdf_history(resources);
    CHECK(jobs[6].saved == &images[0] && !jobs[0].saved);
    usize count = 0;
    for (auto &job : jobs) count += job.saved != nullptr;
    CHECK(count == 4 && resources.jobs.capacity() == 8);
}

PUI_TEST(whole_program_reuses_all_pixels_and_invalidates_content_clip_and_output_writes)
{
    std::vector<std::vector<u8>> expected;
    for (i32 variant = 0; variant < 3; ++variant)
    {
        program_scene s(variant ? vulkan_painter_mode::COMPUTE_SDF
                                : vulkan_painter_mode::SDF_RASTER,
                        variant == 2, true, variant != 0);
        if (!s.win) return;
        const u8 red[] = {200, 20, 70, 255}, green[] = {20, 190, 50, 173};
        auto image = s.base->create_texture(1, 1, red);
        auto copy = s.base->create_target(131, 97);
        u64 hits = 0;
        for (i32 f = 0; f < 20; ++f)
        {
            if (f == 9) s.base->update_texture(image, 0, 0, 1, 1, green);
            if (f == 11)
            {
                s.base->set_target(s.surface->scene_target());
                s.base->clear({61, 71, 81, 255});
            }
            const auto before = vulkan_statistics(s.base);
            begin_frame(s.ctx, *s.win, 0, 1.0 / 60.0);
            ui u(s.ctx);
            u.draw_rect({0, 0, 131, 97}, {17, 31, 71, 255});
            u.draw_rounded_rect({5.25f, 7.75f, 101, 75},
                                {static_cast<u8>(f < 8 ? 81 : 95), 51, 91, 79}, 11);
            u.draw_image(make_skin_image(image, 1, 1, {0, 0, 1, 1}), {13, 17, 91, 63});
            detail::dl_flush(s.ctx);
            s.base->blit(s.surface->scene_target(), nullptr, copy, nullptr);
            s.base->set_target(s.surface->scene_target());
            if (f == 10) detail::clip_push(s.ctx, {21, 23, 37, 31});
            u.draw_rect({30, 20, 53, 41}, {70, 30, 90, 111});
            if (f == 10) detail::clip_pop(s.ctx);
            end_frame(s.ctx);
            const auto after = vulkan_statistics(s.base);
            if (after.whole_program_hits != before.whole_program_hits)
            {
                ++hits;
                CHECK(after.draws == before.draws &&
                      after.gpu_sdf_batches == before.gpu_sdf_batches);
            }
            if (variant && (f == 8 || f == 9 || f == 10 || f == 11))
                CHECK(after.whole_program_hits == before.whole_program_hits);
            auto actual = s.pixels();
            if (!variant)
                expected.push_back(std::move(actual));
            else
                CHECK(actual == expected[f]);
        }
        if (variant) CHECK(hits >= 10);
        s.base->destroy_target(copy);
        s.base->destroy_texture(image);
    }
}

PUI_TEST(prepared_shape_capacity_splits_complete_batches_without_growing_storage)
{
    std::vector<u8> expected;
    for (i32 variant = 0; variant < 3; ++variant)
    {
        program_scene s(variant ? vulkan_painter_mode::COMPUTE_SDF
                                : vulkan_painter_mode::SDF_RASTER,
                        variant == 2, true, true);
        if (!s.win) return;
        std::vector<vk_detail::sdf_shape> shapes;
        std::vector<vk_detail::sdf_span> spans;
        shapes.reserve(8);
        spans.reserve(8);
        s.native->sdf_shapes.swap(shapes);
        s.native->sdf_spans.swap(spans);
        begin_frame(s.ctx, *s.win, 0, 1.0 / 60.0);
        ui u(s.ctx);
        for (i32 i = 0; i < 129; ++i)
            u.draw_rounded_rect({static_cast<f32>(i % 11), 5.25f, 111, 79},
                                {static_cast<u8>(i), 51, 91, 79}, 11);
        end_frame(s.ctx);
        CHECK(s.native->sdf_shapes.capacity() == 8 && s.native->sdf_spans.capacity() == 8);
        auto actual = s.pixels();
        if (!variant)
            expected = std::move(actual);
        else
            CHECK(actual == expected);
    }
}

PUI_TEST(whole_program_cpu_history_survives_streaming_reuse_of_gpu_upload_pages)
{
    program_scene s(vulkan_painter_mode::COMPUTE_SDF, false, true, true);
    if (!s.win) return;
    const u8 pixel[] = {111, 23, 59, 255};
    auto unused = s.base->create_texture(1, 1, pixel);
    std::vector<u8> expected;
    for (i32 f = 0; f < 5; ++f)
    {
        if (f == 3) s.base->update_texture(unused, 0, 0, 1, 1, pixel);
        const auto before = vulkan_statistics(s.base);
        begin_frame(s.ctx, *s.win, 0, 1.0 / 60.0);
        ui u(s.ctx);
        u.draw_rounded_rect({11.25f, 7.75f, 101, 75}, {61, 51, 91, 179}, 11);
        end_frame(s.ctx);
        const auto after = vulkan_statistics(s.base);
        if (f == 3) CHECK(after.whole_program_hits > before.whole_program_hits);
        auto actual = s.pixels();
        if (!f)
            expected = std::move(actual);
        else
            CHECK(actual == expected);
    }
    s.base->destroy_texture(unused);
}

PUI_TEST(whole_program_external_images_keep_real_acquire_release_and_never_reuse_content)
{
    program_scene s(vulkan_painter_mode::COMPUTE_SDF, false, true, true);
    if (!s.win) return;
    const u8 pixel[] = {111, 23, 59, 173};
    auto owned = s.base->create_texture(1, 1, pixel);
    CHECK(vulkan_wait_idle(s.base));
    auto *producer = static_cast<vk_detail::texture *>(owned);
    vulkan_image_opts source;
    source.image = producer->image;
    source.view = producer->view;
    source.width = source.height = 1;
    source.layout = producer->layout;
    source.ready = s.native->timeline;
    source.ready_value = s.native->next_serial - 1;
    auto imported = import_vulkan_image(s.base, source);
    CHECK(imported);
    std::vector<u8> expected;
    for (i32 f = 0; f < 5; ++f)
    {
        begin_frame(s.ctx, *s.win, 0, 1.0 / 60.0);
        ui u(s.ctx);
        u.draw_image(make_skin_image(imported, 1, 1, {0, 0, 1, 1}), {11, 13, 101, 71});
        end_frame(s.ctx);
        CHECK(!vulkan_statistics(s.base).whole_program_hits);
        auto actual = s.pixels();
        if (!f)
            expected = std::move(actual);
        else
            CHECK(actual == expected);
    }
    CHECK(release_vulkan_image(s.base, imported, producer->layout));
    CHECK(vulkan_wait_idle(s.base));
    s.base->destroy_texture(owned);
}

PUI_TEST(whole_program_does_not_freeze_alpha_accumulation_without_a_clear)
{
    program_scene s(vulkan_painter_mode::COMPUTE_SDF, false, true, true);
    if (!s.win) return;
    s.base->set_target(s.surface->scene_target());
    s.base->clear({0, 0, 0, 255});
    CHECK(vulkan_wait_idle(s.base));
    const vertex v[] = {{0, 0, 0, 0, {200, 100, 50, 53}},
                        {131, 0, 0, 0, {200, 100, 50, 53}},
                        {131, 97, 0, 0, {200, 100, 50, 53}},
                        {0, 97, 0, 0, {200, 100, 50, 53}}};
    const i32 indices[] = {0, 1, 2, 0, 2, 3};
    std::vector<u8> previous;
    for (i32 f = 0; f < 6; ++f)
    {
        s.surface->make_current(*s.base);
        s.base->set_clip(nullptr);
        s.base->draw(s.native->white, v, 4, indices, 6);
        s.surface->present();
        auto actual = s.pixels();
        CHECK(!vulkan_statistics(s.base).whole_program_hits);
        if (f) CHECK(actual != previous);
        previous = std::move(actual);
    }
    CHECK(vulkan_statistics(s.base).whole_program_probes > 0);
}

PUI_TEST(whole_program_history_capacity_uses_complete_rendering_without_heap_growth)
{
    std::vector<u8> expected;
    for (i32 variant = 0; variant < 2; ++variant)
    {
        program_scene s(variant ? vulkan_painter_mode::COMPUTE_SDF
                                : vulkan_painter_mode::SDF_RASTER,
                        false, true, variant != 0);
        if (!s.win) return;
        if (variant)
            for (auto *program : static_cast<vk_detail::surface *>(s.surface)->retained)
                program->payload_capacity = 128;
        begin_frame(s.ctx, *s.win, 0, 1.0 / 60.0);
        ui u(s.ctx);
        for (i32 i = 0; i < 129; ++i)
            u.draw_rounded_rect({static_cast<f32>(i % 11), 5.25f, 111, 79},
                                {static_cast<u8>(i), 51, 91, 79}, 11);
        end_frame(s.ctx);
        auto actual = s.pixels();
        if (!variant)
            expected = std::move(actual);
        else
        {
            CHECK(actual == expected && !vulkan_statistics(s.base).whole_program_hits);
            for (auto *program : static_cast<vk_detail::surface *>(s.surface)->retained)
                CHECK(program->payload_capacity == 128);
        }
    }
}

PUI_TEST(whole_program_equal_payloads_reuse_cpu_bytes_across_arena_recycling_and_size_changes)
{
    program_scene scene(vulkan_painter_mode::COMPUTE_SDF);
    if (!scene.win) return;
    auto &d = *scene.native;
    vk_detail::retained_program programs[2];
    for (auto &program : programs)
    {
        program.payload = new u8[4096];
        program.payload_capacity = 4096;
        program.valid = false;
        std::memset(program.payload, 0xcd, 4096);
    }
    d.sdf_spans.push_back({0, 0});
    i32 previous_variant = -1;
    for (i32 f = 0; f < 16; ++f)
    {
        const i32 variant = f < 3 ? 0 : f < 7 ? 1 : f < 11 ? 2 : 0;
        auto &now = programs[f % 2];
        auto &previous = programs[1 - f % 2];
        now.draw_count = now.image_count = now.payload_used = 0;
        now.valid = true;
        d.retaining = &now;
        d.retained_previous = &previous;
        d.retained_equal = previous.valid;
        for (i32 i = 0; i < 3; ++i)
        {
            vertex vertices[6]{};
            vertices[0].x = static_cast<f32>(i + (i == 1 ? variant : 0));
            const i32 indices[] = {0, 1, 2, 0, 2, 3};
            vk_detail::retained_draw event;
            event.key[0] = 2;
            event.vertex_count = i == 1 && variant == 1 ? 6 : 4;
            event.stride = sizeof(vertex);
            event.vertices = reinterpret_cast<const u8 *>(vertices);
            event.indices = reinterpret_cast<const u8 *>(indices);
            event.index_bytes = sizeof(indices);
            event.span_bytes = 12;
            d.retained_add(event, static_cast<vk_detail::texture *>(scene.surface->scene_target()),
                           nullptr);
            const auto &saved = now.draws[i];
            CHECK(!std::memcmp(saved.vertices, vertices, event.vertex_count * sizeof(vertex)));
            CHECK(!std::memcmp(saved.indices, indices, sizeof(indices)));
            if (variant == previous_variant) CHECK(saved.vertices == previous.draws[i].vertices);
        }
        CHECK(d.retained_equal == (variant == previous_variant));
        if (f == 1)
            for (usize i = 0; i < now.payload_capacity; ++i) CHECK(now.payload[i] == 0xcd);
        previous_variant = variant;
    }
    d.retaining = d.retained_previous = nullptr;
    d.sdf_spans.clear();
}

PUI_TEST(whole_program_replays_equal_prefix_before_changes_shortening_and_midframe_mutation)
{
    std::vector<std::vector<u8>> expected;
    for (i32 variant = 0; variant < 3; ++variant)
    {
        program_scene s(variant ? vulkan_painter_mode::COMPUTE_SDF
                                : vulkan_painter_mode::SDF_RASTER,
                        variant == 2, variant != 0, variant != 0);
        if (!s.win) return;
        const u8 initial[] = {51, 81, 131, 171}, changed[] = {141, 37, 79, 193};
        auto image = s.base->create_texture(1, 1, initial);
        for (i32 f = 0; f < 23; ++f)
        {
            const auto before = vulkan_statistics(s.base);
            begin_frame(s.ctx, *s.win, 0, 1.0 / 60.0);
            ui u(s.ctx);
            u.draw_rect({0, 0, 131, 97}, {17, 31, 71, 255});
            u.draw_rounded_rect({4.25f, 7.75f, 101, 75}, {61, 51, 91, 179}, 11);
            detail::dl_flush(s.ctx);
            if (variant && f == 6)
                CHECK(vulkan_statistics(s.base).upload_bytes == before.upload_bytes);
            if (f == 14) s.base->update_texture(image, 0, 0, 1, 1, changed);
            if (f != 11)
            {
                const u8 red = f >= 8 ? 171 : 71;
                u.draw_rounded_rect({11.75f, 3.25f, 109, 83}, {red, 91, 41, 93}, 13);
                u.draw_image(make_skin_image(image, 1, 1, {0, 0, 1, 1}), {21, 17, 79, 61});
            }
            end_frame(s.ctx);
            const auto after = vulkan_statistics(s.base);
            if (variant && f == 6) CHECK(after.whole_program_hits > before.whole_program_hits);
            if (variant && (f == 8 || f == 11 || f == 14))
                CHECK(after.whole_program_hits == before.whole_program_hits);
            auto actual = s.pixels();
            if (!variant)
                expected.push_back(std::move(actual));
            else
                CHECK(actual == expected[f]);
        }
        s.base->destroy_texture(image);
    }
}

PUI_TEST(whole_program_reuses_sdf_raster_without_preparing_the_suffix_painter_or_blend_table)
{
    std::vector<std::vector<u8>> expected;
    for (i32 variant = 0; variant < 3; ++variant)
    {
        program_scene s(vulkan_painter_mode::SDF_RASTER, variant == 2, false, variant != 0);
        if (!s.win) return;
        for (i32 f = 0; f < 19; ++f)
        {
            const auto before = vulkan_statistics(s.base);
            begin_frame(s.ctx, *s.win, 0, 1.0 / 60.0);
            ui u(s.ctx);
            u.draw_rect({0, 0, 131, 97}, {17, 31, 71, 255});
            u.draw_rounded_rect({4.25f, 7.75f, 101, 75}, {61, 51, 91, 179}, 11);
            detail::dl_flush(s.ctx);
            u.draw_rounded_rect({11.75f, 3.25f, 109, 83},
                                {static_cast<u8>(f >= 9 ? 171 : 71), 91, 41, 93}, 13);
            end_frame(s.ctx);
            const auto after = vulkan_statistics(s.base);
            CHECK(after.gpu_sdf_calibration_ms == 0 && after.gpu_sdf_batches == 0);
            if (variant && (f == 6 || f == 17))
            {
                CHECK(after.whole_program_hits > before.whole_program_hits);
                CHECK(after.draws == before.draws && after.upload_bytes == before.upload_bytes);
            }
            if (variant && f == 9) CHECK(after.whole_program_hits == before.whole_program_hits);
            auto actual = s.pixels();
            if (!variant)
                expected.push_back(std::move(actual));
            else
                CHECK(actual == expected[f]);
        }
    }
}

PUI_TEST(whole_program_certifies_sampled_only_partial_scratch_and_rejects_missing_compute_queue)
{
    std::vector<std::vector<u8>> expected;
    for (i32 variant = 0; variant < 4; ++variant)
    {
        program_scene s(vulkan_painter_mode::SDF_RASTER, variant == 2, false, variant != 0);
        if (!s.win) return;
        auto &surface = *static_cast<vk_detail::surface *>(s.surface);
        if (variant == 3)
        {
            s.native->release_program_cache(surface);
            s.native->can_compute = false;
            CHECK(s.native->prepare_program_cache(surface));
            CHECK(!surface.program_cache && !surface.retained[0]);
        }
        auto scratch = s.base->create_target(31, 29);
        CHECK(scratch);
        CHECK(!(static_cast<vk_detail::texture *>(scratch)->usage & VK_IMAGE_USAGE_STORAGE_BIT));
        s.base->set_target(scratch);
        s.base->clear({5, 11, 17, 255});
        CHECK(vulkan_wait_idle(s.base));
        for (i32 f = 0; f < 20; ++f)
        {
            const auto before = vulkan_statistics(s.base);
            begin_frame(s.ctx, *s.win, 0, 1.0 / 60.0);
            ui u(s.ctx);
            u.draw_rect({0, 0, 131, 97}, {17, 31, 71, 255});
            u.draw_rounded_rect({4.25f, 7.75f, 101, 75},
                                {static_cast<u8>(f >= 9 ? 161 : 61), 51, 91, 179}, 11);
            detail::dl_flush(s.ctx);
            const rect source{7, 11, 17, 19}, destination{2, 3, 17, 19};
            s.base->blit(s.surface->scene_target(), &source, scratch, &destination);
            const rect output{41, 37, 31, 29};
            s.base->blit(scratch, nullptr, s.surface->scene_target(), &output);
            end_frame(s.ctx);
            const auto after = vulkan_statistics(s.base);
            if ((variant == 1 || variant == 2) && (f == 6 || f == 17))
                CHECK(after.whole_program_hits > before.whole_program_hits);
            if (variant == 3 || f == 9)
                CHECK(after.whole_program_hits == before.whole_program_hits);
            auto actual = s.pixels();
            if (!variant)
                expected.push_back(std::move(actual));
            else
                CHECK(actual == expected[f]);
        }
        s.base->destroy_texture(scratch);
    }
}

int main()
{
    return original_program_binning_main();
}
