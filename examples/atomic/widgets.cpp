// widgets — checkbox, slider and progress bar.
//
// Shows: `checkbox`, `slider_float` (drag + click-to-jump + clamping) and
// `progress_bar`, all writing model values directly.
//
//   pui_ex_widgets
#include "../example_common.h"

static bool g_wrap = true;
static bool g_grid = false;
static f32 g_zoom = 1.0f;
static f32 g_quality = 35.0f; // 0..100

static void widgets_frame(ui &u, example_app &app)
{
    const theme &th = u.th();
    example_page page = example_begin_page(u, app, "Widgets", "checkbox, slider and progress bar");
    column col(page.content, example_ui::SECTION_GAP);

    {
        const rect body = example_section(
            u, col.next(200.0f), "Checkboxes and sliders",
            "checkbox(rect, label, bool&, id) / slider_float(rect, label, f32&, min, max, id)",
            app.font_bold);
        column c(body, 6.0f);
        (void)u.checkbox(c.next(26.0f), "Wrap long lines", g_wrap, "w_wrap"_id);
        (void)u.checkbox(c.next(26.0f), "Show grid", g_grid, "w_grid"_id);
        c.space(6.0f);
        (void)u.slider_float(c.next(28.0f), "Zoom", g_zoom, 0.5f, 3.0f, "w_zoom"_id, "%.2fx");
        (void)u.slider_float(c.next(28.0f), "Quality", g_quality, 0.0f, 100.0f, "w_quality"_id,
                             "%.0f%%");
    }

    {
        const rect body = example_section(u, col.next(136.0f), "Progress",
                                          "progress_bar(rect, fraction, fill, bg)", app.font_bold);
        column c(body, 6.0f);
        u.progress_bar(c.next(18.0f), g_quality / 100.0f, th.accent, th.widget_bg);
        {
            row r(c.next(14.0f), 8.0f);
            u.progress_bar(r.next(80.0f), 0.0f, th.accent, th.widget_bg);
            u.progress_bar(r.next(80.0f), 0.5f, th.accent, th.widget_bg);
            u.progress_bar(r.next(80.0f), 1.0f, th.accent, th.widget_bg);
        }
        c.space(6.0f);
        u.textf(c.next(18.0f), th.text_dim, ALIGN_LEFT,
                "wrap = %s   grid = %s   zoom = %.2f   quality = %.0f%%", g_wrap ? "on" : "off",
                g_grid ? "on" : "off", static_cast<double>(g_zoom), static_cast<double>(g_quality));
    }
}

int main(int argc, char **argv)
{
    return example_run("widgets", 640, 460, argc, argv, widgets_frame);
}
