// pickers — an HSV color picker with an eyedropper, and a date picker.
//
// Shows: `comp::color_picker` (saturation/value square, hue and alpha bars,
// hex, the eyedropper when the device can read pixels) and `comp::date_picker`
// (a month grid in a popup; arrows, PageUp/PageDown, Enter).
//
//   pui_ex_pickers
#include "../example_common.h"

static color g_color{86, 156, 255, 255};
static date g_due{2026, 10, 15};

static void frame(ui &u, example_app &app)
{
    const theme &th = u.th();
    auto page = example_begin_page(u, app, "Pickers", "HSV color with an eyedropper, and dates");
    rect content = page.content;
    rect left = content.cut_left(content.w * 0.62f);
    (void)content.cut_left(example_ui::SECTION_GAP);

    rect colors = example_section(u, left, "Color",
                                  "drag the square and the bars; Eyedropper samples the window",
                                  app.font_bold);
    (void)comp::color_picker(u, colors.cut_top(min2(colors.h, 190.0f)), g_color,
                             {.id = "accent"_id});

    rect dates = example_section(u, content, "Date", "click, then arrows / PageUp / PageDown",
                                 app.font_bold);
    rect field = dates.cut_top(u.control_h());
    (void)comp::date_picker(u, field, g_due, {.id = "due"_id});
    (void)dates.cut_top(u.spacing());
    char line[64];
    std::snprintf(line, sizeof(line), "weekday %d (0 = Monday)", weekday(g_due));
    u.text(dates.cut_top(u.line_height()), line, th.text_dim, ALIGN_LEFT);
}

int main(int argc, char **argv)
{
    return example_run("pickers", 820, 420, argc, argv, frame);
}
