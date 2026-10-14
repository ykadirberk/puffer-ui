#include "../example_common.h"

static color value{86, 156, 255, 255};
static void frame(ui &u, example_app &app)
{
    auto page =
        example_begin_page(u, app, "Color editor", "RGBA channels with mouse and keyboard editing");
    rect body = example_section(u, page.content, "Accent",
                                "Each channel uses the standard slider interaction", app.font_bold);
    column rows(body, u.spacing());
    comp::color_edit(u, rows.next(u.control_h()), value, {.id = "color"_id});
    u.draw_rounded_rect(rows.next(u.control_h() * 3), value, u.th().radius);
}

int main(int argc, char **argv)
{
    return example_run("colors", 700, 420, argc, argv, frame);
}
