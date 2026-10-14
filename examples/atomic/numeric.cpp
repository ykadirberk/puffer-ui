#include "../example_common.h"

static i64 count = 9007199254740993LL;
static f64 precise = 1.23456789012345, speed = 1.0;
static f32 position[] = {1, 2, 3};
static void frame(ui &u, example_app &app)
{
    auto page = example_begin_page(u, app, "Numeric editors",
                                   "64-bit precision, vectors, drag values and form rows");
    rect body = example_section(u, page.content, "Transform",
                                "Drag Speed or use Left/Right while focused", app.font_bold);
    column rows(body, u.spacing());
    u.int_field(comp::form_field(u, rows.next(u.control_h()), "Count", {.label_width = 100}), count,
                "count"_id);
    u.double_field(comp::form_field(u, rows.next(u.control_h()), "Precise", {.label_width = 100}),
                   precise, "precise"_id);
    u.vector_field(comp::form_field(u, rows.next(u.control_h()), "Position", {.label_width = 100}),
                   position, "position"_id);
    comp::drag_value(u, rows.next(u.control_h()), "Speed", speed,
                     {.id = "speed"_id, .speed = 0.1, .min = 0, .max = 100});
}

int main(int argc, char **argv)
{
    return example_run("numeric", 700, 420, argc, argv, frame);
}
