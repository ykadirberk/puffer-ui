#include "../example_common.h"
#include "../material_icons.h"
#include <algorithm>

struct svg_icon_example
{
    static constexpr color COLORS[] = {{160, 177, 202, 255}, {248, 128, 132, 255}};
    static constexpr f32 MAX_PIXELS = 256;
    vector_handle icon = nullptr;
    bool favorite = false, failed = false;

    void fail(example_app &app, vector_error error)
    {
        std::fprintf(stderr, "%s: %s (byte %zu)\n", example_assets::FAVORITE_SVG, error.message,
                     error.offset);
        failed = true;
        app.running = false;
    }

    void prepare(example_app &app)
    {
        const auto source = example_assets::load_svg_file(example_assets::FAVORITE_SVG);
        if (!source)
        {
            fail(app, source.error);
            return;
        }
        icon = source.image;
        app.shutdown_user = this;
        app.before_shutdown = [](example_app &a, void *user)
        {
            auto &image = static_cast<svg_icon_example *>(user)->icon;
            (void)ui(a.ctx).release_vector(image);
            destroy_vector(image);
        };

        // The portable painter prepares each color. Native Vulkan shares the outline.
        for (const color current : COLORS)
        {
            const auto status =
                ui(app.ctx).prepare_vector(icon, {.max_pixel_size = {MAX_PIXELS, MAX_PIXELS},
                                                  .max_instances = 3,
                                                  .style = {.current_color = current}});
            if (!status)
            {
                fail(app, status.error);
                return;
            }
        }
    }

    void operator()(ui &u, example_app &app)
    {
        auto page =
            example_begin_page(u, app, "SVG icons", "Google Material Icons from a local file");
        const auto card = page.content;
        auto clip = u.region(card, "favorite_preview"_id);
        rect body =
            example_section(u, card, "Favorite", "One source at three sizes", app.font_bold);
        u.checkbox(body.cut_top(u.control_h()), "Mark as favorite", favorite, "favorite"_id);
        (void)body.cut_top(u.spacing());
        // Exercise both prepared palettes in the bounded example selftest.
        if (app.selftest && !app.no_script && app.frame_index == 2) favorite = true;

        struct sample
        {
            f32 size;
            const char *label;
        };
        static constexpr sample SAMPLES[] = {{16, "16 px"}, {32, "32 px"}, {64, "64 px"}};
        const f32 width = max2(0, (body.w - 2 * u.spacing()) / 3);
        row previews(body.cut_top(u.control_h() * 4), u.spacing());
        for (const auto &sample : SAMPLES)
        {
            rect cell = previews.next(width);
            if (cell.w <= 0 || cell.h <= 0) continue;
            u.card(cell);
            cell = cell.pad(u.spacing());
            u.text_ellipsis(cell.cut_bottom(u.control_h()), sample.label, u.th().text_dim,
                            ALIGN_CENTER);
            const f32 size = std::min({sample.size, cell.w, cell.h, MAX_PIXELS / app.win->scale});
            const rect area{cell.center_x() - size * .5f, cell.center_y() - size * .5f, size, size};
            const auto status =
                u.draw_vector(area, icon, {.style = {.current_color = COLORS[favorite]}});
            if (!status)
            {
                fail(app, status.error);
                return;
            }
        }
    }
};

int main(int argc, char **argv)
{
    svg_icon_example example;
    const int result = example_run("SVG icons", 620, 350, argc, argv, example);
    return example.failed ? 1 : result;
}
