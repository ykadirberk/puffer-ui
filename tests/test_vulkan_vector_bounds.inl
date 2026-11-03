static std::vector<std::vector<u8>> vector_native_bounds_pixels(vulkan_upload_mode mode, bool cache)
{
    std::vector<std::vector<u8>> frames;
    auto *base = create_vulkan_device(
        {.validation = true, .tile_cache = cache, .vector_text = false, .upload_mode = mode});
    CHECK(base);
    if (!base) return frames;
    auto *surface = create_vulkan_surface(base, {.width = 64, .height = 64});
    auto *ctx = create_context(base, surface);
    auto *win = add_window(ctx, nullptr, surface, {0, 0, 64, 64});
    ui u(ctx);
    auto image = load_svg(
        "<svg viewBox='0 0 64 64'><rect x='8' y='8' width='48' height='48' fill='red'/></svg>");
    CHECK(image && u.prepare_vector(image.image, {.max_pixel_size = {64, 64}}));
    for (i32 test = 0; test < 6; ++test)
    {
        begin_frame(ctx, *win, test / 60.0, 1.0 / 60);
        base->clear({0, 0, 0, 0});
        vector_draw_opts options;
        rect area{0, 0, 64, 64};
        if (test == 1) options.transform = {0, 1, -1, 0, 64, 0};
        if (test == 2) options.transform = {-1, 0, 0, 1, 64, 0};
        if (test == 3)
        {
            area = {0, 0, 32, 32};
            options.transform.e = 16;
            options.transform.f = 20;
        }
        if (test == 4)
        {
            area = {0, 0, 64, 32};
            options.fit = vector_fit::CONTAIN;
        }
        if (test == 5)
        {
            area = {0, 0, 64, 32};
            options.fit = vector_fit::COVER;
        }
        CHECK(u.draw_vector(area, image.image, options));
        end_frame(ctx);
        std::vector<u8> pixels(64 * 64 * 4);
        CHECK(base->read_pixels(surface->scene_target(), 0, 0, 64, 64, pixels.data()));
        if (test < 3)
            CHECK(pixels[(32 * 64 + 32) * 4] == 255 && pixels[(32 * 64 + 32) * 4 + 3] == 255);
        if (test == 3)
            CHECK(pixels[(36 * 64 + 32) * 4] == 255 && pixels[(18 * 64 + 18) * 4 + 3] == 0);
        if (test == 4)
            CHECK(pixels[(16 * 64 + 32) * 4] == 255 && pixels[(16 * 64 + 8) * 4 + 3] == 0);
        if (test == 5)
            CHECK(pixels[(16 * 64 + 32) * 4] == 255 && pixels[(36 * 64 + 32) * 4 + 3] == 0);
        CHECK(!violation_count(ctx) && vulkan_statistics(base).validation_errors == 0);
        frames.push_back(std::move(pixels));
    }
    CHECK(frames[0] == frames[1] && frames[0] == frames[2]);
    auto *native = static_cast<vk_detail::surface *>(surface);
    native->extent = {0, 0};
    begin_frame(ctx, *win, 7.0 / 60, 1.0 / 60);
    CHECK(u.draw_vector({0, 0, 64, 64}, image.image));
    end_frame(ctx);
    native->extent = {64, 64};
    native->dirty = true;
    begin_frame(ctx, *win, 8.0 / 60, 1.0 / 60);
    base->clear({0, 0, 0, 0});
    CHECK(u.draw_vector({0, 0, 64, 64}, image.image));
    end_frame(ctx);
    std::vector<u8> recreated(64 * 64 * 4);
    CHECK(base->read_pixels(surface->scene_target(), 0, 0, 64, 64, recreated.data()));
    CHECK(recreated == frames[0]);
    destroy_vector(image.image);
    destroy_context(ctx);
    base->destroy_surface(surface);
    destroy_vulkan_device(base);
    return frames;
}
static void vector_native_bounds_regressions()
{
    const auto direct = vector_native_bounds_pixels(vulkan_upload_mode::DIRECT, false);
    const auto staged = vector_native_bounds_pixels(vulkan_upload_mode::STAGED, false);
    const auto cached = vector_native_bounds_pixels(vulkan_upload_mode::DIRECT, true);
    const auto cached_staged = vector_native_bounds_pixels(vulkan_upload_mode::STAGED, true);
    CHECK(direct.size() == 6 && direct == staged && direct == cached && direct == cached_staged);
    if (!g_failures)
        std::puts("all native vector affine/fit/suspended/recreated-target regressions passed (24 "
                  "exact frames)");
}
