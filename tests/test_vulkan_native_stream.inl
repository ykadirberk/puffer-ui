// Force a tiny resident arena: dense rebasing, sparse triangles and many
// mid-frame submissions must retain the one-draw reference's exact pixels.
// The Slug context then changes devices while retaining its CPU outlines.
// Included only by the independent native text implementation test.
static bool arena_growth_regression(render_device *device)
{
    auto *d = static_cast<vk_detail::device *>(device);
    const bool old_cache = d->options.tile_cache;
    vulkan_set_cache(device, false);
    auto *surface = create_vulkan_surface(device, {.width = 1360, .height = 860});
    auto *ctx = create_context(device, surface);
    auto *win = add_window(ctx, nullptr, surface, {0, 0, 1360, 860});
    std::vector<vertex> vertices(36000);
    std::vector<i32> indices(36000);
    for (i32 i = 0; i < 36000; ++i)
    {
        vertices[i] = {10000.0f + i % 3, 10000.0f + i % 5, 0, 0, color::white()};
        indices[i] = i;
    }
    for (i32 f = 0; f < 4; ++f)
    {
        begin_frame(ctx, *win, f / 60.0, 1.0 / 60.0);
        if (f && d->frame->geometry_chunk_bytes < 2 * 1024 * 1024) return false;
        const auto before = vulkan_statistics(device);
        device->draw(nullptr, vertices.data(), static_cast<i32>(vertices.size()), indices.data(),
                     static_cast<i32>(indices.size()));
        end_frame(ctx);
        const auto after = vulkan_statistics(device);
        const u64 submissions = after.submissions - before.submissions;
        if (after.draw_buffer_allocations != before.draw_buffer_allocations ||
            (f ? submissions != 1 : submissions <= 1))
            return false;
    }
    if (violation_count(ctx) || vulkan_last_result(device) != VK_SUCCESS) return false;
    destroy_context(ctx);
    device->destroy_surface(surface);
    vulkan_set_cache(device, old_cache);
    return true;
}
static bool chunk_regression(render_device *first, render_device *second)
{
    std::vector<vertex> vertices(4096);
    std::vector<i32> indices;
    for (i32 i = 0; i < 4096; ++i)
    {
        const f32 x = 4.25f + static_cast<f32>((i / 4) % 9) * 7;
        const f32 y = 3.75f + static_cast<f32>((i / 36) % 7) * 6;
        vertices[i] = {x + (i % 4 == 1 || i % 4 == 2 ? 31 : 0),
                       y + (i % 4 >= 2 ? 29 : 0),
                       0,
                       0,
                       {static_cast<u8>(i * 17), static_cast<u8>(i * 29), static_cast<u8>(i * 41),
                        static_cast<u8>(51 + i % 193)}};
    }
    for (i32 i = 0; i < 48; ++i)
        for (i32 offset : {0, 1, 2, 0, 2, 3}) indices.push_back(i * 4 + offset);
    for (i32 index : {4095, 1, 1023, 0, 0, 0}) indices.push_back(index);
    std::vector<u8> pixels[2];
    for (i32 variant = 0; variant < 2; ++variant)
    {
        auto *device = variant ? second : first;
        auto *d = static_cast<vk_detail::device *>(device);
        auto *surface = create_vulkan_surface(device, {.width = 131, .height = 97});
        auto *ctx = create_context(device, surface);
        auto *win = add_window(ctx, nullptr, surface, {0, 0, 131, 97});
        begin_frame(ctx, *win, 0, 1.0 / 60.0);
        if (!variant)
        {
            auto &slot = *d->frame;
            for (usize i = 0; i < slot.pages.size();)
                if (slot.pages[i].geometry)
                {
                    if (slot.pages[i].used) return false;
                    d->free_page(slot.pages[i]);
                    slot.pages.erase(slot.pages.begin() + static_cast<std::ptrdiff_t>(i));
                }
                else
                    ++i;
            vk_detail::page page;
            if (!d->allocate_buffer(
                    page, 128,
                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT))
                return false;
            page.geometry = true;
            page.staged = d->options.upload_mode == vulkan_upload_mode::STAGED;
            if (page.staged && !d->allocate_device_buffer(page)) return false;
            slot.pages.push_back(page);
            slot.geometry_chunk_bytes = 128;
        }
        const auto before = vulkan_statistics(device);
        device->draw(nullptr, vertices.data(), static_cast<i32>(vertices.size()), indices.data(),
                     static_cast<i32>(indices.size()));
        end_frame(ctx);
        const auto after = vulkan_statistics(device);
        if (after.draw_buffer_allocations != before.draw_buffer_allocations ||
            (!variant && after.submissions - before.submissions < 20))
            return false;
        pixels[variant].resize(131 * 97 * 4);
        if (!device->read_pixels(surface->scene_target(), 0, 0, 131, 97, pixels[variant].data()) ||
            violation_count(ctx))
            return false;
        destroy_context(ctx);
        device->destroy_surface(surface);
    }
    return pixels[0] == pixels[1];
}
static bool rebind_regression(text_scene &scene, render_device *other, std::vector<u8> &pixels)
{
    if (!scene.frame(95, pixels)) return false;
    const auto expected = pixels;
    auto *original_device = scene.device;
    auto *original_surface = scene.surface;
    auto *temporary = create_vulkan_surface(other, {.width = 321, .height = 193});
    if (!temporary) return false;
    for (auto *device : {other, original_device})
    {
        remove_window(scene.ctx, *scene.win);
        scene.device = device;
        scene.surface = device == other ? temporary : original_surface;
        set_device(scene.ctx, device, scene.surface);
        scene.win = add_window(scene.ctx, nullptr, scene.surface, {0, 0, 321, 193});
        if (!scene.frame(95, pixels) || pixels != expected) return false;
    }
    other->destroy_surface(temporary);
    return true;
}
