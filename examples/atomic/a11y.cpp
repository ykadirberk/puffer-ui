// a11y — what a screen reader sees.
//
// Shows: the accessibility nodes every built-in widget records (`ui::a11y`,
// read back with `a11y_count` / `a11y_node`), describing a custom widget, and
// acting for assistive technology (`a11y_invoke`). On Windows the SDL3 bootstrap
// exposes every window to UI Automation (Narrator, NVDA, JAWS); recording starts
// with the first query, so this example switches it on itself to show the tree.
//
//   pui_ex_a11y   (start Narrator with Win+Ctrl+Enter, then Tab through)
#include "../example_common.h"

#include <string>

static bool g_wrap = true;
static f32 g_volume = 0.6f;
static std::string g_name = "Ada";
static i32 g_presses = 0;

static const char *role_name(a11y_role r)
{
    switch (r)
    {
    case a11y_role::BUTTON:
        return "button";
    case a11y_role::CHECKBOX:
        return "checkbox";
    case a11y_role::SLIDER:
        return "slider";
    case a11y_role::TEXT_FIELD:
        return "text field";
    case a11y_role::GROUP:
        return "group";
    default:
        return "other";
    }
}

static void frame(ui &u, example_app &app)
{
    const theme &th = u.th();
    set_accessibility(app.ctx, true); // normally the first screen reader query does this
    auto page = example_begin_page(u, app, "Accessibility", "What a screen reader sees");
    rect content = page.content;
    rect left = content.cut_left(content.w * 0.45f);
    (void)content.cut_left(example_ui::SECTION_GAP);

    rect controls = example_section(u, left, "Widgets", "each one describes itself", app.font_bold);
    column col(controls, u.spacing());
    if (u.button(col.next(u.control_h()), "Press me", "press"_id)) g_presses += 1;
    (void)u.checkbox(col.next(u.control_h()), "Wrap lines", g_wrap, "wrap"_id);
    (void)u.slider_float(col.next(u.control_h()), "Volume", g_volume, 0.0f, 1.0f, "volume"_id);
    (void)u.text_field(col.next(u.control_h()), g_name, "name"_id);

    // A custom widget: draw it, interact, then describe it.
    const rect swatch = col.next(u.control_h() * 1.5f);
    const interaction in = u.interact("swatch"_id, swatch);
    if (in.hovered) u.set_cursor(CURSOR_HAND);
    if (in.clicked) g_presses += 1;
    u.draw_rounded_rect(swatch, in.hovered ? th.accent_hover : th.accent, th.radius);
    u.text(swatch, "Custom swatch", th.bg, ALIGN_CENTER);
    u.a11y("swatch"_id, swatch, {.role = a11y_role::BUTTON, .name = "Custom swatch"});

    char line[96];
    std::snprintf(line, sizeof(line), "presses: %d", g_presses);
    u.text(col.next(u.control_h()), line, th.text_dim, ALIGN_LEFT);

    // The previous frame's tree, as an adapter reads it.
    rect tree = example_section(u, content, "Tree", "a11y_node(window, i)", app.font_bold);
    column rows(tree, 2.0f);
    const window &w = *u.current_window();
    for (i32 i = 0; i < a11y_count(w); ++i)
    {
        const a11y_node_view n = a11y_node(w, i);
        std::snprintf(line, sizeof(line), "%-10s %.*s%s%.*s%s%s", role_name(n.role),
                      static_cast<int>(n.name.size()), n.name.data(), n.value.empty() ? "" : " = ",
                      static_cast<int>(n.value.size()), n.value.data(),
                      (n.state & A11Y_CHECKED) ? "  [checked]" : "",
                      (n.state & A11Y_FOCUSED) ? "  [focused]" : "");
        const rect r = rows.next(u.line_height());
        if (r.h <= 0.0f) break;
        u.text(r, line, (n.state & A11Y_FOCUSED) ? th.text : th.text_dim, ALIGN_LEFT);
    }
}

int main(int argc, char **argv)
{
    return example_run("a11y", 820, 420, argc, argv, frame);
}
