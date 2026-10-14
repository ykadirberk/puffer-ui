#include "../example_common.h"

static void frame(ui &u, example_app &app)
{
    if (app.frame_index == 0)
        set_font_fallbacks(app.ctx, std::span<const font_handle>(&app.font, 1));
    auto page =
        example_begin_page(u, app, "DPI and fonts",
                           "logical layout, physical glyph rasterization, ordered font fallback");
    rect body =
        example_section(u, page.content, "Display",
                        "Move the window between displays to follow their scale", app.font_bold);
    column rows(body, u.spacing());
    u.textf(rows.next(u.control_h()), u.th().text, ALIGN_LEFT, "Framebuffer scale: %.2f",
            app.win->scale);
    u.text(rows.next(u.control_h()), "AV: crisp text at the window's density", u.th().text);
    u.button(rows.next(u.control_h()), "Logical control height", "dpi_button"_id);
}

int main(int argc, char **argv)
{
    return example_run("dpi", 700, 420, argc, argv, frame);
}
