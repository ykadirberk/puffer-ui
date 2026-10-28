// Reuse only the existing fixture; all assertions in the original test stay fixed.
#define PUFFERUI_IMPLEMENTATION
#define main bitmap_interaction_main
#include "test_vulkan_interaction.cpp"
#undef main

int main(int argc, char **argv)
{
    const bool staged = argc == 2 && std::strcmp(argv[1], "--staged") == 0;
    if (argc > 2 || (argc == 2 && !staged)) return 1;
    vulkan_device_opts opts;
    opts.validation = true;
    opts.upload_mode = staged ? vulkan_upload_mode::STAGED : vulkan_upload_mode::DIRECT;
    opts.error_callback = diagnostic;
    render_device *devices[2]{create_vulkan_device(opts), nullptr};
    REQUIRE(devices[0]);
    const auto native = vulkan_handles(devices[0]);
    opts.native = &native;
    opts.tile_cache = true;
    devices[1] = create_vulkan_device(opts);
    REQUIRE(devices[1]);
    static_cast<vk_detail::device *>(devices[1])->text->batch_limit = 1;
    view_state views[2];
    for (i32 variant = 0; variant < 2; ++variant)
        REQUIRE(views[variant].initialize(devices[variant]));
    for (i32 f = 0; f < 180; ++f)
    {
        for (i32 variant = 0; variant < 2; ++variant)
        {
            auto &v = views[variant];
            auto *device = devices[variant];
            if (f == 100)
            {
                v.close(device);
                v.width = 352;
                v.height = 256;
                REQUIRE(v.initialize(device));
            }
            const f32 scale = f >= 80 && f < 100 ? 1.5f : 1.0f;
            set_window_scale(*v.win, scale);
            mouse_move(*v.win, 20 * scale, (f >= 40 && f < 55 ? 90 : 20) * scale);
            if (f == 1 || f == 101) mouse_button(*v.win, true);
            if (f == 2 || f == 102) mouse_button(*v.win, false);
            if (f == 30 || f == 130) text_input_event(*v.win, "X\xc4\x9f");
            if (f == 40 || f == 41) mouse_wheel(*v.win, 0, -2);
            theme style = default_dark();
            style.font = v.font;
            style.text_gamma = f >= 140 ? 1.2f : 1.0f;
            set_theme(v.ctx, style);
            begin_frame(v.ctx, *v.win, f / 30.0, 1.0 / 30.0);
            {
                ui u(v.ctx);
                const f32 width = v.width / scale;
                (void)u.text_field({8, 8, width - 16, u.control_h()}, v.value, "field"_id);
                {
                    auto scroll = u.scroll({8, 48, width - 16, 118}, "list"_id);
                    rect content = scroll.content();
                    scroll.set_content_height(720);
                    for (i32 row = 0; row < 24; ++row)
                    {
                        rect area = content.cut_top(30);
                        u.draw_rounded_rect(area, {20, 40, 70, 190}, 6);
                        u.text(area, "office ffi a\xcc\x81 \xd8\xb3\xd9\x84\xd8\xa7\xd9\x85",
                               u.th().text);
                    }
                }
                v.offset = u.scroll_offset("list"_id);
                v.saw_scroll |= v.offset > 0;
                v.saw_edit |= v.value.find('X') != std::string::npos;
                if (f >= 60 && f < 70) u.blur({160, 172, 80, 48}, 8, 8);
                if (f >= 90 && f < 94)
                {
                    auto popup = u.popup("menu"_id, {80, 80, 110, 70});
                    u.text(popup.content(), "affine", u.th().text);
                }
            }
            end_frame(v.ctx);
            REQUIRE(!violation_count(v.ctx) && vulkan_last_result(device) == VK_SUCCESS);
            REQUIRE(device->read_pixels(v.surface->scene_target(), 0, 0, v.width, v.height,
                                        v.pixels.data()));
        }
        REQUIRE(views[0].pixels == views[1].pixels);
        REQUIRE(views[0].value == views[1].value && views[0].offset == views[1].offset);
    }
    for (i32 variant = 1; variant >= 0; --variant)
    {
        REQUIRE(views[variant].saw_edit && views[variant].saw_scroll);
        REQUIRE(text_atlas_pages(views[variant].ctx) == 0);
        views[variant].close(devices[variant]);
        REQUIRE(vulkan_wait_idle(devices[variant]));
        REQUIRE(!vulkan_statistics(devices[variant]).validation_errors && !errors);
        destroy_vulkan_device(devices[variant]);
    }
    std::puts(
        "all 180 HarfBuzz/Slug field, scroll, DPI, gamma, popup and recreation frames matched");
    return 0;
}
