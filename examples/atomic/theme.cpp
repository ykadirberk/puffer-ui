// theme — colors, metrics, roles and live theme switching.
//
// Shows: the `theme` struct, `set_theme`, `set_button_role`, per-style groups
// (button/panel/card/scrollbar), fonts and text size - with a live preview.
//
//   pui_ex_theme   (flip light/dark, pick an accent, drag radius/border)
#include "../example_common.h"

static bool g_light = false;
static color g_accent = {60, 180, 255, 255};
static f32 g_radius = 4.0f;
static f32 g_border = 0.0f;
static std::string g_field = "themed text field";

static u8 lift(u8 v, i32 d)
{
    const i32 x = static_cast<i32>(v) + d;
    return static_cast<u8>(x > 255 ? 255 : x);
}

static void theme_frame(ui &u, example_app &app)
{
    // Build and apply a theme each frame (set_theme copies it).
    theme t = default_dark();
    t.font = app.font;
    t.text_size = 15.0f;
    if (g_light)
    {
        t.bg = {242, 244, 248, 255};
        t.panel_bg = {255, 255, 255, 255};
        t.border = {198, 204, 212, 255};
        t.text = {24, 28, 34, 255};
        t.text_dim = {108, 116, 126, 255};
        t.widget_bg = {228, 232, 238, 255};
        t.widget_hover = {214, 220, 228, 255};
        t.widget_active = {196, 204, 214, 255};
        t.selection = {60, 120, 200, 90};
        t.caret = {24, 28, 34, 255};
        t.panel.titlebar_bg = {236, 240, 246, 255};
        t.scrollbar.track = {222, 226, 232, 160};
        t.scrollbar.thumb = {176, 184, 194, 230};
        t.scrollbar.thumb_hover = {150, 158, 170, 240};
    }
    t.accent = g_accent;
    t.accent_hover = {lift(g_accent.r, 30), lift(g_accent.g, 30), lift(g_accent.b, 20), 255};
    t.radius = g_radius;
    t.border_thickness = g_border;
    set_button_role(t, "primary"_id,
                    button_override{.bg = some(g_accent), .hover_bg = some(t.accent_hover)});
    set_button_role(t, "danger"_id,
                    button_override{.bg = some(color{190, 60, 60, 255}),
                                    .hover_bg = some(color{215, 80, 80, 255})});
    set_theme(app.ctx, t);

    const theme &th = u.th();
    example_page page =
        example_begin_page(u, app, "Theme", "colors, metrics, roles and a live preview");
    column col(page.content, example_ui::SECTION_GAP);

    {
        const rect body = example_section(u, col.next(202.0f), "Colors and metrics",
                                          "set_theme() applies immediately; roles restyle buttons",
                                          app.font_bold);
        column c(body, 8.0f);
        {
            row r(c.next(26.0f), 8.0f);
            (void)u.checkbox(r.next(130.0f), "Light theme", g_light, "th_light"_id);
        }

        // Accent swatches: plain interact + draw, no built-in widget needed.
        {
            row r(c.next(28.0f), 8.0f);
            u.text(r.next(70.0f), "accent", th.text, ALIGN_LEFT);
            static const color choices[4] = {{60, 180, 255, 255},
                                             {120, 210, 130, 255},
                                             {220, 160, 60, 255},
                                             {200, 110, 210, 255}};
            for (i32 i = 0; i < 4; ++i)
            {
                const rect sw = r.next(36.0f);
                interaction in = u.interact(u.auto_id(), sw);
                if (in.hovered) u.set_cursor(CURSOR_HAND);
                u.draw_rounded_rect(sw, choices[i], 6.0f);
                if (in.hovered) u.draw_rounded_rect(sw.pad(4.0f), th.bg, 4.0f);
                if (in.clicked) g_accent = choices[i];
                if (g_accent.r == choices[i].r && g_accent.g == choices[i].g &&
                    g_accent.b == choices[i].b)
                    u.draw_rounded_rect(sw.pad(2.0f), th.bg, 5.0f);
            }
        }

        (void)u.slider_float(c.next(28.0f), "theme radius", g_radius, 0.0f, 14.0f, "th_radius"_id,
                             "%.0f");
        (void)u.slider_float(c.next(28.0f), "border", g_border, 0.0f, 2.0f, "th_border"_id, "%.1f");
    }

    // Live preview of the style groups.
    {
        const rect body =
            example_section(u, col.next(210.0f), "Live preview",
                            "buttons, cards, fields and progress follow the theme", app.font_bold);
        column c(body, 8.0f);
        {
            row r(c.next(32.0f), 8.0f);
            (void)u.button(r.next(120.0f), "Default", "th_b1"_id);
            (void)u.button(r.next(120.0f), "Primary", "th_b2"_id, "primary"_id);
            (void)u.button(r.next(120.0f), "Danger", "th_b3"_id, "danger"_id);
            u.text(r.remaining(), "button roles", th.text_dim, ALIGN_RIGHT);
        }
        {
            const rect card_r = c.next(64.0f);
            u.card(card_r);
            column inner(card_r.pad(10.0f), 4.0f);
            u.text(inner.next(18.0f), "card_style (bg, border, radius)", th.text, ALIGN_LEFT);
            u.text(inner.next(18.0f), "the theme's card group sizes this", th.text_dim, ALIGN_LEFT);
        }

        {
            row r(c.next(30.0f), 8.0f);
            u.text_field(r.next(240.0f), g_field, "th_field"_id);
            const rect bar = r.next(160.0f);
            u.progress_bar(bar, 0.6f, th.accent, th.widget_bg);
            u.text(r.remaining(), "field / progress use theme colors", th.text_dim, ALIGN_RIGHT);
        }
    }
}

int main(int argc, char **argv)
{
    return example_run("theme", 720, 540, argc, argv, theme_frame);
}
