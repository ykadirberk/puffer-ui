// relayout — animated layout: `u.animate_rect` springs a rect from where it
// was drawn to where the rect cutting puts it this frame.
//
// Shows: `animate_rect` keyed by the item's identity inside `u.scope(id)`,
// `.origin` (the scrolled content's corner: scrolling never lags), `.from`
// (an entrance grows out of a point), an exit that re-submits
// `motion_info(key).target` while it shrinks, and `motion_info().velocity`.
//
//   pui_ex_relayout   (drag Tile width, switch Grid/List, Add/Shuffle, click a tile)
#include "../example_common.h"

#include <cmath>

struct relayout_item
{
    u32 id = 0;
    f64 leaving = -1.0; // the time its exit started (-1: staying)
};

static relayout_item g_items[64];
static i32 g_count = 0;
static u32 g_next = 1;
static i32 g_mode = 0; // 0 grid, 1 list
static f32 g_tile_w = 110.0f;
static u32 g_rng = 7u;

static constexpr f32 EXIT_S = 0.25f; // exit duration

static f32 relayout_rand()
{
    g_rng = g_rng * 1664525u + 1013904223u;
    return static_cast<f32>(g_rng >> 8) / 16777216.0f;
}

static void relayout_add()
{
    if (g_count >= 64) return;
    const i32 at = static_cast<i32>(relayout_rand() * static_cast<f32>(g_count + 1));
    for (i32 i = g_count; i > at; --i) g_items[i] = g_items[i - 1];
    g_items[at] = relayout_item{g_next++, -1.0};
    g_count += 1;
}

static color relayout_color(u32 id)
{
    const f32 h = static_cast<f32>(id % 12) / 12.0f;
    const f32 r = 0.5f + 0.5f * std::cos(6.2832f * h);
    const f32 g = 0.5f + 0.5f * std::cos(6.2832f * (h - 0.333f));
    const f32 b = 0.5f + 0.5f * std::cos(6.2832f * (h - 0.667f));
    return color{static_cast<u8>(70 + 160 * r), static_cast<u8>(70 + 160 * g),
                 static_cast<u8>(70 + 160 * b), 255};
}

static void relayout_frame(ui &u, example_app &app)
{
    const theme &th = u.th();
    if (g_count == 0 && g_next == 1)
        for (i32 i = 0; i < 12; ++i) relayout_add();

    example_page page = example_begin_page(
        u, app, "Animated layout",
        "u.animate_rect(key, rect, {.origin}) - re-cut every frame, every change springs");
    column col(page.content, example_ui::SECTION_GAP);

    {
        const rect body = example_section(
            u, col.next(96.0f), "Change the layout",
            "the cutting decides where tiles go; animate_rect decides how they get there",
            app.font_bold);
        row r(body, u.spacing());
        static const char *const modes[2] = {"Grid", "List"};
        (void)comp::segmented(u, r.next(140.0f), modes, g_mode, {.id = "rl_mode"_id});
        (void)u.slider_float(r.next(200.0f), "Tile width", g_tile_w, 70.0f, 180.0f, "rl_w"_id,
                             "%.0f");
        if (u.button(r.next(u.button_size("Add").x), "Add", "rl_add"_id, {.role = "primary"_id}))
            relayout_add();
        if (u.button(r.next(u.button_size("Shuffle").x), "Shuffle", "rl_shuffle"_id))
            for (i32 i = g_count - 1; i > 0; --i)
            {
                const i32 j = static_cast<i32>(relayout_rand() * static_cast<f32>(i + 1)) % (i + 1);
                const relayout_item t = g_items[i];
                g_items[i] = g_items[j];
                g_items[j] = t;
            }
    }

    const rect area = example_section(
        u, col.remaining(), "Tiles",
        "click a tile to remove it; the list scrolls without lag (.origin = content corner)",
        app.font_bold);
    {
        scroll_view sv = u.scroll(area, "rl_scroll"_id);
        const rect content = sv.content();
        const vec2 origin{content.x, content.y}; // moves with the scroll offset

        // the layout: only items that are staying take a slot
        i32 staying = 0;
        for (i32 i = 0; i < g_count; ++i)
            if (g_items[i].leaving < 0.0) staying += 1;
        const f32 gap = 8.0f;
        const grid_cursor gc =
            auto_fit_grid(rect::make(content.x, content.y, content.w, 1.0e6f),
                          staying > 0 ? staying : 1, g_tile_w, g_tile_w * 0.7f, gap);
        column list(rect::make(content.x, content.y, content.w, 1.0e6f), 6.0f);
        f32 used = 0.0f;
        i32 slot = 0;
        i32 remove = -1;
        for (i32 i = 0; i < g_count; ++i)
        {
            relayout_item &it = g_items[i];
            id_scope sc = u.scope(it.id); // identity, not index: reorders animate correctly
            const uiid key = u.local("pos");
            rect r{};
            f32 k = 1.0f; // exit progress, shrinking the drawn rect
            if (it.leaving >= 0.0)
            {
                // exits: keep heading to the last target while shrinking away
                r = u.animate_rect(key, u.motion_info(key).target, {.origin = origin});
                k = 1.0f - clampf(static_cast<f32>((u.ctx->now - it.leaving) / EXIT_S), 0.0f, 1.0f);
                if (k <= 0.0f) remove = i;
            }
            else
            {
                const rect target = g_mode == 0 ? gc.cell(slot) : list.next(36.0f);
                slot += 1;
                used = max2(used, target.bottom() - content.y);
                // entrances grow out of their own center
                const rect dot = rect::make(target.center_x(), target.center_y(), 0.0f, 0.0f);
                r = u.animate_rect(key, target, {.origin = origin, .from = some(dot)});
            }
            const f32 sw = r.w * k, sh = r.h * k;
            const rect d = rect::make(r.center_x() - sw * 0.5f, r.center_y() - sh * 0.5f, sw, sh);
            if (d.w < 1.0f || d.h < 1.0f) continue;
            const interaction in = u.interact(u.local("hit"), d, it.leaving < 0.0, false);
            if (in.hovered) u.set_cursor(CURSOR_HAND);
            if (in.clicked) it.leaving = u.ctx->now;
            // speed shows as a lighter edge: motion_info reports the velocity
            const vec2 v = u.motion_info(key).velocity;
            const f32 speed = clampf(std::sqrt(v.x * v.x + v.y * v.y) / 1500.0f, 0.0f, 1.0f);
            const color base = relayout_color(it.id);
            u.draw_rounded_rect(d, in.hovered ? theme_lerp_color(base, color::white(), 0.2f) : base,
                                8.0f);
            if (speed > 0.02f)
                u.draw_rounded_rect(d.pad(3.0f),
                                    theme_lerp_color(base, color::white(), 0.35f * speed), 6.0f);
            u.textf(d, color{20, 22, 30, 255}, ALIGN_CENTER, "%u", it.id);
        }
        if (remove >= 0)
        {
            for (i32 i = remove; i < g_count - 1; ++i) g_items[i] = g_items[i + 1];
            g_count -= 1;
        }
        sv.set_content_height(used + 4.0f);
        (void)th;
    }
}

int main(int argc, char **argv)
{
    return example_run("relayout", 760, 620, argc, argv, relayout_frame);
}
