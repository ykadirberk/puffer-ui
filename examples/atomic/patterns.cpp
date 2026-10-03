// patterns — a component gallery inspired by modern UI libraries and guidelines.
//
// Recreated (with PufferUI's own primitives) from patterns seen in:
//   - coss.com/ui            component set + naming (toast, kbd, meter, drawer…)
//   - ui-skills.com          playbook (scale on press, 44px targets, nested radii)
//   - emilkowal.ski/ui       motion rules (purpose, <300ms, tooltips delay then
//                            instant, never animate keyboard-initiated actions)
//   - reui.io / designsystemchecklist.com  component + system checklists
//
//   pui_ex_patterns
#include "../example_common.h"

#include <cmath>
#include <vector>

// ---------------------------------------------------------------- state
static bool g_drawer_open = false;
static bool g_palette_open = false;
static i32 g_palette_sel = 0;
static std::string g_palette_query;
static bool g_load = false;
static f64 g_load_start = -1.0;
static bool g_load_done = false;
static i32 g_accordion_open = 0;
static bool g_switch_on = true;
static bool g_checks[2] = {true, false};
static i32 g_radio = 1;
static i32 g_seg = 0;
static i32 g_toggles = 0b101;
static i32 g_table_sort = 0; // 0 name asc, 1 name desc, 2 size
static i32 g_page = 2;
static std::vector<std::string> g_tags;
static char g_copied_state = 0;
static f64 g_copied_at = -1.0;
static i32 g_scaled_presses = 0;

struct toast_entry
{
    i32 id = 0;
    const char *text = "";
    bool action = false;
    bool done = false; // this toast's action was used
    bool leaving = false;
    f32 slide = 0.0f; // 0 = off-screen below, 1 = fully in
    f64 born = 0.0;
};
static toast_entry g_toasts[4];
static i32 g_toast_count = 0;
static i32 g_toast_next = 1;

// Tooltips: the first one waits, then the others open instantly while you move
// between them (emilkowal.ski/ui).
static uiid g_tip_last = 0;
static f64 g_tip_shown_at = -1.0;
static f64 g_tip_wait_start = -1.0;
static bool g_copy_request = false;

static void toast_push(const char *text, bool action)
{
    if (g_toast_count >= 4) return;
    toast_entry &t = g_toasts[g_toast_count++];
    t = toast_entry{};
    t.id = g_toast_next++;
    t.text = text;
    t.action = action;
    t.born = -1.0; // stamped on the first update
    t.slide = 0.0f;
}

static void toast_update(ui &u, f32 dt)
{
    for (i32 i = 0; i < g_toast_count;)
    {
        toast_entry &t = g_toasts[i];
        const f64 now = u.ctx->now;
        if (t.born < 0.0) t.born = now;
        const bool expired = (now - t.born) > 4.0;
        if (expired || t.leaving) t.leaving = true;

        const f32 target = t.leaving ? 0.0f : 1.0f;
        t.slide = u.animate(id_child("toast"_id, static_cast<uiid>(t.id)), target,
                            spring{.stiffness = 420.0f, .damping_ratio = 0.85f});
        if (t.leaving && t.slide < 0.02f)
        {
            for (i32 j = i; j < g_toast_count - 1; ++j) g_toasts[j] = g_toasts[j + 1];
            g_toast_count -= 1;
            continue;
        }
        (void)dt;
        ++i;
    }
}

static void toast_draw(ui &u, example_app &app)
{
    const theme &th = u.th();
    const f32 w = 280.0f;
    for (i32 i = 0; i < g_toast_count; ++i)
    {
        const toast_entry &t = g_toasts[i];
        const f32 y =
            static_cast<f32>(app.height) - 24.0f - (i + 1) * 54.0f + (1.0f - t.slide) * 60.0f;
        const rect r{static_cast<f32>(app.width) - w - 18.0f, y, w, 46.0f};
        u.draw_rounded_rect(rect::make(r.x + 2.0f, r.y + 3.0f, r.w, r.h), color{0, 0, 0, 80}, 8.0f);
        u.draw_rounded_rect(r, color{32, 38, 48, 245}, 8.0f);
        u.draw_rounded_rect(rect::make(r.x, r.y + 8.0f, 3.0f, r.h - 16.0f), th.accent, 2.0f);
        u.text(r.pad(14.0f, 0.0f, 90.0f, 0.0f), t.text, th.text, ALIGN_LEFT);
        if (t.action)
        {
            const rect a{r.right() - 78.0f, r.y + 10.0f, 64.0f, 26.0f};
            if (u.button(a, t.done ? "Done" : "Undo",
                         id_child("toast_act"_id, static_cast<uiid>(t.id))))
            {
                g_toasts[i].done = true;
                g_toasts[i].leaving = true;
            }
        }
    }
}

// ---------------------------------------------------------------- tooltip
static void tooltip_for(ui &u, rect target, uiid id, const char *text)
{
    const theme &th = u.th();
    if (u.hot_id() != id)
    {
        if (g_tip_last == id) g_tip_last = 0;
        return;
    }
    // First tooltip waits; while you move between them they open instantly.
    if (g_tip_last != id)
    {
        const f64 now = u.ctx->now;
        const bool another_recent = g_tip_shown_at >= 0.0 && (now - g_tip_shown_at) < 1.2;
        if (!another_recent && g_tip_shown_at >= 0.0 && (now - g_tip_shown_at) < 0.6) return;
        if (g_tip_shown_at < 0.0 && g_tip_wait_start < 0.0) g_tip_wait_start = now;
        if (!another_recent && g_tip_wait_start >= 0.0 && (now - g_tip_wait_start) < 0.55) return;
        g_tip_last = id;
        g_tip_shown_at = now;
        g_tip_wait_start = -1.0;
    }
    const f32 tw = u.text_width(text) + 16.0f;
    const rect tip{target.center_x() - tw * 0.5f, target.bottom() + 9.0f, tw, 24.0f};
    u.draw_rounded_rect(tip, color{18, 22, 28, 245}, 6.0f);
    u.text(tip, text, th.text, ALIGN_CENTER);
}

static void card_tooltip(ui &u, rect r, const theme &th)
{
    column c(r, 6.0f);
    row b(c.next(30.0f), 8.0f);
    static const char *tips[3] = {"Duplicate the layer", "Hide the layer", "Delete the layer"};
    static const char *labels[3] = {"Dup", "Hide", "Del"};
    for (i32 i = 0; i < 3; ++i)
    {
        const rect item = b.next(66.0f);
        const uiid id = id_child("tip_btn"_id, static_cast<uiid>(i));
        (void)u.button(item, labels[i], id);
        tooltip_for(u, item, id, tips[i]);
    }
    u.text(c.next(14.0f), "hover one, then slide across the others", th.text_dim, ALIGN_LEFT);
}

// ---------------------------------------------------------------- palette
static const char *const PALETTE[] = {"New file",  "Open project", "Save as",      "Toggle theme",
                                      "Run tests", "Git: commit",  "Search files", "Reload window"};
static constexpr i32 PALETTE_COUNT = static_cast<i32>(sizeof(PALETTE) / sizeof(PALETTE[0]));

static bool palette_match(const char *item, const std::string &query)
{
    if (query.empty()) return true;
    for (usize i = 0; i + query.size() <= std::string_view(item).size(); ++i)
    {
        bool ok = true;
        for (usize j = 0; j < query.size() && ok; ++j)
        {
            char a = item[i + j], b = query[j];
            if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
            ok = (a == b);
        }
        if (ok) return true;
    }
    return false;
}

static void palette_draw(ui &u, example_app &app)
{
    const theme &th = u.th();
    // Command palettes are keyboard-initiated: they appear instantly, with no
    // animation (emilkowal.ski/ui: never animate keyboard actions).
    const rect panel{app.width * 0.5f - 210.0f, 70.0f, 420.0f, 300.0f};
    popup_scope p = u.popup("pattern_palette"_id, panel,
                            POPUP_MODAL | POPUP_CLOSE_ON_ESCAPE | POPUP_CLOSE_ON_CLICK_OUTSIDE);
    if (p.close_requested)
    {
        g_palette_open = false;
        g_palette_query.clear();
        return;
    }

    u.draw_rounded_rect(rect::make(panel.x + 3.0f, panel.y + 5.0f, panel.w, panel.h),
                        color{0, 0, 0, 120}, 14.0f);
    u.draw_rounded_rect(panel, color{24, 28, 35, 252}, 12.0f);
    u.draw_rounded_rect(panel.pad(1.0f), color{24, 28, 35, 0}, 0.0f);
    const rect query = rect::make(panel.x + 14.0f, panel.y + 14.0f, panel.w - 28.0f, 32.0f);
    (void)u.text_field(query, g_palette_query, "palette_query"_id);

    std::vector<i32> hits;
    for (i32 i = 0; i < PALETTE_COUNT; ++i)
        if (palette_match(PALETTE[i], g_palette_query)) hits.push_back(i);
    if (g_palette_sel >= static_cast<i32>(hits.size())) g_palette_sel = 0;

    if (u.key_pressed(key::DOWN)) g_palette_sel += 1;
    if (u.key_pressed(key::UP)) g_palette_sel -= 1;
    if (!hits.empty())
    {
        g_palette_sel =
            (g_palette_sel % static_cast<i32>(hits.size()) + static_cast<i32>(hits.size())) %
            static_cast<i32>(hits.size());
    }
    bool accept = u.key_pressed(key::ENTER) && !hits.empty();

    column list(
        rect::make(panel.x + 12.0f, query.bottom() + 8.0f, panel.w - 24.0f, panel.h - 62.0f), 4.0f);
    for (i32 k = 0; k < static_cast<i32>(hits.size()) && k < 6; ++k)
    {
        const rect item = list.next(30.0f);
        const bool sel = (k == g_palette_sel);
        interaction in = u.interact(id_child("palette_item"_id, static_cast<uiid>(k)), item);
        if (in.hovered)
        {
            g_palette_sel = k;
            u.set_cursor(CURSOR_HAND);
        }
        if (sel) u.draw_rounded_rect(item, th.accent, 6.0f);
        u.text(item.pad(10.0f, 0.0f), PALETTE[hits[static_cast<usize>(k)]], sel ? th.bg : th.text,
               ALIGN_LEFT);
        if (in.clicked) accept = true;
    }
    if (accept)
    {
        g_palette_open = false;
        g_palette_query.clear();
        toast_push("Command executed", false);
    }
}

// ---------------------------------------------------------------- drawer
static void drawer_draw(ui &u, example_app &app)
{
    const theme &th = u.th();
    const rect cover =
        rect::make(0.0f, 0.0f, static_cast<f32>(app.width), static_cast<f32>(app.height));
    popup_scope p = u.popup("pattern_drawer"_id, cover, POPUP_MODAL | POPUP_CLOSE_ON_ESCAPE);
    if (p.close_requested)
    {
        g_drawer_open = false;
        return;
    }
    // Scrim: clicking it closes (the popup covers the window, so we test it here).
    interaction scrim = u.interact("drawer_scrim"_id, cover);
    u.draw_rect(cover, color{0, 0, 0, 120});
    if (scrim.clicked) g_drawer_open = false;

    // Slides in from the right and leaves the same way (spatial consistency).
    const f32 t = u.animate("drawer_t"_id, g_drawer_open ? 1.0f : 0.0f,
                            spring{.stiffness = 380.0f, .damping_ratio = 0.9f});
    const f32 w = 300.0f;
    const rect sheet{cover.w - w - (1.0f - t) * (w + 30.0f), 0.0f, w, cover.h};
    u.draw_rect(sheet, color{26, 30, 38, 252});
    u.draw_rect(rect::make(sheet.x, sheet.y, 1.0f, sheet.h), th.border);
    column c(sheet.pad(16.0f), 8.0f);
    u.text(c.next(24.0f), "Drawer / sheet", th.text, ALIGN_LEFT);
    u.text(c.next(18.0f), "Slides in from the right and leaves the same way.", th.text_dim,
           ALIGN_LEFT);
    u.text(c.next(18.0f), "Esc, the scrim, or the button closes it.", th.text_dim, ALIGN_LEFT);
    if (u.button(c.next(30.0f), "Close", "drawer_close"_id, "primary"_id)) g_drawer_open = false;
}

// ---------------------------------------------------------------- card: choices
static void card_choices(ui &u, rect r, const theme &th)
{
    column c(r, 8.0f);
    {
        static const char *labels[3] = {"Day", "Week", "Month"};
        const rect seg = c.next(28.0f);
        const f32 w = (seg.w - 4.0f) / 3.0f;
        row row_r(seg, 2.0f);
        for (i32 i = 0; i < 3; ++i)
        {
            const rect item = row_r.next(w);
            interaction in = u.interact(id_child("seg"_id, static_cast<uiid>(i)), item);
            u.draw_rect(item, i == g_seg ? th.accent : in.hovered ? th.widget_hover : th.widget_bg);
            u.text(item, labels[i], i == g_seg ? th.bg : th.text, ALIGN_CENTER);
            if (in.clicked) g_seg = i;
        }
    }
    row controls(c.next(28.0f), 16.0f);
    (void)u.checkbox(controls.next(96.0f), "Email", g_checks[0], "chk_mail"_id);
    (void)u.checkbox(controls.next(110.0f), "Mentions", g_checks[1], "chk_mention"_id);
    {
        const rect sw = controls.next(46.0f);
        interaction in = u.interact("pat_switch"_id, sw);
        if (in.hovered) u.set_cursor(CURSOR_HAND);
        const f32 t =
            u.animate("pat_switch_t"_id, g_switch_on ? 1.0f : 0.0f, tween{.duration = 0.14f});
        const rect track{sw.x, sw.y + 4.0f, 42.0f, 20.0f};
        u.draw_rounded_rect(track, g_switch_on ? th.accent : th.widget_bg, 10.0f);
        u.draw_rounded_rect(rect::make(track.x + 3.0f + 22.0f * t, track.y + 3.0f, 14.0f, 14.0f),
                            th.text, 7.0f);
        if (in.clicked) g_switch_on = !g_switch_on;
    }
    {
        row radios(c.next(24.0f), 14.0f);
        static const char *names[3] = {"Low", "Medium", "High"};
        for (i32 i = 0; i < 3; ++i)
        {
            const rect item = radios.next(84.0f);
            interaction in = u.interact(id_child("radio"_id, static_cast<uiid>(i)), item);
            if (in.hovered) u.set_cursor(CURSOR_HAND);
            const rect dot{item.x + 2.0f, item.y + 7.0f, 10.0f, 10.0f};
            u.draw_rounded_rect(dot, th.widget_bg, 5.0f);
            if (g_radio == i) u.draw_rounded_rect(dot.pad(3.0f), th.accent, 3.0f);
            u.text(rect::make(dot.right() + 6.0f, item.y, 60.0f, item.h), names[i], th.text,
                   ALIGN_LEFT);
            if (in.clicked) g_radio = i;
        }
    }
}

// ---------------------------------------------------------------- card: form
static void card_form(ui &u, rect r, const theme &th)
{
    column c(r, 6.0f);
    static std::string email = "ada@";
    static std::string username = "ada";
    {
        const rect g = c.next(28.0f);
        if (g.w > 100.0f)
        {
            u.draw_rounded_rect(g, th.widget_bg, 5.0f);
            const rect prefix{g.x + 8.0f, g.y, 40.0f, g.h};
            u.text(prefix, "@", th.text_dim, ALIGN_LEFT);
            u.text_field(rect::make(prefix.right(), g.y, max2(0.0f, g.w - 48.0f), g.h), username,
                         "fld_user"_id);
        }
    }
    const bool invalid = email.find('@') == std::string::npos || email.back() == '@';
    {
        const rect f = c.next(28.0f);
        if (invalid && !email.empty())
        {
            // invalid state as an opaque ring (the field fill is opaque too)
            u.draw_rounded_rect(f, color{220, 70, 70, 230}, th.radius);
            u.draw_rounded_rect(f.pad(1.0f), th.widget_bg, max2(0.0f, th.radius - 1.0f));
        }
        u.text_field(f, email, "fld_email"_id);
    }
    u.text(c.next(14.0f),
           invalid && !email.empty() ? "Enter a complete email address" : "Looks good",
           invalid && !email.empty() ? color{230, 110, 110, 255} : th.text_dim, ALIGN_LEFT);
}

// ---------------------------------------------------------------- card: feedback
static void card_feedback(ui &u, rect r, const theme &th)
{
    column c(r, 6.0f);
    {
        row b(c.next(28.0f), 6.0f);
        if (u.button(b.next(96.0f), "Toast", "tst_push"_id, "primary"_id))
            toast_push("Saved to workspace", false);
        if (u.button(b.next(110.0f), "With action", "tst_push2"_id))
            toast_push("Item deleted", true);
    }
    {
        const rect alert = c.next(30.0f);
        if (alert.h > 0.0f)
        {
            u.draw_rounded_rect(alert, color{40, 60, 90, 200}, 6.0f);
            u.draw_rect(rect::make(alert.x, alert.y + 6.0f, 3.0f, max2(0.0f, alert.h - 12.0f)),
                        th.accent);
            u.text(alert.pad(12.0f, 0.0f), "Heads up: this build is a preview.", th.text,
                   ALIGN_LEFT);
        }
    }
    {
        row b(c.next(26.0f), 6.0f);
        static const char *tags[3] = {"design", "ui", "release"};
        for (i32 i = 0; i < 3; ++i)
        {
            const rect tag = b.next(84.0f);
            if (tag.w <= 0.0f) break;
            u.draw_rounded_rect(tag, th.border, 13.0f);
            u.draw_rounded_rect(tag.pad(1.0f), th.widget_bg, 12.0f);
            u.text(rect::make(tag.x + 11.0f, tag.y, max2(0.0f, tag.w - 30.0f), tag.h), tags[i],
                   th.text, ALIGN_LEFT);
            if (u.button(rect::make(tag.right() - 20.0f, tag.y + 3.0f, 18.0f, 18.0f), "x",
                         id_child("tag_x"_id, static_cast<uiid>(i))))
                toast_push("Tag removed", false);
        }
        const rect k1 = b.next(28.0f);
        u.draw_rounded_rect(k1, th.widget_bg, 5.0f);
        u.text(k1, "K", th.text_dim, ALIGN_CENTER);
        const rect k2 = b.next(28.0f);
        u.draw_rounded_rect(k2, th.widget_bg, 5.0f);
        u.text(k2, "Cmd", th.text_dim, ALIGN_CENTER);
    }
}

// ---------------------------------------------------------------- card: loading
static void card_loading(ui &u, rect r, const theme &th)
{
    const f64 now = u.ctx->now;
    column c(r, 6.0f);
    if (g_load && !g_load_done && (now - g_load_start) > 1.2) g_load_done = true;
    row b(c.next(30.0f), 10.0f);
    if (u.button(b.next(90.0f), g_load ? (g_load_done ? "Done" : "Loading") : "Load",
                 "load_btn"_id))
    {
        g_load = true;
        g_load_done = false;
        g_load_start = now;
    }
    // Spinner: a fast arc reads as "working" (perceived speed).
    if (!g_load_done)
    {
        const rect sp = b.next(28.0f);
        const vec2 ctr{sp.center_x(), sp.center_y()};
        const f32 a = static_cast<f32>(std::fmod(now * 4.0, 6.2832));
        u.draw_arc(ctr, 10.0f, 3.0f, a, a + 4.7f, th.accent);
    }
    // Skeleton: a moving highlight over placeholder rows.
    if (g_load && !g_load_done)
    {
        const rect sk = c.next(34.0f);
        const f32 s = static_cast<f32>(std::fmod(now * 1.2, 1.0));
        for (i32 i = 0; i < 2; ++i)
        {
            const rect line = rect::make(sk.x, sk.y + static_cast<f32>(i) * 18.0f, sk.w, 12.0f);
            u.draw_rounded_rect(line, th.widget_bg, 6.0f);
            const f32 hx = line.x + (line.w + 80.0f) * s - 80.0f;
            const rect hi = rect::intersect(line, rect::make(hx, line.y, 80.0f, line.h));
            if (hi.w > 0.0f) u.draw_rounded_rect(hi, color{70, 80, 96, 200}, 6.0f);
        }
    }
    else if (g_load_done)
    {
        const f32 a = u.appear("load_appear"_id, 0.25f);
        color cc = th.text;
        cc.a = static_cast<u8>(255.0f * a);
        u.text(c.next(18.0f), "Content loaded", cc, ALIGN_LEFT);
    }
    {
        row m(c.next(22.0f), 10.0f);
        const rect bar = m.next(120.0f);
        u.progress_bar(bar, static_cast<f32>(std::fmod(now * 0.25, 1.0)), th.accent, th.widget_bg);
        // Meter: 5 segments.
        const rect meter = m.next(120.0f);
        const i32 filled = static_cast<i32>(std::fmod(now * 0.8, 6.0));
        for (i32 i = 0; i < 5; ++i)
        {
            const f32 w = (meter.w - 4.0f * 4.0f) / 5.0f;
            const rect seg{meter.x + static_cast<f32>(i) * (w + 4.0f), meter.y + 6.0f, w, 10.0f};
            u.draw_rounded_rect(seg, i < filled ? th.accent : th.widget_bg, 5.0f);
        }
        // Progress ring.
        const rect ring = m.next(34.0f);
        const vec2 ctr{ring.center_x(), ring.center_y()};
        u.draw_arc(ctr, 13.0f, 4.0f, -1.5708f, -1.5708f + 6.2832f, th.widget_bg);
        u.draw_arc(ctr, 13.0f, 4.0f, -1.5708f,
                   -1.5708f + static_cast<f32>(std::fmod(now * 0.5, 1.0)) * 6.2832f, th.accent);
    }
}

// ---------------------------------------------------------------- card: misc
static void card_misc(ui &u, rect r, const theme &th)
{
    const f64 now = u.ctx->now;
    column c(r, 6.0f);
    // Accordion: chevron rotates, body height animates.
    {
        static const char *items[3] = {"What is PufferUI?", "Is it immediate mode?",
                                       "Where are the examples?"};
        for (i32 i = 0; i < 3; ++i)
        {
            const rect head = c.next(24.0f);
            interaction in = u.interact(id_child("acc"_id, static_cast<uiid>(i)), head);
            if (in.hovered) u.set_cursor(CURSOR_HAND);
            u.draw_rect(head, th.widget_bg);
            u.text(head.pad(8.0f, 0.0f), items[i], th.text, ALIGN_LEFT);
            const f32 open =
                u.animate(id_child("acc_t"_id, static_cast<uiid>(i)),
                          g_accordion_open == i ? 1.0f : 0.0f, tween{.duration = 0.18f});
            // chevron: a small rotated polygon
            const f32 a = open * 1.5708f;
            const vec2 ctr{head.right() - 14.0f, head.center_y()};
            const vec2 p0{-4.0f, -2.0f}, p1{4.0f, -2.0f}, p2{0.0f, 3.0f};
            const vec2 pts[3] = {
                {ctr.x + p0.x * std::cos(a) - p0.y * std::sin(a),
                 ctr.y + p0.x * std::sin(a) + p0.y * std::cos(a)},
                {ctr.x + p1.x * std::cos(a) - p1.y * std::sin(a),
                 ctr.y + p1.x * std::sin(a) + p1.y * std::cos(a)},
                {ctr.x + p2.x * std::cos(a) - p2.y * std::sin(a),
                 ctr.y + p2.x * std::sin(a) + p2.y * std::cos(a)},
            };
            u.draw_polygon(std::span<const vec2>(pts, 3), th.text_dim);
            if (in.clicked) g_accordion_open = (g_accordion_open == i) ? -1 : i;
            if (open > 0.01f)
            {
                const rect body = c.next(30.0f * open + 2.0f);
                u.draw_rect(body, th.panel_bg);
                u.text_fit(body.pad(8.0f, 6.0f),
                           "Built from the same public API the framework uses.", th.text_dim);
            }
        }
    }
    {
        row av(c.next(24.0f), 12.0f);
        // Avatar stack: initials with rings.
        static const char *initials[3] = {"AK", "JD", "MK"};
        for (i32 i = 0; i < 3; ++i)
        {
            const rect a{av.next(26.0f)};
            u.draw_rounded_rect(rect::make(a.x - 2.0f, a.y - 2.0f, a.w + 4.0f, a.h + 4.0f), th.bg,
                                15.0f);
            u.draw_rounded_rect(a, color{static_cast<u8>(60 + i * 40), 110, 200, 255}, 13.0f);
            u.text(a, initials[i], color::white(), ALIGN_CENTER);
        }
        const rect rest = av.next(34.0f);
        u.draw_rounded_rect(rest, th.widget_bg, 13.0f);
        u.text(rest, "+4", th.text_dim, ALIGN_CENTER);
    }
    {
        // Copy button with a short "Copied!" feedback (infrequent => delight).
        const rect cb = c.next(28.0f);
        if (u.button(rect::make(cb.x, cb.y, 110.0f, 28.0f),
                     (now - g_copied_at) < 1.2 ? "Copied!" : "Copy link", "copy_btn"_id,
                     "primary"_id))
        {
            g_copied_at = now;
            g_copy_request = true; // the app performs the real copy below
            toast_push("Link copied", false);
        }
    }
}

// ---------------------------------------------------------------- card: scale
static void card_scale(ui &u, rect r, const theme &th)
{
    column c(r, 6.0f);
    const rect b = c.next(44.0f);
    const rect hit = rect::make(b.x, b.y, 160.0f, 44.0f);
    interaction in = u.interact("scale_btn"_id, hit);
    if (in.hovered) u.set_cursor(CURSOR_HAND);
    // Subtle scale while held: the interface feels responsive (emilkowal.ski/ui).
    // Animate the *press amount* so the button starts at full size.
    const f32 pressed = u.animate("scale_t"_id, in.held ? 1.0f : 0.0f, tween{.duration = 0.09f});
    const f32 s = 1.0f - 0.06f * pressed;
    const rect visual{hit.center_x() - 80.0f * s, hit.center_y() - 22.0f * s, 160.0f * s,
                      44.0f * s};
    u.draw_rounded_rect(visual, in.held ? th.accent_hover : th.accent, 8.0f);
    u.text(visual, "Press me", th.bg, ALIGN_CENTER);
    if (in.clicked) g_scaled_presses += 1;
    u.textf(c.next(16.0f), th.text_dim, ALIGN_LEFT, "presses: %d", g_scaled_presses);
}

// ---------------------------------------------------------------- card: data
static void card_data(ui &u, rect r, const theme &th)
{
    column c(r, 6.0f);
    {
        struct row_data
        {
            const char *name;
            i32 size;
        };
        static const row_data ROWS[3] = {{"alpha.cpp", 812}, {"beta.h", 240}, {"gamma.cpp", 1520}};
        const rect head = c.next(22.0f);
        u.draw_rect(head, th.panel_bg);
        if (u.button(rect::make(head.x, head.y, head.w * 0.6f, head.h), "Name", "tbl_name"_id, 0,
                     button_override{.bg = some(color{0, 0, 0, 0}), .radius = some(0.0f)}))
            g_table_sort = (g_table_sort == 0) ? 1 : 0;
        if (u.button(rect::make(head.x + head.w * 0.6f, head.y, head.w * 0.4f, head.h),
                     g_table_sort == 2 ? "Size v" : "Size", "tbl_size"_id, 0,
                     button_override{.bg = some(color{0, 0, 0, 0}), .radius = some(0.0f)}))
            g_table_sort = 2;
        i32 order[3] = {0, 1, 2};
        if (g_table_sort == 1)
        {
            order[0] = 2;
            order[2] = 0; // name: descending
        }
        else if (g_table_sort == 2)
        {
            // size: descending (insertion sort over the three rows)
            for (i32 i = 1; i < 3; ++i)
            {
                const i32 key = order[i];
                i32 j = i - 1;
                while (j >= 0 && ROWS[order[j]].size < ROWS[key].size)
                {
                    order[j + 1] = order[j];
                    --j;
                }
                order[j + 1] = key;
            }
        }
        for (i32 i = 0; i < 3; ++i)
        {
            const row_data &d = ROWS[order[i]];
            const rect row = c.next(20.0f);
            interaction in = u.interact(id_child("trow"_id, static_cast<uiid>(i)), row);
            if (in.hovered) u.draw_rect(row, th.widget_hover);
            u.text(row.pad(6.0f, 0.0f), d.name, th.text, ALIGN_LEFT);
            char size[16];
            std::snprintf(size, sizeof(size), "%d B", d.size);
            // numbers right-aligned: the closest we get to tabular-nums with one font
            u.text(row.pad(6.0f, 0.0f), size, th.text_dim, ALIGN_RIGHT);
        }
    }
    {
        row foot(c.next(22.0f), 6.0f);
        u.text(foot.next(150.0f), "Home  /  Files  /  src", th.text_dim, ALIGN_LEFT);
        for (i32 i = 0; i < 4; ++i)
        {
            char label[4];
            std::snprintf(label, sizeof(label), "%d", i + 1);
            if (u.button(foot.next(26.0f), label, id_child("page"_id, static_cast<uiid>(i)), 0,
                         button_override{.radius = some(4.0f)}))
                g_page = i + 1;
        }
    }
    (void)g_page;
}

// ---------------------------------------------------------------- gallery
static font_handle g_font_bold = FONT_INVALID;

struct pattern_card
{
    const char *title;
    const char *caption;
    f32 height;
    void (*draw)(ui &, rect, const theme &);
};

static const pattern_card CARDS[] = {
    {"Choices", "segmented control, checkbox, switch, radio", 164.0f, card_choices},
    {"Form", "input group and a validation ring", 154.0f, card_form},
    {"Feedback", "toast, alert, tag chips, kbd", 162.0f, card_feedback},
    {"Loading", "spinner, skeleton, progress, meter, ring", 164.0f, card_loading},
    {"Accordion", "chevron rotation, animated body, avatars, copy", 222.0f, card_misc},
    {"Press feedback", "scale while held, 44px targets", 134.0f, card_scale},
    {"Data", "sortable table, breadcrumb, pagination", 160.0f, card_data},
    {"Tooltips", "delay first, then instant between them", 118.0f, card_tooltip},
};
inline constexpr i32 CARD_COUNT = static_cast<i32>(sizeof(CARDS) / sizeof(CARDS[0]));

static void pattern_frame(ui &u, example_app &app)
{
    const theme &th = u.th();
    g_font_bold = app.font_bold;
    const rect client =
        rect::make(0.0f, 0.0f, static_cast<f32>(app.width), static_cast<f32>(app.height));
    u.draw_rect(client, th.bg);

    column page(client.pad(16.0f), 10.0f);
    {
        row head(page.next(30.0f), 10.0f);
        {
            text_scope title =
                u.text_style(20.0f, g_font_bold == FONT_INVALID ? app.font : g_font_bold);
            u.text(head.next(300.0f), "Component patterns", th.text, ALIGN_LEFT);
        }
        if (u.button(head.next(130.0f), "Toast", "g_toast"_id, "primary"_id))
            toast_push("Saved to workspace", false);
        if (u.button(head.next(150.0f), "Open palette", "g_palette"_id)) g_palette_open = true;
        if (u.button(head.next(140.0f), "Open drawer", "g_drawer"_id)) g_drawer_open = true;
    }
    u.text(page.next(16.0f),
           "recreated from coss.com/ui, ui-skills.com, reui.io and emilkowal.ski/ui", th.text_dim,
           ALIGN_LEFT);

    const f32 view_h = page.remaining().h;
    // Card heights are known up front, so set the content height before laying
    // out: overflow is judged from the previous frame, and this keeps the very
    // first frame from collapsing the cards into each other.
    f32 col_l = 0.0f, col_r = 0.0f;
    for (i32 i = 0; i < CARD_COUNT; ++i) ((i % 2) == 0 ? col_l : col_r) += CARDS[i].height + 14.0f;
    scroll_view sv = u.scroll(page.next(view_h), "patterns_scroll"_id);
    sv.set_content_height(max2(col_l, col_r) + 8.0f);
    rect content = sv.content();
    const f32 col_w = max2(120.0f, (content.w - 14.0f) * 0.5f);
    rect left = content;
    rect right = content;
    left.w = col_w;
    right.x += col_w + 14.0f;
    right.w = col_w;
    column lc(left, 14.0f);
    column rc(right, 14.0f);

    auto card = [&](bool left_col, const pattern_card &pc)
    {
        column &c = left_col ? lc : rc;
        const rect r = c.next(pc.height);
        if (r.h < 24.0f) return;

        u.card(r);
        rect inner = r.pad(14.0f);
        rect header = inner.cut_top(38.0f);
        {
            font_handle bold = (g_font_bold == FONT_INVALID) ? u.th().font : g_font_bold;
            text_scope ts = u.text_style(15.0f, bold);
            u.text(header.cut_top(20.0f), pc.title, th.text, ALIGN_LEFT);
        }
        u.text(header.cut_top(16.0f), pc.caption, th.text_dim, ALIGN_LEFT);
        u.draw_line(inner.left(), header.bottom() + 3.0f, inner.right(), header.bottom() + 3.0f,
                    th.border, 1.0f);
        (void)inner.cut_top(9.0f);
        pc.draw(u, inner, th);
    };

    for (i32 i = 0; i < CARD_COUNT; ++i) card((i % 2) == 0, CARDS[i]);

    toast_update(u, static_cast<f32>(app.dt));
    if (g_copy_request)
    {
        sdl3_system_clipboard()->set("https://example.com/patterns");
        g_copy_request = false;
    }
    if (g_palette_open) palette_draw(u, app);
    if (g_drawer_open) drawer_draw(u, app);
    toast_draw(u, app);
}

int main(int argc, char **argv)
{
    return example_run("patterns", 860, 560, argc, argv, pattern_frame);
}
