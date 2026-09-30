// pui_tour — the guided tour: every capability in one app.
//
// The left sidebar lists the chapters (grouped, scrollable); pick one with the
// mouse or Up/Down. Each chapter names the standalone example that shows the
// same capability in isolation (`pui_ex_<name>`, in examples/atomic/).
//
//   pui_tour
//   pui_tour --selftest
#include "dock_helpers.h"
#include "example_common.h"

#include <cmath>
#include <vector>

// ---------------------------------------------------------------- chapter state
static i32 g_chapter = 0;
static f32 g_sidebar_w = 230.0f;
static bool g_light = false;
static color g_accent = {60, 180, 255, 255};
static f32 g_radius = 4.0f;      // theme radius (Theme chapter)
static i32 g_kind = 0;           // combo selection (Menus chapter)
static f32 g_blur_radius = 8.0f; // blur radius (Blur chapter)
static f32 g_blur_alpha = 1.0f;  // blur alpha: 1 replaces the sharp content
static bool g_wrap = true, g_grid_on = false, g_reduced = false;
static f32 g_zoom = 1.0f, g_quality = 35.0f;
static std::string g_name = "PufferUI";
static f32 g_amount = 12.5f;
static char g_status[96] = "press Enter to submit";
static i32 g_clicks = 0;
static bool g_play = false;
static i32 g_appear_key = 0;
static i32 g_rating = 3, g_seg = 1;
static bool g_switch = true;
static bool g_menu_open = false, g_modal_open = false;
static bool g_opt = true;
static i32 g_stat = 0;
static i32 g_selected = 2, g_scroll_cmd = 0;
static i32 g_left_count = 0, g_right_count = 0;

// ---------------------------------------------------------------- chapters
using chapter_fn = void (*)(ui &, example_app &, rect);

struct tour_chapter
{
    const char *group;
    const char *name;
    const char *blurb;
    const char *target;
    chapter_fn draw;
};

// The tour's first chapter: what the library's bootstrap hands you.
static void ch_bootstrap(ui &u, example_app &app, rect body)
{
    const theme &th = u.th();
    (void)app;
    column col(body, 8.0f);
    {
        const rect card = example_section(u, col.next(64.0f), "One call in",
                                          "sdl3_app_init: window + renderer + device + context "
                                          "+ window + native chrome",
                                          app.font_bold);
        u.text_wrapped(card,
                       "The bootstrap owns the SDL side; you own fonts, the "
                       "theme and the frame body. sdl3_app_pump routes every "
                       "event and keeps the client rect in sync.",
                       th.text_dim);
    }
    {
        const rect card = example_section(
            u, col.next(64.0f), "The frame loop",
            "pump -> tick -> begin_frame -> draw -> end_frame -> present", app.font_bold);
        u.text_wrapped(card,
                       "begin_frame takes the window and the app clock; the "
                       "ui scope draws; end_frame flushes the draw list. "
                       "pui_ex_bootstrap is the complete, runnable version.",
                       th.text_dim);
    }
    {
        const rect card = example_section(u, col.next(64.0f), "Native chrome",
                                          "the system's own snap, drag and resize", app.font_bold);
        u.text_wrapped(card,
                       "The bar minus the buttons is the system caption: "
                       "dragging shows the system's snap preview, edges and "
                       "corners resize natively, and a maximized window "
                       "restores when dragged.",
                       th.text_dim);
    }
}

static void ch_layout(ui &u, example_app &app, rect body)
{
    const theme &th = u.th();
    (void)app;
    row cols(body, 10.0f);
    rect left = cols.next(body.w * 0.5f);
    rect right = cols.remaining();

    // mutating cuts + ratio
    {
        rect area = left.cut_top(64.0f);
        const rect top = area.cut_top(22.0f);
        u.draw_rect(top, th.widget_bg);
        u.text(top.pad(8.0f, 0.0f), "cut_top(22)", th.text, ALIGN_LEFT);
        const rect ratio = area.cut_right_ratio(0.4f);
        u.draw_rect(ratio, th.widget_hover);
        u.text(ratio, "cut_right_ratio(0.4)", th.text, ALIGN_CENTER);
        u.draw_rect(area, th.widget_bg);
        u.text(area.pad(8.0f, 0.0f), "the rest", th.text_dim, ALIGN_LEFT);
        (void)left.cut_top(8.0f);
    }
    // non-mutating slices + align + fit_aspect
    {
        const rect area = left.cut_top(72.0f);
        u.draw_rect(area, th.widget_bg);
        const rect video = area.pad(6.0f).fit_aspect(16.0f / 9.0f);
        u.draw_rounded_rect(video, th.panel_bg, 4.0f);
        u.text(video, "fit_aspect(16:9)", th.text_dim, ALIGN_CENTER);
        const rect badge = rect::make(0.0f, 0.0f, 70.0f, 18.0f);
        u.draw_rounded_rect(badge.align_right(area.pad(6.0f)).align_top(area.pad(6.0f)), th.accent,
                            3.0f);
        u.text(badge.align_right(area.pad(6.0f)).align_top(area.pad(6.0f)), "align_right", th.bg,
               ALIGN_CENTER);
    }

    // tracks
    u.text(right.cut_top(18.0f), "track_row: fixed(70) | flex(2) | flex()", th.text_dim,
           ALIGN_LEFT);
    {
        track_row tr(right.cut_top(30.0f),
                     {track_size::fixed(70.0f), track_size::flex(2.0f), track_size::flex()}, 6.0f);
        const rect a = tr.next();
        const rect b = tr.next();
        const rect c = tr.next();
        u.draw_rect(a, th.widget_bg);
        u.draw_rect(b, th.accent);
        u.draw_rect(c, th.panel_bg);
        u.text(a, "fixed", th.text, ALIGN_CENTER);
        u.text(b, "flex 2", th.bg, ALIGN_CENTER);
        u.text(c, "flex 1", th.text, ALIGN_CENTER);
    }
    (void)right.cut_top(8.0f);
    // auto-fit grid
    u.text(right.cut_top(18.0f), "auto_fit_grid reflows with the width", th.text_dim, ALIGN_LEFT);
    {
        const i32 count = 6;
        grid_cursor probe =
            auto_fit_grid(rect::make(0.0f, 0.0f, right.w, 0.0f), count, 110.0f, 34.0f, 6.0f);
        const f32 gh =
            static_cast<f32>(probe.rows()) * 34.0f + static_cast<f32>(probe.rows() - 1) * 6.0f;
        grid_cursor g = auto_fit_grid(right.cut_top(gh), count, 110.0f, 34.0f, 6.0f);
        for (i32 i = 0; i < g.count(); ++i)
        {
            const rect cell = g.next();
            u.draw_rect(cell, i == 0 ? th.accent : th.widget_bg);
            char label[24];
            std::snprintf(label, sizeof(label), "cell %d", i + 1);
            u.text(cell, label, i == 0 ? th.bg : th.text, ALIGN_CENTER);
        }
    }
}

static void ch_cursors(ui &u, example_app &app, rect body)
{
    const theme &th = u.th();
    (void)app;
    column col(body, 6.0f);
    const rect header = col.cut_top(30.0f);
    const rect footer = col.cut_bottom(24.0f);
    u.draw_rect(header, th.panel_bg);
    u.text(header.pad(8.0f, 0.0f), "col.cut_top(30)", th.text, ALIGN_LEFT);
    u.draw_rect(footer, th.panel_bg);
    u.text(footer.pad(8.0f, 0.0f), "col.cut_bottom(24)", th.text_dim, ALIGN_LEFT);

    row main(col.remaining(), 8.0f);
    const rect side = main.next(140.0f);
    u.draw_rect(side, th.widget_bg);
    column sc(side.pad(6.0f), 4.0f);
    for (i32 i = 0; i < 5; ++i)
    {
        char label[24];
        std::snprintf(label, sizeof(label), "row.next(%d)", i + 1);
        u.text(sc.next(18.0f), label, th.text_dim, ALIGN_LEFT);
    }
    column cc(main.remaining(), 6.0f);
    for (i32 i = 0; i < 3; ++i)
    {
        const rect item = cc.next(30.0f);
        u.draw_rounded_rect(item, th.panel_bg, 4.0f);
        u.text(item.pad(8.0f, 0.0f), "column.next(30)", th.text, ALIGN_LEFT);
    }
    const rect clamped = cc.next(400.0f);
    u.draw_rect(clamped, th.accent);
    u.text(clamped.pad(8.0f, 0.0f), "next(400) clamps to what is left", th.bg, ALIGN_LEFT);
}

static void id_counter(ui &u, const theme &th, rect r, const char *label, uiid key, i32 &value)
{
    region reg(u, key, r);
    column c(reg.content().pad(8.0f), 4.0f);
    u.text(c.next(16.0f), label, th.text, ALIGN_LEFT);
    if (u.button(c.next(24.0f), "count", u.local("inc"))) value += 1;
    char line[48];
    std::snprintf(line, sizeof(line), "value %d", value);
    u.text(c.next(16.0f), line, th.text_dim, ALIGN_LEFT);
}

static void ch_ids(ui &u, example_app &app, rect body)
{
    const theme &th = u.th();
    (void)app;
    column col(body, 8.0f);
    u.text(col.next(18.0f), "one widget function, two regions: ids stay independent", th.text_dim,
           ALIGN_LEFT);
    {
        row r(col.next(78.0f), 8.0f);
        id_counter(u, th, r.next(150.0f), "left", "left"_id, g_left_count);
        id_counter(u, th, r.next(150.0f), "right", "right"_id, g_right_count);
    }
    u.text(col.next(18.0f), "unlabeled widgets can use auto_id() (draw-order stable)", th.text_dim,
           ALIGN_LEFT);
    {
        row r(col.next(34.0f), 6.0f);
        for (i32 i = 0; i < 4; ++i)
        {
            const rect sw = r.next(90.0f);
            interaction in = u.interact(u.auto_id(), sw);
            u.draw_rounded_rect(sw,
                                in.held      ? th.accent
                                : in.hovered ? th.widget_hover
                                             : th.widget_bg,
                                4.0f);
            char label[24];
            std::snprintf(label, sizeof(label), "auto #%d", i + 1);
            u.text(sw, label, in.held ? th.bg : th.text, ALIGN_CENTER);
        }
    }
    char line[160];
    std::snprintf(line, sizeof(line), "\"left\"_id = 0x%016llX   id_child mixes a salt",
                  static_cast<unsigned long long>("left"_id));
    u.text(col.next(18.0f), line, th.text_dim, ALIGN_LEFT);
}

static void ch_text(ui &u, example_app &app, rect body)
{
    const theme &th = u.th();
    column col(body, 6.0f);
    {
        text_scope title = u.text_style(22.0f, app.font_bold);
        u.text(col.next(28.0f), "Bold title via text_style", th.text, ALIGN_LEFT);
    }
    {
        text_scope oblique = u.text_style(14.0f, app.font_oblique);
        u.text(col.next(18.0f), "oblique via text_style(size, font)", th.text_dim, ALIGN_LEFT);
    }
    {
        const rect r = col.next(20.0f);
        u.draw_rect(r, th.widget_bg);
        u.text(r, "left", th.text, ALIGN_LEFT);
        u.text(r, "center", th.text, ALIGN_CENTER);
        u.text(r, "right", th.text, ALIGN_RIGHT);
    }
    u.text(col.next(18.0f),
           "T\xC3\xBCrk\xC3\xA7"
           "e \xC4\x9F\xC3\xBC\xC5\x9F\xC3\xB6 | \xCE\xB1\xCE\xB2\xCE\xB3 "
           "\xCE\x94 | x \xC3\x97 y \xC3\xB7 \xC2\xB1 | \xE2\x82\xBA \xE2\x82\xAC \xE2\x86\x92",
           th.text, ALIGN_LEFT);
    u.text_ellipsis(col.next(18.0f), "ellipsis: this label is too long for its rect and is trimmed",
                    th.text_dim, ALIGN_LEFT);
    {
        const char *s = "text_wrapped breaks on words; measure_text gives the height up front.";
        const measure_size m = u.measure_text(s, body.w);
        u.text_wrapped(col.next(m.height), s, th.text);
        char line[96];
        std::snprintf(line, sizeof(line), "measure_text -> %.0f x %.0f",
                      static_cast<double>(m.width), static_cast<double>(m.height));
        u.text(col.next(16.0f), line, th.text_dim, ALIGN_LEFT);
    }
}

static void ch_buttons(ui &u, example_app &app, rect body)
{
    const theme &th = u.th();
    (void)app;
    column col(body, 8.0f);
    {
        row r(col.next(28.0f), 6.0f);
        if (u.button(r.next(110.0f), "Default", "tr_b1"_id)) g_clicks += 1;
        if (u.button(r.next(110.0f), "Primary", "tr_b2"_id, "primary"_id)) g_clicks += 1;
        if (u.button(r.next(110.0f), "Danger", "tr_b3"_id, "danger"_id)) g_clicks += 1;
        char line[48];
        std::snprintf(line, sizeof(line), "clicks: %d", g_clicks);
        u.text(r.remaining(), line, th.text_dim, ALIGN_RIGHT);
    }
    {
        row r(col.next(32.0f), 6.0f);
        u.button(r.next(120.0f), "outlined", "tr_b4"_id, 0,
                 button_override{.bg = some(color{0, 0, 0, 0}),
                                 .border = some(th.accent),
                                 .border_thickness = some(1.5f)});
        u.button(r.next(120.0f), "pill", "tr_b5"_id, 0,
                 button_override{.radius = some(15.0f), .pad_x = some(16.0f)});
        u.button(r.next(120.0f), "square", "tr_b6"_id, 0, button_override{.radius = some(0.0f)});
    }
    u.text(col.next(18.0f), "style_scope restyles the buttons inside it", th.text_dim, ALIGN_LEFT);
    {
        style_scope warn(u, button_override{.bg = some(color{200, 150, 40, 255}),
                                            .hover_bg = some(color{230, 180, 60, 255}),
                                            .text = some(color{25, 25, 25, 255})});
        row r(col.next(28.0f), 6.0f);
        u.button(r.next(150.0f), "scoped A", "tr_b7"_id);
        u.button(r.next(150.0f), "scoped B", "tr_b8"_id);
    }
    const button_style st = u.resolve_button_style("primary"_id);
    char line[128];
    std::snprintf(line, sizeof(line), "resolve_button_style(\"primary\"): radius %.1f, pad_x %.1f",
                  static_cast<double>(st.radius), static_cast<double>(st.pad_x));
    u.text(col.next(18.0f), line, th.text_dim, ALIGN_LEFT);
}

static void ch_inputs(ui &u, example_app &app, rect body)
{
    const theme &th = u.th();
    (void)app;
    column col(body, 8.0f);
    {
        track_row form(col.next(28.0f),
                       {track_size::fixed(70.0f), track_size::flex(), track_size::fixed(70.0f),
                        track_size::fixed(110.0f)},
                       6.0f);
        u.text(form.next(), "Name", th.text, ALIGN_LEFT);
        u.text_field(form.next(), g_name, "tr_name"_id);
        u.text(form.next(), "Amount", th.text, ALIGN_LEFT);
        u.number_field(form.next(), g_amount, "tr_amount"_id, "%.2f");
    }
    if (u.key_pressed(key::ENTER))
        std::snprintf(g_status, sizeof(g_status), "submitted: %s = %.2f", g_name.c_str(),
                      static_cast<double>(g_amount));
    u.text(col.next(16.0f), g_status, th.text_dim, ALIGN_LEFT);
    u.text(col.next(16.0f), "Tab moves focus, Escape clears it, Ctrl+A/C/X/V use the clipboard",
           th.text_dim, ALIGN_LEFT);
    u.text(col.next(16.0f), "fields write straight into app state - no getters", th.text_dim,
           ALIGN_LEFT);
}

static void ch_widgets(ui &u, example_app &app, rect body)
{
    const theme &th = u.th();
    (void)app;
    column col(body, 8.0f);
    (void)u.checkbox(col.next(24.0f), "Wrap long lines", g_wrap, "tr_wrap"_id);
    (void)u.checkbox(col.next(24.0f), "Show grid", g_grid_on, "tr_grid"_id);
    (void)u.slider_float(col.next(26.0f), "Zoom", g_zoom, 0.5f, 3.0f, "tr_zoom"_id, "%.2fx");
    (void)u.slider_float(col.next(26.0f), "Quality", g_quality, 0.0f, 100.0f, "tr_quality"_id,
                         "%.0f%%");
    u.progress_bar(col.next(16.0f), g_quality / 100.0f, th.accent, th.widget_bg);
    char line[128];
    std::snprintf(line, sizeof(line), "wrap %s | grid %s | zoom %.2f | quality %.0f%%",
                  g_wrap ? "on" : "off", g_grid_on ? "on" : "off", static_cast<double>(g_zoom),
                  static_cast<double>(g_quality));
    u.text(col.next(16.0f), line, th.text_dim, ALIGN_LEFT);
}

static void ch_vlist(ui &u, example_app &app, rect body)
{
    const theme &th = u.th();
    (void)app;
    column col(body, 8.0f);
    u.text(col.next(16.0f),
           "virtual_list submits only the visible slice: the callback runs for rows that can",
           th.text_dim, ALIGN_LEFT);
    u.text(col.next(14.0f), "paint, and the content height derives from count * row height.",
           th.text_dim, ALIGN_LEFT);
    col.space(4.0f);
    static i32 rendered = 0;
    rendered = 0;
    scroll_view sv = u.scroll(col.remaining(), "tr_vlist"_id);
    sv.virtual_list(2000, 24.0f,
                    [&](ui &uu, i32 index, rect row)
                    {
                        ++rendered;
                        uu.draw_rounded_rect(row, (index % 2) ? th.widget_bg : th.panel_bg, 4.0f);
                        char label[64];
                        std::snprintf(label, sizeof(label), "row %d", index);
                        uu.text(row.pad(8.0f, 0.0f), label, th.text, ALIGN_LEFT);
                    });
    char line[96];
    std::snprintf(line, sizeof(line), "submitted %d of 2000 rows this frame", rendered);
    u.text(rect::make(col.bounds_.x, col.bounds_.bottom() - 18.0f, col.bounds_.w, 18.0f), line,
           th.text_dim, ALIGN_LEFT);
}

static void ch_keyboard(ui &u, example_app &app, rect body)
{
    const theme &th = u.th();
    (void)app;
    column col(body, 8.0f);
    u.text(col.next(16.0f),
           "Tab walks every widget in submission order (Shift+Tab goes back); Enter,", th.text_dim,
           ALIGN_LEFT);
    u.text(col.next(14.0f), "Space and the arrow keys act on whatever owns the focus ring.",
           th.text_dim, ALIGN_LEFT);
    col.space(4.0f);
    static i32 clicks = 0;
    static bool on = true;
    static f32 vol = 45.0f;
    if (u.button(col.next(28.0f), "Click, Enter or Space", "tr_kb_go"_id)) ++clicks;
    (void)u.checkbox(col.next(24.0f), "Enabled", on, "tr_kb_on"_id);
    (void)u.slider_float(col.next(26.0f), "Volume", vol, 0.0f, 100.0f, "tr_kb_vol"_id, "%.0f%%");
    char line[128];
    std::snprintf(line, sizeof(line), "clicks %d | enabled %s | volume %.0f%%", clicks,
                  on ? "on" : "off", static_cast<double>(vol));
    u.text(col.next(16.0f), line, th.text_dim, ALIGN_LEFT);
}

static bool g_show_panel = true;
static rect g_panel_bounds = rect::make(320.0f, 150.0f, 260.0f, 150.0f);

static void ch_panels(ui &u, example_app &app, rect body)
{
    const theme &th = u.th();
    (void)app;
    row r(body.cut_top(74.0f), 8.0f);
    const rect a = r.next(190.0f);
    u.card(a);
    u.text(a.pad(10.0f, 0.0f), "u.card(rect)", th.text, ALIGN_LEFT);
    const rect b = r.next(210.0f);
    u.card(b, card_override{.bg = some(color{40, 80, 60, 220}),
                            .border = some(th.accent),
                            .radius = some(10.0f),
                            .border_thickness = some(2.0f)});
    u.text(b.pad(10.0f, 0.0f), "card_override", th.text, ALIGN_LEFT);
    if (u.button(r.next(140.0f), "Reopen panel", "tr_reopen"_id)) g_show_panel = true;

    u.text(rect::make(body.x, body.y + 82.0f, body.w, 18.0f),
           "the panel floats above the UI and blocks input underneath it", th.text_dim, ALIGN_LEFT);

    if (g_show_panel)
    {
        panel_scope p = u.panel("Tour panel", g_panel_bounds, PANEL_NONE, "tr_panel"_id);
        if (p.close_requested)
            g_show_panel = false;
        else
        {
            column pc(p.content(), 4.0f);
            u.text(pc.next(16.0f), "panel_scope content()", th.text, ALIGN_LEFT);
            u.text(pc.next(16.0f), "drag the titlebar; the dot closes", th.text_dim, ALIGN_LEFT);
        }
    }
}

static void ch_scroll(ui &u, example_app &app, rect body)
{
    const theme &th = u.th();
    (void)app;
    column col(body, 6.0f);
    {
        row r(col.next(26.0f), 6.0f);
        if (u.button(r.next(90.0f), "top", "tr_top"_id)) g_scroll_cmd = 1;
        if (u.button(r.next(90.0f), "bottom", "tr_bottom"_id)) g_scroll_cmd = 2;
        if (u.button(r.next(110.0f), "item 20", "tr_item"_id)) g_scroll_cmd = 3;
        u.text(r.remaining(), "wheel + draggable bar", th.text_dim, ALIGN_RIGHT);
    }
    {
        row r(col.remaining(), 8.0f);
        const rect area = r.next(240.0f);
        u.draw_rect(area, th.panel_bg);
        scroll_view sv = u.scroll(area.pad(4.0f), "tr_scroll"_id,
                                  scroll_options{4.0f, SCROLL_ALWAYS_RESERVE_BAR});
        if (g_scroll_cmd == 1)
            sv.scroll_to(0.0f);
        else if (g_scroll_cmd == 2)
            sv.scroll_to(1.0e9f);
        else if (g_scroll_cmd == 3)
        {
            sv.ensure_visible(
                rect::make(sv.content().x, sv.content().y + 19.0f * 26.0f, 1.0f, 22.0f));
            g_selected = 19;
        }
        column lc(sv.content(), 4.0f);
        for (i32 i = 0; i < 30; ++i)
        {
            const rect item = lc.next(22.0f);
            interaction in = u.interact(u.auto_id(), item);
            const bool sel = (i == g_selected);
            u.draw_rounded_rect(item,
                                in.held      ? th.widget_active
                                : in.hovered ? th.widget_hover
                                : sel        ? th.accent
                                             : th.widget_bg,
                                3.0f);
            char label[24];
            std::snprintf(label, sizeof(label), "item %02d", i + 1);
            u.text(item.pad(6.0f, 0.0f), label, sel ? th.bg : th.text, ALIGN_LEFT);
            if (in.clicked)
            {
                g_selected = i;
                sv.ensure_visible(item);
            }
        }
        sv.set_content_height(30.0f * 22.0f + 29.0f * 4.0f);

        const rect over = r.next(200.0f);
        u.draw_rect(over, th.panel_bg);
        scroll_view ov =
            u.scroll(over.pad(4.0f), "tr_overlay"_id, scroll_options{4.0f, SCROLL_OVERLAY});
        column oc(ov.content(), 5.0f);
        for (i32 i = 0; i < 12; ++i)
        {
            char label[64];
            std::snprintf(label, sizeof(label), "overlay row %d - the bar floats over content",
                          i + 1);
            u.text(oc.next(18.0f), label, th.text_dim, ALIGN_LEFT);
        }
        ov.set_content_height(12.0f * 18.0f + 11.0f * 5.0f);
    }
    g_scroll_cmd = 0;
}

static void ch_popups(ui &u, example_app &app, rect body)
{
    const theme &th = u.th();
    (void)app;
    column col(body, 8.0f);
    {
        row r(col.next(28.0f), 6.0f);
        const rect menu_btn = r.next(110.0f);
        if (u.button(menu_btn, "Menu", "tr_menu"_id, "primary"_id)) g_menu_open = true;
        if (u.button(r.next(130.0f), "Modal", "tr_modal"_id)) g_modal_open = true;
        u.text(r.remaining(), g_status, th.text_dim, ALIGN_RIGHT);

        if (g_menu_open)
        {
            const rect menu = rect::make(menu_btn.x, menu_btn.bottom() + 4.0f, 170.0f, 124.0f);
            popup_scope p = u.popup("tr_menu_popup"_id, menu,
                                    POPUP_CLOSE_ON_ESCAPE | POPUP_CLOSE_ON_CLICK_OUTSIDE);
            if (p.close_requested)
                g_menu_open = false;
            else
            {
                style_scope flat(u, button_override{.radius = some(0.0f)});
                u.draw_rounded_rect(menu, th.panel_bg, 6.0f);
                u.draw_rect(rect::make(menu.x, menu.y, menu.w, 2.0f), th.accent);
                column items(menu.pad(8.0f), 4.0f);
                if (u.button(items.next(26.0f), "Rename", "tr_mi1"_id))
                {
                    std::snprintf(g_status, sizeof(g_status), "rename picked");
                    g_menu_open = false;
                }
                if (u.button(items.next(26.0f), "Duplicate", "tr_mi2"_id))
                {
                    std::snprintf(g_status, sizeof(g_status), "duplicate picked");
                    g_menu_open = false;
                }
                if (u.button(items.next(26.0f), "Delete", "tr_mi3"_id, "danger"_id))
                {
                    std::snprintf(g_status, sizeof(g_status), "delete picked");
                    g_menu_open = false;
                }
            }
        }
    }
    u.text(col.next(16.0f), "Tab is trapped inside an open popup; Escape / click-outside close it",
           th.text_dim, ALIGN_LEFT);
    u.text(col.next(16.0f), "base UI under a modal is blocked (see pui_ex_popups)", th.text_dim,
           ALIGN_LEFT);

    if (g_modal_open)
    {
        const rect dlg = rect::make(200.0f, 120.0f, 320.0f, 130.0f);
        popup_scope p = u.popup("tr_modal_popup"_id, dlg, POPUP_MODAL | POPUP_CLOSE_ON_ESCAPE);
        if (p.close_requested)
            g_modal_open = false;
        else
        {
            u.draw_rounded_rect(dlg, th.panel_bg, 8.0f);
            column dc(dlg.pad(14.0f), 8.0f);
            u.text(dc.next(22.0f), "Modal dialog", th.text, ALIGN_LEFT);
            u.text(dc.next(18.0f), "POPUP_MODAL blocks the base UI.", th.text_dim, ALIGN_LEFT);
            row buttons(dc.next(28.0f), 6.0f);
            if (u.button(buttons.next(110.0f), "Cancel", "tr_cancel"_id)) g_modal_open = false;
            if (u.button(buttons.next(110.0f), "OK", "tr_ok"_id, "primary"_id))
            {
                std::snprintf(g_status, sizeof(g_status), "modal confirmed");
                g_modal_open = false;
            }
        }
    }
}

static dock_node g_dock_root;
static example_dock::dock_pool g_dock_pool;
static uiid g_dock_float = 0;
static rect g_dock_float_bounds = rect::make(360.0f, 170.0f, 250.0f, 160.0f);

static const char *dock_name(uiid p)
{
    if (p == "t_stats"_id) return "Stats";
    if (p == "t_log"_id) return "Log";
    if (p == "t_settings"_id) return "Settings";
    if (p == "t_help"_id) return "Help";
    return "Panel";
}

static void dock_content(ui &u, uiid panel, rect pc, const theme &th)
{
    rect inner = pc.pad(6.0f);
    if (panel == "t_stats"_id)
    {
        column c(inner, 4.0f);
        if (u.button(c.next(24.0f), "count", "tr_dcount"_id)) g_stat += 1;
        char line[40];
        std::snprintf(line, sizeof(line), "count %d", g_stat);
        u.text(c.next(16.0f), line, th.text_dim, ALIGN_LEFT);
    }
    else if (panel == "t_log"_id)
    {
        scroll_view sv = u.scroll(inner, "tr_dlog"_id);
        column c(sv.content(), 3.0f);
        for (i32 i = 0; i < 20; ++i)
        {
            char line[40];
            std::snprintf(line, sizeof(line), "log %02d", i + 1);
            u.text(c.next(15.0f), line, th.text_dim, ALIGN_LEFT);
        }
        sv.set_content_height(20.0f * 15.0f + 19.0f * 3.0f);
    }
    else if (panel == "t_settings"_id)
    {
        (void)u.checkbox(inner.cut_top(22.0f), "Option", g_opt, "tr_dopt"_id);
    }
    else
    {
        u.text(inner, "drag tabs: center tabs, edges split", th.text_dim, ALIGN_LEFT);
    }
}

static void ch_dock(ui &u, example_app &app, rect body)
{
    const theme &th = u.th();
    (void)app;
    static bool init = false;
    if (!init)
    {
        init = true;
        dock_node *a = g_dock_pool.alloc();
        dock_node *b = g_dock_pool.alloc();
        *a = dock_node{};
        a->kind = DOCK_TABS;
        a->panels[0] = "t_stats"_id;
        a->panel_names[0] = "Stats";
        a->panels[1] = "t_settings"_id;
        a->panel_names[1] = "Settings";
        a->panel_count = 2;
        *b = dock_node{};
        b->kind = DOCK_LEAF;
        b->panels[0] = "t_log"_id;
        b->panel_names[0] = "Log";
        b->panel_count = 1;
        g_dock_root = dock_node{};
        g_dock_root.kind = DOCK_SPLIT_H;
        g_dock_root.ratio = 0.5f;
        g_dock_root.a = a;
        g_dock_root.b = b;
    }

    column col(body, 6.0f);
    u.text(col.next(16.0f), "dock_space: drag a tab to split or tab; dropping outside undocks",
           th.text_dim, ALIGN_LEFT);
    const dock_action act =
        u.dock_space("tr_dock"_id, col.remaining(), g_dock_root,
                     function_ref<void(uiid, rect, bool)>([&](uiid panel, rect pc, bool)
                                                          { dock_content(u, panel, pc, th); }));
    example_dock::apply(g_dock_root, act, dock_name, g_dock_float, g_dock_pool);

    if (g_dock_float)
    {
        panel_scope p = u.panel(dock_name(g_dock_float), g_dock_float_bounds, PANEL_NONE,
                                g_dock_float, dock_name(g_dock_float));
        if (p.close_requested)
        {
            example_dock::add_center(*example_dock::first_leaf(g_dock_root), g_dock_float,
                                     dock_name);
            g_dock_float = 0;
        }
        else
        {
            column pc(p.content(), 4.0f);
            u.text(pc.next(16.0f), "undocked", th.text_dim, ALIGN_LEFT);
        }
    }
}

static void tour_extra_window(ui &u, example_app &app, i32 index)
{
    const theme &th = u.th();
    window &w = *example_window_for(app, SDL_GetWindowID(app.extras[index - 1].sdl));
    const rect client = rect::make(0.0f, 0.0f, w.area.w, w.area.h);
    u.draw_rect(client, th.panel_bg);
    rect chrome = client;
    (void)u.titlebar(w, chrome, "pui_tour - extra window");
    column col(chrome.pad(14.0f), 8.0f);
    if (u.button(col.next(28.0f), "Click me", "tr_w2_btn"_id, "primary"_id)) g_clicks += 1;
    char line[48];
    std::snprintf(line, sizeof(line), "shared clicks: %d", g_clicks);
    u.text(col.next(18.0f), line, th.text_dim, ALIGN_LEFT);
    u.text(col.next(16.0f), "close it and the tour keeps running", th.text_dim, ALIGN_LEFT);
}

static void ch_windows(ui &u, example_app &app, rect body)
{
    const theme &th = u.th();
    column col(body, 8.0f);
    u.text(col.next(18.0f), "one render_device, several windows and input queues", th.text,
           ALIGN_LEFT);
    if (u.button(col.next(28.0f), "Spawn a second window", "tr_spawn"_id, "primary"_id))
    {
        app.extra_frame = tour_extra_window;
        example_add_window(app, "pui_tour - second window", 380, 220);
    }
    char line[64];
    std::snprintf(line, sizeof(line), "shared clicks: %d   extra windows: %d", g_clicks,
                  app.extra_count);
    u.text(col.next(18.0f), line, th.text_dim, ALIGN_LEFT);
    u.text(col.next(16.0f), "closing an extra window removes it; the tour keeps running",
           th.text_dim, ALIGN_LEFT);
    u.text(col.next(16.0f), "needs backend_caps::SHARED_DEVICE (the SDL3 device reports it)",
           th.text_dim, ALIGN_LEFT);
}

static texture_handle g_tour_tex = nullptr;

static void ch_drawing(ui &u, example_app &app, rect body)
{
    const theme &th = u.th();
    if (!g_tour_tex)
    {
        std::vector<u8> px(static_cast<usize>(48) * 48 * 4);
        for (i32 y = 0; y < 48; ++y)
            for (i32 x = 0; x < 48; ++x)
            {
                const bool on = (((x / 6) + (y / 6)) & 1) != 0;
                u8 *p = px.data() + (static_cast<usize>(y) * 48 + x) * 4;
                p[0] = on ? 210 : 70;
                p[1] = on ? 170 : 90;
                p[2] = on ? 90 : 140;
                p[3] = 255;
            }
        g_tour_tex = app.device->create_texture(48, 48, px.data());
    }

    column col(body, 8.0f);
    {
        row r(col.next(30.0f), 6.0f);
        u.draw_rect(r.next(80.0f), th.widget_bg);
        u.draw_rounded_rect(r.next(80.0f), th.accent, 8.0f);
        const rect cross = r.next(80.0f);
        u.draw_line(cross.x, cross.center_y(), cross.right(), cross.center_y(), th.text_dim, 2.0f);
        u.draw_line(cross.center_x(), cross.y, cross.center_x(), cross.bottom(), th.text_dim, 2.0f);
    }
    {
        row r(col.next(84.0f), 8.0f);
        const rect tri = r.next(84.0f);
        const vec2 pts[3] = {{tri.center_x(), tri.y + 6.0f},
                             {tri.x + 8.0f, tri.bottom() - 8.0f},
                             {tri.right() - 8.0f, tri.bottom() - 8.0f}};
        u.draw_polygon(std::span<const vec2>(pts, 3), th.accent);

        const rect pie = r.next(84.0f);
        const vec2 pc{pie.center_x(), pie.center_y()};
        const f32 pr = min2(pie.w, pie.h) * 0.42f;
        u.draw_sector(pc, 0.0f, pr, 0.0f, 2.0f, th.accent);
        u.draw_sector(pc, 0.0f, pr, 2.0f, 4.0f, th.widget_hover);
        u.draw_sector(pc, 0.0f, pr, 4.0f, 6.2832f, th.widget_bg);

        const rect ring = r.next(84.0f);
        u.draw_arc(vec2{ring.center_x(), ring.center_y()}, 26.0f, 8.0f, 0.4f, 5.0f, th.accent);

        if (g_tour_tex)
        {
            const skin_image img{g_tour_tex,
                                 rect::make(0.0f, 0.0f, 1.0f, 1.0f),
                                 48.0f,
                                 48.0f,
                                 0.0f,
                                 0.0f,
                                 0.0f,
                                 0.0f,
                                 SKIN_CENTER_STRETCH};
            u.draw_image(img, r.next(84.0f));
        }
    }
    u.text(col.next(16.0f),
           "draw_polygon / draw_sector / draw_arc are antialiased; draw_image "
           "takes any texture you create",
           th.text_dim, ALIGN_LEFT);
}

static void ch_blur(ui &u, example_app &app, rect body)
{
    const theme &th = u.th();
    for (i32 i = 0; i < 9; ++i)
    {
        const u8 v = static_cast<u8>(30 + i * 16);
        u.draw_rect(rect::make(0.0f, static_cast<f32>(i) * 30.0f, body.w, 30.0f),
                    color{v, static_cast<u8>(60 + i * 12), static_cast<u8>(150 - i * 10), 255});
    }
    column col(rect::make(body.x + 8.0f, body.y + 8.0f, 220.0f, 200.0f), 6.0f);
    u.text(col.next(18.0f), "u.blur(rect, radius, corner, alpha)", th.text, ALIGN_LEFT);
    (void)u.slider_float(col.next(26.0f), "radius", g_blur_radius, 0.0f, 30.0f, "tr_radius"_id,
                         "%.0f");
    (void)u.slider_float(col.next(26.0f), "alpha", g_blur_alpha, 0.0f, 1.0f, "tr_blur_alpha"_id,
                         "%.2f");
    u.text(col.next(16.0f), "alpha 1 replaces the content (text below becomes unreadable)",
           th.text_dim, ALIGN_LEFT);

    const bool supported = has_cap(app.device->caps(), backend_caps::RENDER_TARGETS);
    u.text(col.next(16.0f),
           supported ? "RENDER_TARGETS: yes" : "RENDER_TARGETS: no (flat fallback)", th.text_dim,
           ALIGN_LEFT);

    static rect panel_bounds{};
    if (panel_bounds.w <= 0.0f)
        panel_bounds = rect::make(body.x + 240.0f, body.y + 20.0f, 260.0f, 150.0f);
    u.blur(panel_bounds, g_blur_radius, 10.0f, g_blur_alpha);
    panel_scope p = u.panel("Frosted", panel_bounds, PANEL_NO_CONTROLS, "tr_blur_panel"_id, nullptr,
                            panel_override{.bg = some(color{16, 20, 28, 120}),
                                           .titlebar_bg = some(color{16, 20, 28, 150})});
    column pc(p.content(), 4.0f);
    u.text(pc.next(16.0f), "the bands behind are smeared", th.text, ALIGN_LEFT);
    u.text(pc.next(16.0f), "the panel surface is translucent", th.text_dim, ALIGN_LEFT);
}

static void ch_animation(ui &u, example_app &app, rect body)
{
    const theme &th = u.th();
    (void)app;
    column col(body, 6.0f);
    {
        row r(col.next(26.0f), 6.0f);
        if (u.button(r.next(100.0f), "Toggle", "tr_play"_id, "primary"_id)) g_play = !g_play;
        if (u.button(r.next(100.0f), "Replay", "tr_replay"_id)) g_appear_key += 1;
        (void)u.checkbox(r.next(140.0f), "Reduced motion", g_reduced, "tr_reduced"_id);
        u.text(r.remaining(), u.animations_active() ? "animations active" : "idle", th.text_dim,
               ALIGN_RIGHT);
    }
    u.set_reduced_motion(g_reduced);

    auto lane = [&](const char *label, f32 h)
    {
        row r(col.next(h), 6.0f);
        u.text(r.next(70.0f), label, th.text_dim, ALIGN_LEFT);
        return r.next(r.remaining().w);
    };
    {
        const rect b = lane("tween", 34.0f);
        u.draw_rect(b, th.widget_bg);
        const f32 t = u.animate("tr_tween"_id, g_play ? 1.0f : 0.0f,
                                tween{.duration = 0.5f, .curve = easing::EASE_OUT_BACK});
        u.draw_rounded_rect(rect::make(b.x + 4.0f + (b.w - 64.0f) * t, b.y + 5.0f, 60.0f, 24.0f),
                            th.accent, 5.0f);
    }
    {
        const rect b = lane("spring", 34.0f);
        u.draw_rect(b, th.widget_bg);
        const f32 s = u.animate("tr_spring"_id, g_play ? 1.0f : 0.0f, spring{260.0f, 0.7f});
        u.draw_rounded_rect(
            rect::make(b.x + 4.0f, b.y + 5.0f, 36.0f + 150.0f * clampf(s, 0.0f, 1.2f), 24.0f),
            th.widget_hover, 5.0f);
    }
    {
        const rect b = lane("smooth", 34.0f);
        u.draw_rect(b, th.widget_bg);
        const f32 sm = u.smooth("tr_smooth"_id, g_play ? 1.0f : 0.0f, 0.25f);
        u.draw_rounded_rect(rect::make(b.x + 4.0f + (b.w - 64.0f) * sm, b.y + 5.0f, 60.0f, 24.0f),
                            th.text_dim, 5.0f);
    }
    {
        const rect b = lane("appear", 34.0f);
        u.draw_rect(b, th.widget_bg);
        const f32 a = u.appear(id_child("tr_appear"_id, static_cast<uiid>(g_appear_key)), 0.5f);
        color c = th.accent;
        c.a = static_cast<u8>(255.0f * clampf(a, 0.0f, 1.0f));
        u.draw_rounded_rect(rect::make(b.x + 4.0f, b.y + 5.0f + (24.0f - 24.0f * a) * 0.5f,
                                       30.0f + 90.0f * clampf(a, 0.0f, 1.0f), 24.0f * a + 1.0f),
                            c, 5.0f);
    }
    u.text(col.next(16.0f), "animate/animate_color/smooth/appear are keyed and collected after 5 s",
           th.text_dim, ALIGN_LEFT);
}

static void ch_theme(ui &u, example_app &app, rect body)
{
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
        t.panel.titlebar_bg = {236, 240, 246, 255};
    }
    t.accent = g_accent;
    t.accent_hover = {static_cast<u8>(g_accent.r > 225 ? 255 : g_accent.r + 30),
                      static_cast<u8>(g_accent.g > 225 ? 255 : g_accent.g + 30),
                      static_cast<u8>(g_accent.b > 235 ? 255 : g_accent.b + 20), 255};
    t.radius = g_radius;
    set_button_role(t, "primary"_id,
                    button_override{.bg = some(g_accent), .hover_bg = some(t.accent_hover)});
    set_button_role(t, "danger"_id,
                    button_override{.bg = some(color{190, 60, 60, 255}),
                                    .hover_bg = some(color{215, 80, 80, 255})});
    set_theme(app.ctx, t);

    const theme &th = u.th();
    column col(body, 8.0f);
    (void)u.checkbox(col.next(24.0f), "Light theme", g_light, "tr_light"_id);
    {
        row r(col.next(26.0f), 6.0f);
        u.text(r.next(60.0f), "accent", th.text, ALIGN_LEFT);
        static const color choices[4] = {
            {60, 180, 255, 255}, {120, 210, 130, 255}, {220, 160, 60, 255}, {200, 110, 210, 255}};
        for (i32 i = 0; i < 4; ++i)
        {
            const rect sw = r.next(34.0f);
            interaction in = u.interact(u.auto_id(), sw);
            u.draw_rounded_rect(sw, choices[i], 5.0f);
            if (in.hovered) u.draw_rounded_rect(sw.pad(3.0f), th.bg, 4.0f);
            if (in.clicked) g_accent = choices[i];
        }
    }
    (void)u.slider_float(col.next(26.0f), "radius", g_radius, 0.0f, 14.0f, "tr_radius_theme"_id,
                         "%.0f");
    {
        row r(col.next(30.0f), 6.0f);
        (void)u.button(r.next(110.0f), "Default", "tr_tb1"_id);
        (void)u.button(r.next(110.0f), "Primary", "tr_tb2"_id, "primary"_id);
        (void)u.button(r.next(110.0f), "Danger", "tr_tb3"_id, "danger"_id);
    }
    const rect card = col.next(58.0f);
    u.card(card);
    u.text(card.pad(10.0f, 0.0f), "card group colors come from the theme", th.text, ALIGN_LEFT);
}

static bool toggle_switch(ui &u, rect r, bool &value, uiid id)
{
    const theme &th = u.th();
    interaction in = u.interact(id, r);
    if (in.hovered || in.held) u.set_cursor(CURSOR_HAND);
    const f32 th_h = 22.0f;
    const rect track = rect::make(r.x, r.y + (r.h - th_h) * 0.5f, 42.0f, th_h);
    const f32 t = u.animate(id_child(id, 1), value ? 1.0f : 0.0f, tween{.duration = 0.15f});
    u.draw_rounded_rect(track, value ? th.accent : th.widget_bg, th_h * 0.5f);
    const f32 d = th_h - 6.0f;
    u.draw_rounded_rect(rect::make(track.x + 3.0f + (track.w - d - 6.0f) * t, track.y + 3.0f, d, d),
                        th.text, d * 0.5f);
    if (in.clicked)
    {
        value = !value;
        return true;
    }
    return false;
}

static bool segmented(ui &u, rect r, const char *const *labels, i32 count, i32 &value, uiid id)
{
    const theme &th = u.th();
    bool changed = false;
    const f32 w = (count > 0) ? (r.w - static_cast<f32>(count - 1) * 2.0f) / count : r.w;
    row seg(r, 2.0f);
    for (i32 i = 0; i < count; ++i)
    {
        const rect item = seg.next(w);
        interaction in = u.interact(id_child(id, static_cast<uiid>(i)), item);
        if (in.hovered) u.set_cursor(CURSOR_HAND);
        u.draw_rect(item, i == value   ? th.accent
                          : in.held    ? th.widget_active
                          : in.hovered ? th.widget_hover
                                       : th.widget_bg);
        u.text(item, labels[i], i == value ? th.bg : th.text, ALIGN_CENTER);
        if (in.clicked && i != value)
        {
            value = i;
            changed = true;
        }
    }
    return changed;
}

static void ch_custom(ui &u, example_app &app, rect body)
{
    const theme &th = u.th();
    (void)app;
    column col(body, 8.0f);
    {
        row r(col.next(26.0f), 8.0f);
        (void)toggle_switch(u, r.next(160.0f), g_switch, "tr_switch"_id);
        char line[48];
        std::snprintf(line, sizeof(line), "switch: %s", g_switch ? "on" : "off");
        u.text(r.remaining(), line, th.text_dim, ALIGN_RIGHT);
    }
    static const char *labels[3] = {"One", "Two", "Three"};
    (void)segmented(u, col.next(28.0f), labels, 3, g_seg, "tr_seg"_id);

    // Star rating: draw_polygon + per-star ids.
    {
        const rect r = col.next(32.0f);
        row seg(r, 4.0f);
        const f32 w = (r.w - 4.0f * 4.0f) / 5.0f;
        for (i32 i = 0; i < 5; ++i)
        {
            const rect cell = seg.next(w);
            interaction in = u.interact(id_child("tr_stars"_id, static_cast<uiid>(i)), cell);
            if (in.hovered) u.set_cursor(CURSOR_HAND);
            const f32 s = min2(cell.w, cell.h) * 0.44f;
            vec2 pts[10];
            for (i32 k = 0; k < 10; ++k)
            {
                const f32 a = -1.5708f + static_cast<f32>(k) * 0.62832f;
                const f32 rad = (k % 2 == 0) ? s : s * 0.45f;
                pts[k] = {cell.center_x() + static_cast<f32>(std::cos(a)) * rad,
                          cell.center_y() + static_cast<f32>(std::sin(a)) * rad};
            }
            u.draw_polygon(std::span<const vec2>(pts, 10),
                           i < g_rating ? color{230, 180, 60, 255} : th.widget_bg);
            if (in.clicked) g_rating = i + 1;
        }
    }

    const rect probe = col.next(46.0f);
    interaction in = u.interact("tr_probe"_id, probe);
    u.draw_rect(probe, in.held ? th.accent : in.hovered ? th.widget_hover : th.widget_bg);
    char line[176];
    std::snprintf(line, sizeof(line),
                  "hovered %d  pressed %d  activated %d  held %d  clicked %d  focused %d  "
                  "hot 0x%llX",
                  in.hovered ? 1 : 0, in.pressed ? 1 : 0, in.activated ? 1 : 0, in.held ? 1 : 0,
                  in.clicked ? 1 : 0, in.focused ? 1 : 0,
                  static_cast<unsigned long long>(u.hot_id()));
    u.text(probe.pad(8.0f, 0.0f), line, in.held ? th.bg : th.text_dim, ALIGN_LEFT);
}

static void ch_menus(ui &u, example_app &app, rect body)
{
    const theme &th = u.th();
    (void)app;
    column col(body, 8.0f);

    // combo: one row; the open dropdown appears below the header.
    static const char *kinds[4] = {"Sprite", "Emitter", "Light", "Camera"};
    {
        row r(col.next(30.0f), 10.0f);
        example_caption(u, r.next(70.0f), "combo:");
        if (u.combo(r.next(160.0f), "", kinds, 4, g_kind, "tr_kind"_id, 150.0f))
            std::snprintf(g_status, sizeof(g_status), "picked %s", kinds[g_kind]);
        u.text(r.remaining(), "click, arrows + Enter, or click away", th.text_dim, ALIGN_LEFT);
    }

    // tooltip + context menu row.
    {
        row r(col.next(30.0f), 8.0f);
        const rect tip_btn = r.next(90.0f);
        if (u.button(tip_btn, "Hover me", "tr_tipbtn"_id))
            std::snprintf(g_status, sizeof(g_status), "save clicked");
        u.tooltip(tip_btn, "tr_tipbtn"_id, "tooltips wait, then flow");
        const rect rightable = r.next(120.0f);
        u.draw_rounded_rect(rightable, th.widget_bg, 6.0f);
        u.text(rightable, "right-press", th.text_dim, ALIGN_CENTER);
        static const char *actions[3] = {"Fit", "Duplicate", "Delete"};
        if (const i32 picked = u.context_menu("tr_ctx"_id, rightable, actions, 3); picked >= 0)
            std::snprintf(g_status, sizeof(g_status), "picked %s", actions[picked]);
    }
    u.text(col.next(18.0f), g_status, th.text_dim, ALIGN_LEFT);
}

static void ch_headless(ui &u, example_app &app, rect body)
{
    const theme &th = u.th();
    (void)app;
    column col(body, 8.0f);
    u.text(col.next(20.0f), "These two run without SDL:", th.text, ALIGN_LEFT);
    u.text(col.next(18.0f), "pui_ex_headless - null_device, synthetic input, violation capture,",
           th.text_dim, ALIGN_LEFT);
    u.text(col.next(18.0f), "                  backend_caps checks, no display at all", th.text_dim,
           ALIGN_LEFT);
    u.text(col.next(18.0f), "pui_ex_renderer - a CPU render_device + render_surface that",
           th.text_dim, ALIGN_LEFT);
    u.text(col.next(18.0f), "                  rasterizes every triangle and writes a PPM",
           th.text_dim, ALIGN_LEFT);
    u.text(col.next(20.0f), "docs/porting_a_backend.md has the full contract.", th.text_dim,
           ALIGN_LEFT);
    const rect bar = col.next(20.0f);
    u.progress_bar(bar, 1.0f, th.accent, th.widget_bg);
    u.text(bar, "every example runs headless in CI via --selftest", th.bg, ALIGN_CENTER);
}

static const tour_chapter CHAPTERS[] = {
    {"Start", "Bootstrap", "one-call app setup, the raw frame loop, native chrome",
     "pui_ex_bootstrap", ch_bootstrap},
    {"Layout", "Rect algebra", "cuts, slices, align/fit + tracks + auto-fit grid",
     "pui_ex_rect / pui_ex_tracks / pui_ex_grid", ch_layout},
    {"Layout", "Cursors", "column/row slicing, spacing, clamping", "pui_ex_cursors", ch_cursors},
    {"Layout", "Regions & IDs", "region scoping, local/auto_id/id_child", "pui_ex_ids", ch_ids},
    {"Text", "Text & fonts", "sizes, bold/oblique, ellipsis, wrap, measure, UTF-8", "pui_ex_text",
     ch_text},
    {"Widgets", "Buttons", "roles, overrides, style_scope", "pui_ex_buttons", ch_buttons},
    {"Widgets", "Inputs", "text_field / number_field, focus, Enter", "pui_ex_inputs", ch_inputs},
    {"Widgets", "Small widgets", "checkbox, slider, progress bar", "pui_ex_widgets", ch_widgets},
    {"Widgets", "Keyboard", "Tab rings, Enter/Space activation, arrow keys", "pui_ex_keyboard",
     ch_keyboard},
    {"Widgets", "Cards & panels", "card overrides + floating panels", "pui_ex_panels", ch_panels},
    {"Widgets", "Scrolling", "gutter/overlay bars, ensure_visible, scroll_to", "pui_ex_scroll",
     ch_scroll},
    {"Widgets", "Virtual list", "visible-slice submission, derived content height", "pui_ex_vlist",
     ch_vlist},
    {"Layers", "Popups & menus", "flags, close requests, modal, focus trap", "pui_ex_popups",
     ch_popups},
    {"Layers", "Combo & context menus", "combo, delayed tooltips, right-press menu", "pui_ex_combo",
     ch_menus},
    {"Layers", "Docking", "tabs, splits, drag-to-dock, undock", "pui_ex_dock", ch_dock},
    {"Layers", "Multi-window", "one device, several windows, shared model", "pui_ex_windows",
     ch_windows},
    {"Visuals", "Shapes & images", "primitives, polygons/sectors/arcs, textures", "pui_ex_drawing",
     ch_drawing},
    {"Visuals", "Blur", "frosted backdrops + capability fallback", "pui_ex_blur", ch_blur},
    {"Visuals", "Animation", "tween, spring, smooth, appear, reduced motion", "pui_ex_animation",
     ch_animation},
    {"Visuals", "Theme", "live colors, metrics, roles, fonts", "pui_ex_theme", ch_theme},
    {"Custom", "Custom widgets", "interact + primitives, interaction fields", "pui_ex_custom",
     ch_custom},
    {"Custom", "Headless & backends", "null_device, violations, custom render_device",
     "pui_ex_headless / pui_ex_renderer", ch_headless},
};
inline constexpr i32 CHAPTER_COUNT = static_cast<i32>(sizeof(CHAPTERS) / sizeof(CHAPTERS[0]));

static void tour_frame(ui &u, example_app &app)
{
    const theme &th = u.th();
    rect client = rect::make(0.0f, 0.0f, static_cast<f32>(app.width), static_cast<f32>(app.height));
    u.draw_rect(client, th.bg);

    // Window chrome: the custom titlebar replaces the OS decoration. The bar's
    // title IS the page title; the chapter line rides in the drag strip.
    window &w = *u.ctx->current_window;
    rect chrome = client;
    (void)u.titlebar(w, chrome, "PufferUI tour");

    // top strip (chapter position + hints under the titlebar; the title lives
    // in the titlebar itself)
    const rect top = chrome.cut_top(22.0f);
    u.draw_line(top.left(), top.bottom(), top.right(), top.bottom(), th.border, 1.0f);
    {
        char line[96];
        std::snprintf(line, sizeof(line), "chapter %d / %d   -   %s", g_chapter + 1, CHAPTER_COUNT,
                      CHAPTERS[g_chapter].name);
        u.text(top.pad(12.0f, 0.0f), line, th.text_dim, ALIGN_LEFT);
        u.text(top.pad(12.0f, 0.0f),
               u.animations_active() ? "anim: on   Up/Down: switch" : "Up/Down: switch",
               th.text_dim, ALIGN_RIGHT);
    }

    // keyboard chapter switching
    if (u.key_pressed(key::DOWN)) g_chapter = (g_chapter + 1) % CHAPTER_COUNT;
    if (u.key_pressed(key::UP)) g_chapter = (g_chapter + CHAPTER_COUNT - 1) % CHAPTER_COUNT;

    // sidebar: a scrollable chapter list
    rect sidebar = chrome.cut_left(g_sidebar_w);
    sidebar = sidebar.pad(8.0f);
    u.draw_rect(sidebar, th.panel_bg);
    {
        scroll_view sv = u.scroll(sidebar.pad(4.0f), "tour_nav"_id);
        column nav(sv.content(), 3.0f);
        f32 used = 0.0f;
        const char *last_group = nullptr;
        for (i32 i = 0; i < CHAPTER_COUNT; ++i)
        {
            const tour_chapter &ch = CHAPTERS[i];
            if (!last_group || std::strcmp(last_group, ch.group) != 0)
            {
                last_group = ch.group;
                nav.space(6.0f);
                u.text(nav.next(18.0f), ch.group, th.text_dim, ALIGN_LEFT);
                used += 6.0f + 18.0f + 3.0f;
            }
            const rect item = nav.next(24.0f);
            used += 24.0f + 3.0f;
            interaction in = u.interact(u.local(ch.name), item);
            if (in.hovered || in.held) u.set_cursor(CURSOR_HAND);
            const bool active = (i == g_chapter);
            u.draw_rounded_rect(item,
                                active       ? th.accent
                                : in.hovered ? th.widget_hover
                                             : th.widget_bg,
                                4.0f);
            u.text(item.pad(8.0f, 0.0f), ch.name, active ? th.bg : th.text, ALIGN_LEFT);
            if (in.clicked) g_chapter = i;
        }
        sv.set_content_height(used + 8.0f);
    }

    // content
    rect content = chrome.pad(14.0f);
    const tour_chapter &ch = CHAPTERS[g_chapter];
    rect head = content.cut_top(66.0f);
    {
        text_scope title = u.text_style(20.0f, app.font_bold);
        u.text(head.cut_top(26.0f), ch.name, th.text, ALIGN_LEFT);
    }
    u.text(head.cut_top(20.0f), ch.blurb, th.text_dim, ALIGN_LEFT);
    {
        char line[128];
        std::snprintf(line, sizeof(line), "standalone: %s", ch.target);
        u.text(head.cut_top(18.0f), line, th.text_dim, ALIGN_LEFT);
    }
    (void)content.cut_top(4.0f);

    // Each chapter's widgets live in their own region (ids scoped per chapter).
    region reg(u, u.local(ch.name), content);
    ch.draw(u, app, reg.content());
}

int main(int argc, char **argv)
{
    for (i32 i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], "--chapter") == 0)
        {
            const i32 n = std::atoi(argv[i + 1]);
            if (n >= 1 && n <= CHAPTER_COUNT) g_chapter = n - 1;
        }
    return example_run("tour", 960, 620, argc, argv, tour_frame);
}