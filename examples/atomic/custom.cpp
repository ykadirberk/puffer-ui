// custom — building widgets from `interact` + drawing primitives.
//
// Shows: `interact` and the `interaction` fields, `widget_state`-style logic,
// `set_cursor`, `hot_id()`, `local` / `id_child` / `auto_id`, and how to compose
// a widget the framework does not provide.
//
//   pui_ex_custom   (click the switch, the segments, the star rating)
#include "../example_common.h"

#include <cmath>

// A toggle switch: interact + rounded rects + an animated knob. Returns true on
// change (the same contract as the built-in widgets).
static bool toggle_switch(ui &u, rect r, bool &value, uiid id)
{
    const theme &th = u.th();
    interaction in = u.interact(id, r);
    if (in.hovered || in.held) u.set_cursor(CURSOR_HAND);

    const f32 track_h = 24.0f;
    const rect track = rect::make(r.x, r.y + (r.h - track_h) * 0.5f, 46.0f, track_h);
    const f32 t = u.animate(id_child(id, 1), value ? 1.0f : 0.0f, tween{.duration = 0.15f});
    if (in.held)
        u.draw_rounded_rect(track, th.widget_active, track_h * 0.5f);
    else
        u.draw_rounded_rect(track, value ? th.accent : th.widget_bg, track_h * 0.5f);

    color knob = value ? th.text : th.text_dim;
    if (in.hovered) knob = th.text;
    const f32 knob_d = track_h - 6.0f;
    u.draw_rounded_rect(
        rect::make(track.x + 3.0f + (track.w - knob_d - 6.0f) * t, track.y + 3.0f, knob_d, knob_d),
        knob, knob_d * 0.5f);

    if (in.clicked)
    {
        value = !value;
        return true;
    }
    return false;
}

// A segmented control: a row of buttons sharing one id family.
static bool segmented(ui &u, rect r, const char *const *labels, i32 count, i32 &value, uiid id)
{
    const theme &th = u.th();
    bool changed = false;
    const f32 seg_w = (count > 0) ? (r.w - static_cast<f32>(count - 1) * 2.0f) / count : r.w;
    row seg(r, 2.0f);
    for (i32 i = 0; i < count; ++i)
    {
        const rect item = seg.next(seg_w);
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

// A star rating built from `draw_polygon` + per-star interact rects.
static bool star_rating(ui &u, rect r, i32 &value, i32 stars, uiid id)
{
    const theme &th = u.th();
    bool changed = false;
    row seg(r, 4.0f);
    const f32 star_w = (stars > 0) ? (r.w - static_cast<f32>(stars - 1) * 4.0f) / stars : r.w;
    for (i32 i = 0; i < stars; ++i)
    {
        const rect cell = seg.next(star_w);
        interaction in = u.interact(id_child(id, static_cast<uiid>(i)), cell);
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
        const bool filled = i < value;
        color c = filled ? color{230, 180, 60, 255} : th.widget_bg;
        if (in.hovered) c = filled ? color{250, 200, 90, 255} : th.widget_hover;
        u.draw_polygon(std::span<const vec2>(pts, 10), c);
        if (in.clicked)
        {
            value = i + 1;
            changed = true;
        }
    }
    return changed;
}

static bool g_enabled = true;
static i32 g_tab = 1;
static i32 g_rating = 3;

static void custom_frame(ui &u, example_app &app)
{
    const theme &th = u.th();
    example_page page = example_begin_page(
        u, app, "Custom widgets", "compose first-class widgets from interact + primitives");
    column col(page.content, example_ui::SECTION_GAP);

    {
        const rect body = example_section(u, col.next(216.0f), "Custom widgets",
                                          "interact(id, rect) + primitives = a first-class widget",
                                          app.font_bold);
        column c(body, 6.0f);
        {
            row r(c.next(28.0f), 8.0f);
            (void)toggle_switch(u, r.next(180.0f), g_enabled, "cu_switch"_id);
            char line[64];
            std::snprintf(line, sizeof(line), "enabled: %s", g_enabled ? "yes" : "no");
            u.text(r.remaining(), line, th.text_dim, ALIGN_RIGHT);
        }

        example_caption(u, c.next(16.0f), "segmented(id_child(id, i))");
        {
            static const char *labels[3] = {"One", "Two", "Three"};
            (void)segmented(u, c.next(30.0f), labels, 3, g_tab, "cu_tabs"_id);
        }

        example_caption(u, c.next(16.0f), "star rating (draw_polygon + per-star ids)");
        (void)star_rating(u, c.next(34.0f), g_rating, 5, "cu_stars"_id);
    }

    // Raw `interaction` fields for a probe rect: everything a widget can need.
    {
        const rect body = example_section(
            u, col.next(160.0f), "Interaction fields",
            "hovered, pressed, activated, held, clicked, double, right, focused", app.font_bold);
        column c(body, 6.0f);
        {
            const rect probe = c.next(44.0f);
            interaction in = u.interact("cu_probe"_id, probe);
            if (in.hovered) u.set_cursor(CURSOR_HAND);
            u.draw_rect(probe, in.held ? th.accent : in.hovered ? th.widget_hover : th.widget_bg);
            char line[192];
            std::snprintf(line, sizeof(line),
                          "hovered %d  pressed %d  activated %d  held %d  clicked %d  double %d  "
                          "right %d  focused %d",
                          in.hovered ? 1 : 0, in.pressed ? 1 : 0, in.activated ? 1 : 0,
                          in.held ? 1 : 0, in.clicked ? 1 : 0, in.double_clicked ? 1 : 0,
                          in.right_clicked ? 1 : 0, in.focused ? 1 : 0);
            u.text(probe.pad(8.0f, 0.0f), line, in.held ? th.bg : th.text_dim, ALIGN_LEFT);
        }

        {
            const uiid hot = u.hot_id();
            char line[96];
            std::snprintf(line, sizeof(line), "hot_id(): 0x%016llX (widget under the pointer)",
                          static_cast<unsigned long long>(hot));
            u.text(c.next(18.0f), line, th.text_dim, ALIGN_LEFT);
        }
        u.text(c.next(18.0f),
               "ids from `id_child`/`local`/`auto_id` keep instances independent; hover uses the "
               "theme cursor",
               th.text_dim, ALIGN_LEFT);
    }
}

int main(int argc, char **argv)
{
    return example_run("custom", 720, 500, argc, argv, custom_frame);
}
