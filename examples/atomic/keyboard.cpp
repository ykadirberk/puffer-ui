// keyboard — the focus ring: Tab walks every widget, keys activate.
//
// Shows the built-in keyboard navigation with nothing wired up: Tab moves
// focus through the submission-order ring (Shift+Tab goes back), Enter or
// Space activates a focused button or checkbox, and Left/Right adjust a
// focused slider. Every built-in widget draws the theme's focus outline
// while it owns keyboard focus.
//
//   pui_ex_keyboard
#include "../example_common.h"

static i32 g_clicks = 0;
static bool g_enabled = true;
static bool g_notify = false;
static f32 g_volume = 45.0f; // 0..100
static std::string g_name = "Ada";

static void keyboard_frame(ui &u, example_app &app)
{
    const theme &th = u.th();
    example_page page =
        example_begin_page(u, app, "Keyboard", "Tab rings, Enter/Space, arrow keys");
    column col(page.content, example_ui::SECTION_GAP);

    {
        const rect body = example_section(
            u, col.next(212.0f), "The focus ring",
            "Tab / Shift+Tab move focus - Enter, Space and the arrows act on the focused widget",
            app.font_bold);
        column c(body, 6.0f);
        if (u.button(c.next(30.0f), "Click, Enter or Space", "kb_go"_id)) ++g_clicks;
        (void)u.checkbox(c.next(26.0f), "Enabled", g_enabled, "kb_enabled"_id);
        (void)u.checkbox(c.next(26.0f), "Show notifications", g_notify, "kb_notify"_id);
        (void)u.slider_float(c.next(28.0f), "Volume", g_volume, 0.0f, 100.0f, "kb_volume"_id,
                             "%.0f%%");
        c.space(4.0f);
        char line[160];
        std::snprintf(line, sizeof(line), "clicks: %d   enabled: %s   notify: %s   volume: %.0f%%",
                      g_clicks, g_enabled ? "on" : "off", g_notify ? "on" : "off",
                      static_cast<double>(g_volume));
        u.text(c.next(18.0f), line, th.text_dim, ALIGN_LEFT);
    }

    {
        const rect body = example_section(u, col.next(120.0f), "Fields join the same ring",
                                          "a text field keeps Enter (commit) and Escape (blur); "
                                          "Tab leaves it and selects the value",
                                          app.font_bold);
        column c(body, 6.0f);
        (void)u.text_field(c.next(26.0f), g_name, "kb_name"_id);
        u.text(c.next(18.0f), g_name.empty() ? "(the field is empty)" : g_name.c_str(), th.text_dim,
               ALIGN_LEFT);
    }
}

int main(int argc, char **argv)
{
    return example_run("keyboard", 640, 460, argc, argv, keyboard_frame);
}
