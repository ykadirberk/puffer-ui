#include <pufferui/vulkan.h>
#include <cstdio>
#include <string>
#include <vector>
#include <chrono>
#include <cstring>
using namespace pui;
static i32 errors = 0;
static void diagnostic(i32 code, const char *message, void *)
{
    std::fprintf(stderr, "Vulkan %d: %s\n", code, message);
    ++errors;
}
#define REQUIRE(x)                                                                                 \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x);                                   \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
struct view_state
{
    context *ctx = nullptr;
    render_surface *surface = nullptr;
    window *win = nullptr;
    font_handle font = FONT_INVALID;
    std::string value = "field";
    std::vector<u8> pixels;
    i32 width = 320, height = 240;
    f32 offset = 0;
    i32 atlas_high = 0;
    bool saw_edit = false, saw_scroll = false;
    bool initialize(render_device *d)
    {
        surface = create_vulkan_surface(d, {.width = width, .height = height});
        if (!surface) return false;
        ctx = create_context(d, surface);
        win = add_window(ctx, nullptr, surface, rect::make(0, 0, width, height));
        font = load_font(ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
        theme theme = default_dark();
        theme.font = font;
        set_theme(ctx, theme);
        set_text_atlas_budget(ctx, 2);
        focus_window(ctx, *win);
        pixels.resize(static_cast<usize>(width) * height * 4);
        return font != FONT_INVALID;
    }
    void close(render_device *d)
    {
        destroy_context(ctx);
        d->destroy_surface(surface);
        ctx = nullptr;
        surface = nullptr;
    }
};
int main(int argc, char **argv)
{
    bool bench = false, cache = false, staged = false;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--bench") == 0)
            bench = true;
        else if (std::strcmp(argv[i], "--cache") == 0)
            cache = true;
        else if (std::strcmp(argv[i], "--staged") == 0)
            staged = true;
        else
            return 1;
    }
    vulkan_device_opts opts;
    opts.validation = !bench;
    opts.error_callback = diagnostic;
    opts.upload_mode = staged ? vulkan_upload_mode::STAGED : vulkan_upload_mode::DIRECT;
    render_device *d = create_vulkan_device(opts);
    REQUIRE(d);
    view_state views[2];
    for (i32 i = 0; i < (bench ? 1 : 2); ++i) REQUIRE(views[i].initialize(d));
    if (bench)
        std::puts("frame,total_ms,draws,vertices,indices,upload_bytes,upload_allocated_bytes,cache_"
                  "ms,cache_hits,cache_fallbacks,image_bytes,retired_bytes,cache_prepare_ms,cache_"
                  "bypasses");
    std::string alphabet;
    for (char ch = 33; ch < 127; ++ch) alphabet += ch;
    for (i32 f = 0; f < 180; ++f)
    {
        for (i32 variant = 0; variant < (bench ? 1 : 2); ++variant)
        {
            const auto start = std::chrono::steady_clock::now();
            const auto before = vulkan_statistics(d);
            auto &v = views[variant];
            if (f == 100)
            {
                v.close(d);
                v.width = 352;
                v.height = 256;
                REQUIRE(v.initialize(d));
            }
            vulkan_set_cache(d, bench ? cache : variant == 1);
            const f32 scale = f >= 80 && f < 100 ? 1.5f : 1.0f;
            set_window_scale(*v.win, scale);
            mouse_move(*v.win, 20 * scale, (f >= 40 && f < 55 ? 90 : 20) * scale);
            if (f == 1 || f == 101) mouse_button(*v.win, true);
            if (f == 2 || f == 102) mouse_button(*v.win, false);
            if (f == 30 || f == 130) text_input_event(*v.win, "X\xC4\x9F");
            if (f == 40 || f == 41) mouse_wheel(*v.win, 0, -2);
            if (f == 42) mouse_wheel(*v.win, 0, 1);
            theme theme = default_dark();
            theme.font = v.font;
            theme.text_gamma = f >= 140 ? 1.2f : 1.0f;
            set_theme(v.ctx, theme);
            begin_frame(v.ctx, *v.win, f / 30.0, 1.0 / 30.0);
            {
                ui u(v.ctx);
                const f32 width = v.width / scale;
                (void)u.text_field(rect::make(8, 8, width - 16, u.control_h()), v.value,
                                   "field"_id);
                if (f >= 20 && f < 24)
                {
                    auto size = u.text_style(96.0f + 16.0f * static_cast<f32>(f - 20), v.font);
                    REQUIRE(u.text_width(alphabet) > 0);
                }
                {
                    auto scroll = u.scroll(rect::make(8, 48, width - 16, 118), "list"_id);
                    rect content = scroll.content();
                    scroll.set_content_height(720);
                    for (i32 row = 0; row < 24; ++row)
                    {
                        auto scope = u.scope(static_cast<uiid>(row + 1));
                        rect area = content.cut_top(30);
                        u.draw_rect(area,
                                    row % 2 ? color{40, 70, 100, 90} : color{20, 40, 70, 190});
                        char text[32];
                        std::snprintf(text, sizeof(text), "row %d", row);
                        u.text(area, text, u.th().text);
                    }
                }
                v.offset = u.scroll_offset("list"_id);
                v.saw_scroll |= v.offset > 0;
                v.saw_edit |= v.value.find('X') != std::string::npos;
                u.draw_rect(rect::make(170, 175, 70, 45),
                            f >= 60 && f < 70 ? color{220, 30, 70, 200} : color{30, 100, 200, 160});
                if (f >= 60 && f < 70) u.blur(rect::make(160, 172, 80, 48), 8, 8);
                if (f >= 90 && f < 94)
                {
                    auto popup = u.popup("menu"_id, rect::make(80, 80, 110, 70));
                    u.draw_rect(popup.content(), {20, 200, 40, 190});
                }
                const i32 pages = text_atlas_pages(v.ctx);
                if (pages > v.atlas_high) v.atlas_high = pages;
            }
            end_frame(v.ctx);
            REQUIRE(violation_count(v.ctx) == 0 && vulkan_last_result(d) == VK_SUCCESS);
            if (!bench)
                REQUIRE(d->read_pixels(v.surface->scene_target(), 0, 0, v.width, v.height,
                                       v.pixels.data()));
            else
            {
                const auto after = vulkan_statistics(d);
                const auto number = [](u64 x) { return static_cast<unsigned long long>(x); };
                const f64 elapsed =
                    std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - start)
                        .count();
                std::printf("%d,%.6f,%llu,%llu,%llu,%llu,%llu,%.6f,%llu,%llu,%llu,%llu,%.6f,%llu\n",
                            f, elapsed, number(after.requested_draws - before.requested_draws),
                            number(after.requested_vertices - before.requested_vertices),
                            number(after.requested_indices - before.requested_indices),
                            number(after.upload_bytes - before.upload_bytes),
                            number(after.upload_allocated_bytes), after.cache_ms - before.cache_ms,
                            number(after.cache_hits - before.cache_hits),
                            number(after.cache_fallbacks - before.cache_fallbacks),
                            number(after.image_bytes), number(after.retired_bytes),
                            after.cache_prepare_ms - before.cache_prepare_ms,
                            number(after.cache_bypasses - before.cache_bypasses));
            }
        }
        if (!bench)
        {
            REQUIRE(views[0].pixels == views[1].pixels);
            REQUIRE(views[0].value == views[1].value && views[0].offset == views[1].offset);
        }
    }
    for (i32 i = 0; i < (bench ? 1 : 2); ++i)
    {
        auto &v = views[i];
        REQUIRE(v.saw_edit && v.saw_scroll && v.atlas_high > 1);
        v.close(d);
    }
    REQUIRE(vulkan_wait_idle(d));
    REQUIRE(vulkan_statistics(d).validation_errors == 0 && errors == 0);
    REQUIRE(vulkan_statistics(d).peak_cache_metadata_bytes <= opts.cache_budget_bytes);
    destroy_vulkan_device(d);
    if (!bench) std::puts("all 180 native interaction cache/reference frames matched");
    return 0;
}
