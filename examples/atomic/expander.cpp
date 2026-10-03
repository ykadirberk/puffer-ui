// expander — collapsible sections with animated height.
//
// Shows: an expander/disclosure header (rotating chevron, hover state, cursor),
// an animated body height that is clipped while it grows, fading summaries, a
// spring vs tween comparison, an accordion (one open at a time), nesting, and
// expanders inside a scrolled list. `Reduced motion` snaps everything, and the
// duration slider shows why UI motion stays under ~300 ms.
//
//   pui_ex_expander
#include "../example_common.h"

#include <cmath>

// ---------------------------------------------------------------- state
static bool g_basic[3] = {true, false, false};
static i32 g_accordion = 0; // -1 = all closed
static bool g_outer = true;
static bool g_inner = false;
static bool g_rows[6] = {true, false, false, false, false, false};
static bool g_reduced = false;
static bool g_spring = false;
static f32 g_duration = 0.22f;
static font_handle g_bold = FONT_INVALID;

// ---------------------------------------------------------------- expander
// `animate()` must be called once per key per frame, so the height is measured
// first (which advances the animation) and the same value is used to draw.
struct exp_measured
{
    f32 t = 0.0f;      // 0 closed .. 1 open
    f32 height = 0.0f; // header + gap + the visible part of the body
};

inline constexpr f32 EXP_HEADER_H = 40.0f;
inline constexpr f32 EXP_BODY_GAP = 6.0f;

static exp_measured exp_measure(ui &u, uiid id, bool open, f32 content_h)
{
    const f32 t = g_spring ? u.animate(id_child(id, 1), open ? 1.0f : 0.0f,
                                       spring{.stiffness = 340.0f, .damping_ratio = 0.85f})
                           : u.animate(id_child(id, 1), open ? 1.0f : 0.0f,
                                       tween{.duration = g_duration, .curve = easing::EASE_OUT});
    exp_measured m;
    m.t = t;
    m.height = EXP_HEADER_H + ((t > 0.004f) ? EXP_BODY_GAP + content_h * t : 0.0f);
    return m;
}

static vec2 exp_rotate(vec2 p, f32 a)
{
    const f32 c = std::cos(a), s = std::sin(a);
    return {p.x * c - p.y * s, p.x * s + p.y * c};
}

static void exp_draw(ui &u, const theme &th, rect area, uiid id, const char *title,
                     const char *summary, bool &open, f32 content_h, f32 t,
                     function_ref<void(ui &, rect)> body)
{
    // header: chevron + title + a summary that fades away as the body opens
    const rect head{area.x, area.y, area.w, EXP_HEADER_H};
    interaction in = u.interact(id_child(id, 0), head);
    if (in.hovered || in.held) u.set_cursor(CURSOR_HAND);

    const color hbg = (open || in.hovered) ? th.widget_hover : th.widget_bg;
    if (th.border_thickness > 0.0f)
    {
        u.draw_rounded_rect(head, th.border, th.radius);
        u.draw_rounded_rect(head.pad(th.border_thickness), hbg,
                            max2(0.0f, th.radius - th.border_thickness));
    }
    else
    {
        u.draw_rounded_rect(head, hbg, th.radius);
    }

    {
        const vec2 ctr{head.x + 18.0f, head.center_y()};
        const vec2 local[3] = {exp_rotate(vec2{-3.5f, -2.5f}, t * 1.5708f),
                               exp_rotate(vec2{3.5f, -2.5f}, t * 1.5708f),
                               exp_rotate(vec2{0.0f, 3.0f}, t * 1.5708f)};
        const vec2 world[3] = {{ctr.x + local[0].x, ctr.y + local[0].y},
                               {ctr.x + local[1].x, ctr.y + local[1].y},
                               {ctr.x + local[2].x, ctr.y + local[2].y}};
        u.draw_polygon(std::span<const vec2>(world, 3), in.hovered ? th.accent : th.text_dim);
    }
    {
        text_scope ts = u.text_style(15.0f, g_bold);
        u.text(rect::make(head.x + 34.0f, head.y, head.w - 150.0f, head.h), title, th.text,
               ALIGN_LEFT);
    }
    if (summary && summary[0])
    {
        color sc = th.text_dim;
        sc.a = static_cast<u8>(static_cast<f32>(sc.a) * (1.0f - clampf(t, 0.0f, 1.0f)));
        u.text(rect::make(head.right() - 134.0f, head.y, 118.0f, head.h), summary, sc, ALIGN_RIGHT);
    }
    if (in.clicked) open = !open;

    if (t <= 0.004f) return;

    // The body is clipped to the animated height while its content keeps its
    // full layout, so it reveals smoothly instead of reflowing.
    const rect body_area{area.x, head.bottom() + EXP_BODY_GAP, area.w, content_h * t};
    region reg(u, id_child(id, 2), body_area);
    body(u, rect{body_area.x, body_area.y, body_area.w, content_h}); // full-height layout
}

static void exp_paragraph(ui &u, rect r, const theme &th, const char *text, uiid salt,
                          bool controls)
{
    column c(r, 8.0f);
    const measure_size m = u.measure_text(text, r.w);
    u.text(c.next(m.height), text, th.text_dim, ALIGN_LEFT);
    if (controls)
    {
        row b(c.next(26.0f), 8.0f);
        (void)u.button(b.next(96.0f), "Details", id_child(salt, 1));
        static f32 amount = 0.4f;
        (void)u.slider_float(b.remaining(), "Amount", amount, 0.0f, 1.0f, id_child(salt, 2),
                             "%.2f");
    }
}

static const char *const EXP_TITLES[3] = {"Account", "Notifications", "Advanced"};
static const char *const EXP_SUMMARIES[3] = {"3 settings", "5 settings", "12 settings"};
static const char *const EXP_TEXT =
    "The body keeps its full layout and is revealed by a clip that grows with the "
    "animation, so the content below moves smoothly instead of jumping.";

// ---------------------------------------------------------------- frame
static void expander_frame(ui &u, example_app &app)
{
    const theme &th = u.th();
    g_bold = app.font_bold;
    u.set_reduced_motion(g_reduced);

    example_page page =
        example_begin_page(u, app, "Expander", "collapsible sections with animated height");
    scroll_view sv = u.scroll(page.content, "exp_page"_id);

    // Section heights are known up front, so the content height is set before
    // laying out: a scroll view judges overflow from the previous frame, and
    // pre-setting it keeps the very first frame from collapsing the sections.
    constexpr f32 H_MOTION = 160.0f, H_INDEP = 468.0f, H_ACCORDION = 280.0f, H_NESTED = 300.0f,
                  H_LIST = 300.0f;
    constexpr f32 H_TOTAL = H_MOTION + H_INDEP + H_ACCORDION + H_NESTED + H_LIST +
                            4.0f * example_ui::SECTION_GAP + 4.0f;
    sv.set_content_height(H_TOTAL);

    column col(sv.content(), example_ui::SECTION_GAP);

    auto section = [&](f32 h, const char *title, const char *caption)
    { return example_section(u, col.next(h), title, caption, app.font_bold); };

    // ---- motion controls
    {
        rect body = section(H_MOTION, "Motion", "the same expander with different motion settings");
        column c(body, 8.0f);
        {
            row r(c.next(28.0f), 8.0f);
            if (u.button(r.next(110.0f), "Expand all", "exp_all"_id, "primary"_id))
            {
                g_basic[0] = g_basic[1] = g_basic[2] = true;
                g_outer = true;
                g_inner = true;
                for (i32 i = 0; i < 6; ++i) g_rows[i] = true;
            }
            if (u.button(r.next(124.0f), "Collapse all", "exp_none"_id))
            {
                g_basic[0] = g_basic[1] = g_basic[2] = false;
                g_outer = false;
                g_inner = false;
                g_accordion = -1;
                for (i32 i = 0; i < 6; ++i) g_rows[i] = false;
            }
            u.textf(r.remaining(), th.text_dim, ALIGN_RIGHT, "duration: %.0f ms",
                    static_cast<double>(g_duration * 1000.0f));
        }
        (void)u.slider_float(c.next(26.0f), "Tween duration", g_duration, 0.05f, 0.6f,
                             "exp_duration"_id, "%.2fs");
        {
            row r(c.next(24.0f), 18.0f);
            (void)u.checkbox(r.next(150.0f), "Spring instead", g_spring, "exp_spring"_id);
            (void)u.checkbox(r.next(170.0f), "Reduced motion", g_reduced, "exp_reduced"_id);
            u.text(r.remaining(), "UI motion should stay under ~300 ms", th.text_dim, ALIGN_RIGHT);
        }
    }

    // ---- independent expanders
    {
        rect body = section(H_INDEP, "Independent expanders",
                            "each one animates on its own; the chevron rotates");
        column c(body, 10.0f);
        for (i32 i = 0; i < 3; ++i)
        {
            const uiid id = id_child("exp_basic"_id, static_cast<uiid>(i));
            const exp_measured m = exp_measure(u, id, g_basic[i], 68.0f);
            const rect area = c.next(m.height);
            exp_draw(u, th, area, id, EXP_TITLES[i], EXP_SUMMARIES[i], g_basic[i], 68.0f, m.t,
                     [&](ui &u2, rect r2) { exp_paragraph(u2, r2, th, EXP_TEXT, id, true); });
        }
    }

    // ---- accordion
    {
        rect body = section(H_ACCORDION, "Accordion", "one section open at a time");
        column c(body, 10.0f);
        for (i32 i = 0; i < 3; ++i)
        {
            const uiid id = id_child("exp_acc"_id, static_cast<uiid>(i));
            const bool open = (g_accordion == i);
            const exp_measured m = exp_measure(u, id, open, 44.0f);
            const rect area = c.next(m.height);
            bool toggled = open;
            exp_draw(u, th, area, id, EXP_TITLES[i], nullptr, toggled, 44.0f, m.t,
                     [&](ui &u2, rect r2) { exp_paragraph(u2, r2, th, EXP_TEXT, id, false); });
            if (toggled != open) g_accordion = toggled ? i : -1;
        }
    }

    // ---- nested
    {
        rect body = section(H_NESTED, "Nested", "an expander inside an expander");
        column c(body, 10.0f);
        const uiid outer_id = "exp_outer"_id;
        const exp_measured om = exp_measure(u, outer_id, g_outer, 156.0f);
        const rect outer_area = c.next(om.height);
        exp_draw(u, th, outer_area, outer_id, "Outer section", "holds another one", g_outer, 156.0f,
                 om.t,
                 [&](ui &u2, rect r2)
                 {
                     column inner_col(r2, 10.0f);
                     const measure_size m = u2.measure_text(EXP_TEXT, r2.w);
                     u2.text(inner_col.next(m.height), EXP_TEXT, th.text_dim, ALIGN_LEFT);
                     const uiid inner_id = "exp_inner"_id;
                     const exp_measured im = exp_measure(u2, inner_id, g_inner, 44.0f);
                     const rect inner_area = inner_col.next(im.height);
                     exp_draw(u2, th, inner_area, inner_id, "Inner section", "nested", g_inner,
                              44.0f, im.t, [&](ui &u3, rect r3)
                              { exp_paragraph(u3, r3, th, EXP_TEXT, inner_id, false); });
                 });
    }

    // ---- inside a scrolled list
    {
        rect body = section(H_LIST, "In a scrolled list", "expanders grow inside a scroll view");
        scroll_view list = u.scroll(body, "exp_scroll"_id);
        column c(list.content(), 8.0f);
        f32 list_h = 0.0f;
        for (i32 i = 0; i < 6; ++i)
        {
            const uiid id = id_child("exp_row"_id, static_cast<uiid>(i));
            char title[32];
            std::snprintf(title, sizeof(title), "Row %d", i + 1);
            const exp_measured m = exp_measure(u, id, g_rows[i], 44.0f);
            const rect area = c.next(m.height);
            exp_draw(u, th, area, id, title, g_rows[i] ? nullptr : "collapsed", g_rows[i], 44.0f,
                     m.t, [&](ui &u2, rect r2) { exp_paragraph(u2, r2, th, EXP_TEXT, id, false); });
            list_h += m.height + 8.0f;
        }
        list.set_content_height(list_h + 4.0f);
    }
}

int main(int argc, char **argv)
{
    return example_run("expander", 720, 600, argc, argv, expander_frame);
}
