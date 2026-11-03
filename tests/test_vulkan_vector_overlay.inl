struct vector_overlay_test_host : window_host
{
    render_device *device = nullptr;
    render_surface *live = nullptr;
    i32 opens = 0, moves = 0, closes = 0;
    void move_window(window &, f32, f32) override {}
    void resize_window(window &, f32, f32) override {}
    bool popup_bounds(window &, rect &area) override
    {
        area = {-1000, -1000, 2000, 2000};
        return true;
    }
    bool open_overlay(window &parent, rect area, overlay_handle &out) override
    {
        live = create_vulkan_surface(
            device, {.width = static_cast<i32>(std::ceil(area.w * parent.scale)),
                     .height = static_cast<i32>(std::ceil(area.h * parent.scale))});
        out.native = live;
        out.surface = live;
        ++opens;
        return live != nullptr;
    }
    void move_overlay(window &, rect, overlay_handle &) override { ++moves; }
    void close_overlay(window &, overlay_handle &handle) override
    {
        if (handle.surface) device->destroy_surface(handle.surface);
        handle = {};
        live = nullptr;
        ++closes;
    }
};
static std::vector<std::vector<u8>> vector_overlay_pixels(vulkan_upload_mode mode, bool native)
{
    std::vector<std::vector<u8>> captures;
    auto *base = create_vulkan_device(
        {.validation = true, .vector_text = false, .upload_mode = mode, .vector_paths = native});
    CHECK(base);
    if (!base) return captures;
    auto *surface = create_vulkan_surface(base, {.width = 64, .height = 64});
    auto *ctx = create_context(base, surface);
    auto *win = add_window(ctx, nullptr, surface, {0, 0, 64, 64});
    vector_overlay_test_host host;
    host.device = base;
    set_window_host(ctx, &host);
    ui u(ctx);
    auto image =
        load_svg("<svg viewBox='0 0 64 64'><rect width='64' height='64' fill='red'/></svg>");
    CHECK(image);
    CHECK(u.prepare_vector(image.image, {.max_pixel_size = {64, 64}, .max_instances = 1}));
    for (i32 dpi = 0; dpi < 2; ++dpi)
        for (i32 repetition = 0; repetition < 2; ++repetition)
        {
            const f32 scale = static_cast<f32>(1 << dpi);
            set_window_scale(*win, scale);
            begin_frame(ctx, *win, (dpi * 2 + repetition) / 60.0, 1.0 / 60);
            base->clear({10, 20, 30, 255});
            const rect area{(48.0f + repetition * 4) / scale, 8 / scale, 40 / scale, 40 / scale};
            CHECK(detail::overlay_begin(ctx, "vector_overlay"_id, area));
            u.draw_rect(area, {0, 0, 255, 255});
            u8 alpha = 0;
            {
                auto clip =
                    u.region({area.x, area.y, 20 / scale, 40 / scale}, "vector_overlay_clip"_id);
                auto disabled = u.disabled();
                alpha = static_cast<u8>(255 * ctx->disabled_alpha);
                CHECK(u.draw_vector(area, image.image));
                CHECK(u.draw_vector(area, image.image).error.code == vector_error_code::LIMIT);
            }
            u.draw_rect({area.x + 28 / scale, area.y + 16 / scale, 8 / scale, 8 / scale},
                        {0, 255, 0, 255});
            detail::overlay_end(ctx);
            end_frame(ctx);
            CHECK(host.live);
            std::vector<u8> pixels(40 * 40 * 4), parent(64 * 64 * 4);
            CHECK(base->read_pixels(host.live->scene_target(), 0, 0, 40, 40, pixels.data()));
            CHECK(base->read_pixels(surface->scene_target(), 0, 0, 64, 64, parent.data()));
            const usize left = (20 * 40 + 10) * 4, right = (20 * 40 + 24) * 4,
                        green = (20 * 40 + 32) * 4;
            CHECK(pixels[left] == alpha && pixels[left + 2] == 255 - alpha &&
                  pixels[left + 3] == 255);
            CHECK(pixels[right] == 0 && pixels[right + 2] == 255 && pixels[right + 3] == 255);
            CHECK(pixels[green] == 0 && pixels[green + 1] == 255 && pixels[green + 3] == 255);
            CHECK(parent[(20 * 64 + 60) * 4] == 10 && parent[(20 * 64 + 60) * 4 + 2] == 30);
            CHECK(!violation_count(ctx) && vulkan_statistics(base).validation_errors == 0);
            captures.push_back(std::move(pixels));
        }
    CHECK(host.opens == 1 && host.moves >= 1);
    begin_frame(ctx, *win, 5.0 / 60, 1.0 / 60);
    end_frame(ctx);
    CHECK(host.closes == 1 && !host.live);
    destroy_vector(image.image);
    destroy_context(ctx);
    base->destroy_surface(surface);
    destroy_vulkan_device(base);
    return captures;
}
static void vector_overlay_regressions()
{
    const auto direct = vector_overlay_pixels(vulkan_upload_mode::DIRECT, true);
    const auto staged = vector_overlay_pixels(vulkan_upload_mode::STAGED, true);
    const auto portable = vector_overlay_pixels(vulkan_upload_mode::DIRECT, false);
    const auto portable_staged = vector_overlay_pixels(vulkan_upload_mode::STAGED, false);
    CHECK(direct.size() == 4 && direct == staged && direct == portable &&
          direct == portable_staged);
    if (!g_failures)
        std::puts("all vector overlay regressions passed (16 exact "
                  "native/portable/direct/staged/DPI/clip/disabled frames)");
}
