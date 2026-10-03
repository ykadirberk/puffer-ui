// pui_showcase.cpp - "Nebula": the motion-heavy PufferUI showcase.
//
// One app that leans on every moving part of the library at once:
//
//   * Layout that re-flows AND animates: every card, tile and board card is laid
//     out with plain rect cutting, and `u.animate_rect` springs it from the
//     rect it had to the rect it gets (relative to its parent's corner, so
//     scrolling never lags). Resizing the window, collapsing the sidebar,
//     switching the layout lab's mode, changing density or dragging a board
//     card all glide; `u.motion_info` velocity drives squash and stretch.
//   * Keyed library animation everywhere: `animate` (tween / spring), `smooth`,
//     `appear`, `animate_color`, and `theme_lerp` for the dark/light cross-fade.
//   * Effects from the public drawing API only: backdrop blur (the frosted top
//     bar, glass cards, a draggable lens, the page "focus pull"), vertex-colored
//     gradients, glows, rims, confetti, ripples, a radial menu, a pseudo-3D tilt
//     card, shimmer skeletons and a morphing blob.
//   * Components: drawer + accordions (settings), command palette, toasts,
//     segmented controls, switches, a radio group, context menus, tooltips.
//
//   pui_showcase              Ctrl+Space: command palette, Ctrl+Up/Down: pages
//   pui_showcase --page N     start on page N (0..4)
//   pui_showcase --light      start in the light theme;  --no-hud  hide the perf card
#include "example_common.h"
#include "tutorial/glass.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ============================================================ pages and tables
enum page_id : i32
{
    PAGE_OVERVIEW = 0,
    PAGE_LAYOUT,
    PAGE_BOARD,
    PAGE_EFFECTS,
    PAGE_MOTION,
    PAGE_COUNT
};

static const char *const PAGE_NAMES[PAGE_COUNT] = {"Overview", "Layout lab", "Board", "Effects",
                                                   "Motion"};
static const char *const PAGE_CAPTIONS[PAGE_COUNT] = {
    "live metrics, count-ups and a streaming chart",
    "one set of tiles, five layouts - every change springs",
    "drag cards between columns; the others make room",
    "blur, light, particles and pseudo-3D from plain triangles",
    "springs, curves and staggers you can tune live"};

enum icon_kind : i32
{
    ICON_GRID,
    ICON_LAYERS,
    ICON_BOARD,
    ICON_SPARK,
    ICON_WAVE,
    ICON_GEAR,
    ICON_BELL,
    ICON_SEARCH,
    ICON_PLUS,
    ICON_MINUS,
    ICON_CHEVRON,
    ICON_ROCKET,
    ICON_SHUFFLE,
    ICON_SORT,
    ICON_CHECK,
};
static const i32 PAGE_ICONS[PAGE_COUNT] = {ICON_GRID, ICON_LAYERS, ICON_BOARD, ICON_SPARK,
                                           ICON_WAVE};

static constexpr i32 ACCENT_COUNT = 5;
static const color ACCENTS[ACCENT_COUNT] = {{132, 112, 255, 255},
                                            {255, 92, 160, 255},
                                            {32, 196, 184, 255},
                                            {255, 156, 56, 255},
                                            {72, 156, 255, 255}};
static const char *const ACCENT_NAMES[ACCENT_COUNT] = {"Violet", "Rose", "Teal", "Amber", "Azure"};

static const char *const TAG_NAMES[4] = {"design", "engine", "ops", "research"};
static const color TAG_COLORS[4] = {
    {255, 110, 180, 255}, {255, 160, 70, 255}, {60, 200, 190, 255}, {120, 150, 255, 255}};
static const char *const COLUMN_NAMES[3] = {"Backlog", "In flight", "Shipped"};
static const color COLUMN_COLORS[3] = {
    {150, 160, 200, 255}, {255, 170, 70, 255}, {80, 210, 140, 255}};

// ============================================================ small math
static f32 sat(f32 v)
{
    return clampf(v, 0.0f, 1.0f);
}
static f32 lerpf(f32 a, f32 b, f32 t)
{
    return a + (b - a) * t;
}
static color mixc(color a, color b, f32 t)
{
    return theme_lerp_color(a, b, sat(t));
}
static rect grow(rect r, f32 by)
{
    return rect::make(r.x - by, r.y - by, max2(0.0f, r.w + 2.0f * by), max2(0.0f, r.h + 2.0f * by));
}
static rect shifted(rect r, f32 dx, f32 dy)
{
    return rect::make(r.x + dx, r.y + dy, r.w, r.h);
}
static rect scaled(rect r, f32 sx, f32 sy)
{
    const f32 w = max2(0.0f, r.w * sx), h = max2(0.0f, r.h * sy);
    return rect::make(r.center_x() - w * 0.5f, r.center_y() - h * 0.5f, w, h);
}
static color hsv(f32 h, f32 s, f32 v, u8 a = 255)
{
    h -= std::floor(h);
    const f32 i = std::floor(h * 6.0f);
    const f32 f = h * 6.0f - i;
    const f32 p = v * (1.0f - s), q = v * (1.0f - f * s), t = v * (1.0f - (1.0f - f) * s);
    f32 r = v, gg = t, b = p;
    switch (static_cast<i32>(i) % 6)
    {
    case 1:
        r = q, gg = v, b = p;
        break;
    case 2:
        r = p, gg = v, b = t;
        break;
    case 3:
        r = p, gg = q, b = v;
        break;
    case 4:
        r = t, gg = p, b = v;
        break;
    case 5:
        r = v, gg = p, b = q;
        break;
    default:
        break;
    }
    return color{static_cast<u8>(r * 255.0f + 0.5f), static_cast<u8>(gg * 255.0f + 0.5f),
                 static_cast<u8>(b * 255.0f + 0.5f), a};
}
static u32 hash_u32(u32 x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
static f32 hash01(u32 x)
{
    return static_cast<f32>(hash_u32(x) & 0xFFFFFFu) / 16777216.0f;
}

// ============================================================ app state
struct particle
{
    f32 x = 0, y = 0, vx = 0, vy = 0, rot = 0, vrot = 0, age = 0, ttl = 1, size = 4;
    color c{};
};
struct ripple
{
    uiid owner = 0;
    vec2 p{};
    f64 born = 0.0;
};
struct pulse
{
    vec2 p{};
    f64 born = 0.0;
};
struct lab_tile
{
    u32 id = 0;
    f32 hue = 0.0f;
    f32 tall = 0.5f; // masonry height factor
    f64 dying = -1.0;
};
struct board_card
{
    u32 id = 0;
    i32 col = 0;
    i32 tag = 0;
    f32 progress = 0.0f;
    char title[64]{};
    f64 dying = -1.0;
};
struct feed_event
{
    u32 serial = 0;
    i32 what = 0;
    f64 at = 0.0;
};

struct showcase
{
    font_handle font = FONT_INVALID, bold = FONT_INVALID;

    // navigation
    i32 page = PAGE_OVERVIEW, prev_page = PAGE_OVERVIEW;
    u32 page_serial = 0;
    f64 page_since = 0.0;
    f32 nav_dir = 1.0f;
    f32 age = 0.0f; // seconds on the current page
    f32 scroll_target[PAGE_COUNT]{};
    f32 content_h[PAGE_COUNT]{};
    f32 scroll_anchor_y = 0.0f, scroll_anchor_t = 0.0f;

    // shell
    bool sidebar_open = true, auto_collapse = true, ambient = true, reduced_motion = false;
    i32 theme_mode = 0, accent = 0, density = 1, transition = 0;
    bool drawer_open = false, palette_open = false, notes_open = false;
    bool acc_look = true, acc_motion = true, acc_layout = false;
    comp::palette_state palette;
    u32 palette_serial = 0;
    comp::toast_host toasts;
    char history[6][96]{};
    f64 history_at[6]{};
    i32 history_count = 0, unread = 0;
    u32 badge_serial = 0;
    f64 bell_at = -10.0;
    rect bell_rect{};
    bool blocked = false;  // a modal layer is up: the custom widgets below it yield
    bool motion_on = true; // ambient motion (background, streams) is running
    f32 light = 0.0f;      // 0 dark .. 1 light (animated)
    color accent_c{};      // animated accent
    u32 rng = 0x9E3779B9u;
    // performance HUD: frame times (ms, newest at ft_head - 1), 0.25 s averages
    static constexpr i32 FT_N = 120;
    f32 ft_hist[FT_N]{};
    i32 ft_head = 0, ft_count = 0; // count: samples recorded so far (up to FT_N)
    f64 fps_acc_t = 0.0, fps_acc_build = 0.0, fps_acc_max = 0.0;
    i32 fps_acc_n = 0;
    f32 fps_shown = 60.0f, ms_shown = 16.7f, build_shown = 0.0f, max_shown = 16.7f;
    // vsync starts OFF so the HUD measures the real cost of a frame; the device
    // is told only when the setting changes (the backend starts with it on)
    bool vsync = false, vsync_applied = true;
    f32 build_ms = 0.0f; // the previous frame's CPU build time
    bool hud_compact = false;
    f32 clock = 0.0f; // ambient clock (stops when ambient motion is off)

    std::vector<particle> particles;
    std::vector<ripple> ripples;
    std::vector<pulse> pulses;

    // overview
    f32 kpi[4] = {48210.0f, 128940.0f, 182.0f, 99.98f};
    f32 kpi_delta[4] = {12.4f, 8.1f, -3.2f, 0.02f};
    f32 gauges[3] = {0.62f, 0.48f, 0.81f};
    f64 gauge_next = 2.6;
    i32 bar_range = 0, bar_range_seen = 0;
    u32 bar_seed = 1;
    f32 bars_from[12]{}, bars_to[12]{};
    f64 bars_at = 0.0;
    i32 chart_range = 0;
    f32 chart_phase = 0.0f;
    std::vector<feed_event> feed;
    u32 feed_serial = 1;
    f64 feed_next = 3.0;

    // layout lab
    std::vector<lab_tile> tiles;
    u32 next_tile = 1, lab_selected = 0;
    i32 lab_mode = 0;
    f32 lab_size = 112.0f, lab_gap = 12.0f;

    // board
    std::vector<board_card> cards;
    u32 next_card = 1, expanded = 0;
    std::string draft;
    u32 drag_id = 0;
    bool drag_live = false;
    vec2 grab{}, press{};
    i32 drop_col = -1, drop_index = -1;
    i32 col_count_seen[3]{};

    // effects
    vec2 lens{0.3f, 0.45f};
    vec2 lens_anchor{};
    f32 lens_blur = 18.0f;
    bool radial_open = false;
    i32 radial_kb = 0;
    bool loaded = false;
    f64 fireworks_at[3] = {-1.0, -1.0, -1.0};
    vec2 fireworks_pos[3]{};

    // motion
    f32 stiff = 180.0f, damp = 0.35f, mass = 1.0f;
    i32 preset = 1;
    bool kick = false;
    f64 kick_at = -10.0;
    f32 ball_prev = 0.0f;
    vec2 trail[10]{};
    i32 trail_head = 0;
    f32 trail_acc = 0.0f; // the trail samples by time, not per frame
    bool wave = false;
    f64 wave_at = -10.0;
    f64 curves_at = 0.0;
    i32 swatch = 0;
};

static showcase g_app;
static i32 g_start_page = -1;

static f32 frand(showcase &s)
{
    s.rng ^= s.rng << 13;
    s.rng ^= s.rng >> 17;
    s.rng ^= s.rng << 5;
    return static_cast<f32>(s.rng & 0xFFFFFFu) / 16777216.0f;
}

static vec2 pointer(ui &u)
{
    return vec2{u.ctx->mouse_x, u.ctx->mouse_y};
}

// ============================================================ drawing helpers
static void fill_circle(ui &u, vec2 c, f32 r, color col)
{
    if (r <= 0.25f || col.a == 0) return;
    u.draw_rounded_rect(rect::make(c.x - r, c.y - r, r * 2.0f, r * 2.0f), col, r);
}

static void gradient_quad(ui &u, rect r, color tl, color tr, color br, color bl)
{
    vertex v[4] = {{r.x, r.y, 0.0f, 0.0f, tl},
                   {r.right(), r.y, 0.0f, 0.0f, tr},
                   {r.right(), r.bottom(), 0.0f, 0.0f, br},
                   {r.x, r.bottom(), 0.0f, 0.0f, bl}};
    const i32 idx[6] = {0, 1, 2, 0, 2, 3};
    u.draw_triangles(nullptr, v, 4, idx, 6);
}

// A rounded rect whose color is interpolated from its four corners: a fan from
// the center over the outline (barycentric interpolation does the gradient),
// over a same-shape anti-aliased underlay so the edge stays soft.
static void gradient_round(ui &u, rect r, f32 radius, color tl, color tr, color br, color bl)
{
    if (r.w <= 0.5f || r.h <= 0.5f) return;
    radius = clampf(radius, 0.0f, min2(r.w, r.h) * 0.5f);
    constexpr i32 SEG = 6;
    constexpr i32 RING = 4 * (SEG + 1);
    vertex v[1 + RING];
    i32 idx[3 * RING];
    const auto col_at = [&](f32 x, f32 y)
    {
        const f32 fx = sat((x - r.x) / r.w), fy = sat((y - r.y) / r.h);
        return mixc(mixc(tl, tr, fx), mixc(bl, br, fx), fy);
    };
    const vec2 centers[4] = {{r.x + radius, r.y + radius},
                             {r.right() - radius, r.y + radius},
                             {r.right() - radius, r.bottom() - radius},
                             {r.x + radius, r.bottom() - radius}};
    const f32 start[4] = {PI, PI * 1.5f, 0.0f, PI * 0.5f};
    const color mid = col_at(r.center_x(), r.center_y());
    v[0] = {r.center_x(), r.center_y(), 0.0f, 0.0f, mid};
    i32 n = 1;
    for (i32 c = 0; c < 4; ++c)
    {
        for (i32 s = 0; s <= SEG; ++s)
        {
            const f32 a = start[c] + (PI * 0.5f) * static_cast<f32>(s) / static_cast<f32>(SEG);
            const f32 x = centers[c].x + std::cos(a) * radius;
            const f32 y = centers[c].y + std::sin(a) * radius;
            v[n++] = {x, y, 0.0f, 0.0f, col_at(x, y)};
        }
    }
    i32 k = 0;
    for (i32 i = 0; i < RING; ++i)
    {
        idx[k++] = 0;
        idx[k++] = 1 + i;
        idx[k++] = 1 + (i + 1) % RING;
    }
    u.draw_rounded_rect(r, mid, radius);
    u.draw_triangles(nullptr, v, n, idx, k);
}

// An anti-aliased polyline: a triangle strip with a feathered rim on both sides
// (one draw call, smooth joints, unlike chained draw_line segments).
static void stroke(ui &u, const vec2 *p, i32 n, color c, f32 th)
{
    constexpr i32 MAXP = 160;
    if (n < 2) return;
    n = n > MAXP ? MAXP : n;
    vertex v[MAXP * 4];
    i32 idx[(MAXP - 1) * 18];
    const f32 hw = th * 0.5f, feather = 0.9f;
    color clear = c;
    clear.a = 0;
    for (i32 i = 0; i < n; ++i)
    {
        vec2 d{};
        if (i == 0)
            d = {p[1].x - p[0].x, p[1].y - p[0].y};
        else if (i == n - 1)
            d = {p[n - 1].x - p[n - 2].x, p[n - 1].y - p[n - 2].y};
        else
        {
            vec2 a{p[i].x - p[i - 1].x, p[i].y - p[i - 1].y};
            vec2 b{p[i + 1].x - p[i].x, p[i + 1].y - p[i].y};
            const f32 la = max2(std::sqrt(a.x * a.x + a.y * a.y), 1e-4f);
            const f32 lb = max2(std::sqrt(b.x * b.x + b.y * b.y), 1e-4f);
            d = {a.x / la + b.x / lb, a.y / la + b.y / lb};
        }
        const f32 len = max2(std::sqrt(d.x * d.x + d.y * d.y), 1e-4f);
        const f32 nx = -d.y / len, ny = d.x / len;
        const f32 offs[4] = {-(hw + feather), -hw, hw, hw + feather};
        for (i32 k = 0; k < 4; ++k)
            v[i * 4 + k] = {p[i].x + nx * offs[k], p[i].y + ny * offs[k], 0.0f, 0.0f,
                            (k == 0 || k == 3) ? clear : c};
    }
    i32 m = 0;
    for (i32 i = 0; i + 1 < n; ++i)
    {
        for (i32 b = 0; b < 3; ++b)
        {
            const i32 a0 = i * 4 + b, a1 = i * 4 + b + 1, b0 = (i + 1) * 4 + b,
                      b1 = (i + 1) * 4 + b + 1;
            idx[m++] = a0;
            idx[m++] = a1;
            idx[m++] = b1;
            idx[m++] = a0;
            idx[m++] = b1;
            idx[m++] = b0;
        }
    }
    u.draw_triangles(nullptr, v, n * 4, idx, m);
}

// The area between a polyline and a baseline, fading from `top` to `bottom`.
static void area_under(ui &u, const vec2 *p, i32 n, f32 base_y, color top, color bottom)
{
    constexpr i32 MAXP = 160;
    if (n < 2) return;
    n = n > MAXP ? MAXP : n;
    vertex v[MAXP * 2];
    i32 idx[(MAXP - 1) * 6];
    for (i32 i = 0; i < n; ++i)
    {
        v[i * 2 + 0] = {p[i].x, p[i].y, 0.0f, 0.0f, top};
        v[i * 2 + 1] = {p[i].x, base_y, 0.0f, 0.0f, bottom};
    }
    i32 m = 0;
    for (i32 i = 0; i + 1 < n; ++i)
    {
        idx[m++] = i * 2;
        idx[m++] = i * 2 + 2;
        idx[m++] = i * 2 + 1;
        idx[m++] = i * 2 + 2;
        idx[m++] = i * 2 + 3;
        idx[m++] = i * 2 + 1;
    }
    u.draw_triangles(nullptr, v, n * 2, idx, m);
}

// A rounded-rect outline (focus rings, selection rings, drop placeholders).
// The outline of a rounded rect, clockwise on screen (y down), SEG segments per
// corner. Returns the point count (4 * (SEG + 1)).
static constexpr i32 ROUND_SEG = 8;
static i32 rounded_points(rect r, f32 radius, vec2 *out)
{
    radius = clampf(radius, 0.0f, min2(r.w, r.h) * 0.5f);
    const vec2 centers[4] = {{r.x + radius, r.y + radius},
                             {r.right() - radius, r.y + radius},
                             {r.right() - radius, r.bottom() - radius},
                             {r.x + radius, r.bottom() - radius}};
    const f32 start[4] = {PI, PI * 1.5f, 0.0f, PI * 0.5f};
    i32 n = 0;
    for (i32 c = 0; c < 4; ++c)
        for (i32 i = 0; i <= ROUND_SEG; ++i)
        {
            const f32 a = start[c] + (PI * 0.5f) * static_cast<f32>(i) / ROUND_SEG;
            out[n++] = {centers[c].x + std::cos(a) * radius, centers[c].y + std::sin(a) * radius};
        }
    return n;
}

static void outline(ui &u, rect r, f32 radius, color c, f32 th)
{
    vec2 pts[4 * (ROUND_SEG + 1) + 1];
    i32 n = rounded_points(r, radius, pts);
    pts[n++] = pts[0];
    stroke(u, pts, n, c, th);
}

// A soft drop shadow: a few widening, fading rounded rects.
static void soft_shadow(ui &u, rect r, f32 radius, f32 spread, f32 strength, f32 dy)
{
    if (strength <= 0.01f) return;
    const color base = mixc(color{0, 0, 8, 255}, color{50, 50, 110, 255}, g_app.light);
    for (i32 i = 4; i >= 1; --i)
    {
        const f32 k = static_cast<f32>(i) / 4.0f;
        color c = base;
        c.a = static_cast<u8>(clampf(strength * 34.0f * (1.15f - k), 0.0f, 255.0f));
        u.draw_rounded_rect(grow(shifted(r, 0.0f, dy * k), spread * k), c, radius + spread * k);
    }
}

// Vector icons drawn from lines/arcs/polygons, rotatable (gear spin, bell wiggle).
static void icon(ui &u, i32 kind, vec2 c, f32 s, color col, f32 angle = 0.0f)
{
    const f32 ca = std::cos(angle), sa = std::sin(angle);
    const auto P = [&](f32 x, f32 y)
    { return vec2{c.x + (x * ca - y * sa) * s, c.y + (x * sa + y * ca) * s}; };
    const f32 th = max2(1.5f, s * 0.095f);
    const auto L = [&](f32 x0, f32 y0, f32 x1, f32 y1, f32 w)
    {
        const vec2 a = P(x0, y0), b = P(x1, y1);
        u.draw_line(a.x, a.y, b.x, b.y, col, w);
    };
    switch (kind)
    {
    case ICON_GRID:
        for (i32 i = 0; i < 4; ++i)
        {
            const f32 x = (i % 2) ? 0.06f : -0.42f, y = (i / 2) ? 0.06f : -0.42f;
            u.draw_rounded_rect(rect::make(c.x + x * s, c.y + y * s, 0.36f * s, 0.36f * s), col,
                                0.1f * s);
        }
        break;
    case ICON_LAYERS:
        L(-0.45f, -0.12f, 0.0f, -0.36f, th);
        L(0.0f, -0.36f, 0.45f, -0.12f, th);
        L(0.45f, -0.12f, 0.0f, 0.12f, th);
        L(0.0f, 0.12f, -0.45f, -0.12f, th);
        L(-0.45f, 0.1f, 0.0f, 0.34f, th);
        L(0.0f, 0.34f, 0.45f, 0.1f, th);
        break;
    case ICON_BOARD:
    {
        const f32 xs[3] = {-0.44f, -0.13f, 0.18f}, hs[3] = {0.84f, 0.56f, 0.7f};
        for (i32 i = 0; i < 3; ++i)
            u.draw_rounded_rect(rect::make(c.x + xs[i] * s, c.y - 0.42f * s, 0.26f * s, hs[i] * s),
                                col, 0.07f * s);
        break;
    }
    case ICON_SPARK:
    {
        const vec2 a[4] = {P(0.0f, -0.5f), P(0.13f, 0.0f), P(0.0f, 0.5f), P(-0.13f, 0.0f)};
        const vec2 b[4] = {P(-0.5f, 0.0f), P(0.0f, -0.13f), P(0.5f, 0.0f), P(0.0f, 0.13f)};
        u.draw_polygon(a, col);
        u.draw_polygon(b, col);
        break;
    }
    case ICON_WAVE:
    {
        vec2 pts[14];
        for (i32 i = 0; i < 14; ++i)
        {
            const f32 x = -0.46f + 0.92f * static_cast<f32>(i) / 13.0f;
            pts[i] = P(x, -0.2f * std::sin(x * 7.0f));
        }
        stroke(u, pts, 14, col, th);
        break;
    }
    case ICON_GEAR:
        u.draw_arc(c, 0.24f * s, 0.13f * s, 0.0f, 2.0f * PI, col);
        for (i32 i = 0; i < 8; ++i)
        {
            const f32 a = static_cast<f32>(i) * PI * 0.25f;
            L(std::cos(a) * 0.28f, std::sin(a) * 0.28f, std::cos(a) * 0.46f, std::sin(a) * 0.46f,
              0.15f * s);
        }
        break;
    case ICON_BELL:
    {
        const vec2 top = P(0.0f, -0.08f);
        u.draw_arc(top, 0.26f * s, th, PI + angle, 2.0f * PI + angle, col);
        L(-0.26f, -0.08f, -0.32f, 0.24f, th);
        L(0.26f, -0.08f, 0.32f, 0.24f, th);
        L(-0.42f, 0.25f, 0.42f, 0.25f, th);
        fill_circle(u, P(0.0f, 0.38f), 0.08f * s, col);
        fill_circle(u, P(0.0f, -0.4f), 0.05f * s, col);
        break;
    }
    case ICON_SEARCH:
        u.draw_arc(P(-0.08f, -0.08f), 0.26f * s, th, 0.0f, 2.0f * PI, col);
        L(0.12f, 0.12f, 0.42f, 0.42f, th * 1.3f);
        break;
    case ICON_PLUS:
        L(-0.36f, 0.0f, 0.36f, 0.0f, th);
        L(0.0f, -0.36f, 0.0f, 0.36f, th);
        break;
    case ICON_MINUS:
        L(-0.36f, 0.0f, 0.36f, 0.0f, th);
        break;
    case ICON_CHEVRON:
        L(0.12f, -0.3f, -0.16f, 0.0f, th);
        L(-0.16f, 0.0f, 0.12f, 0.3f, th);
        break;
    case ICON_ROCKET:
    {
        const vec2 body[5] = {P(0.0f, -0.5f), P(0.17f, -0.15f), P(0.17f, 0.22f), P(-0.17f, 0.22f),
                              P(-0.17f, -0.15f)};
        const vec2 fin_l[3] = {P(-0.17f, 0.02f), P(-0.17f, 0.22f), P(-0.36f, 0.32f)};
        const vec2 fin_r[3] = {P(0.17f, 0.02f), P(0.36f, 0.32f), P(0.17f, 0.22f)};
        const vec2 flame[3] = {P(-0.1f, 0.26f), P(0.1f, 0.26f), P(0.0f, 0.5f)};
        u.draw_polygon(body, col);
        u.draw_polygon(fin_l, col);
        u.draw_polygon(fin_r, col);
        u.draw_polygon(flame, color{255, 170, 60, col.a});
        break;
    }
    case ICON_SHUFFLE:
        L(-0.42f, -0.24f, -0.12f, -0.24f, th);
        L(-0.12f, -0.24f, 0.18f, 0.24f, th);
        L(0.18f, 0.24f, 0.42f, 0.24f, th);
        L(-0.42f, 0.24f, -0.12f, 0.24f, th);
        L(-0.12f, 0.24f, 0.18f, -0.24f, th);
        L(0.18f, -0.24f, 0.42f, -0.24f, th);
        L(0.3f, -0.36f, 0.42f, -0.24f, th);
        L(0.3f, -0.12f, 0.42f, -0.24f, th);
        L(0.3f, 0.12f, 0.42f, 0.24f, th);
        L(0.3f, 0.36f, 0.42f, 0.24f, th);
        break;
    case ICON_SORT:
        L(-0.4f, -0.28f, 0.4f, -0.28f, th);
        L(-0.4f, 0.0f, 0.16f, 0.0f, th);
        L(-0.4f, 0.28f, -0.06f, 0.28f, th);
        break;
    case ICON_CHECK:
        L(-0.32f, 0.02f, -0.08f, 0.26f, th * 1.2f);
        L(-0.08f, 0.26f, 0.36f, -0.24f, th * 1.2f);
        break;
    default:
        break;
    }
}

// ============================================================ glass
// The frosted surface every card uses: blur what is behind (intersected with the
// current clip - u.blur composites straight to the scene, so a card scrolled
// half out of its viewport must not blur outside it), a tint, a sheen and the
// light-facing rim from the tutorial's glass.h. `strength` fades all of it.
static void glass_panel(ui &u, rect r, f32 radius, f32 strength = 1.0f, f32 blur = 22.0f)
{
    if (r.w < 3.0f || r.h < 3.0f || strength <= 0.01f) return;
    const f32 L = g_app.light;
    const rect clip_r = u.ctx->clip_depth > 0 ? u.ctx->clip_stack[u.ctx->clip_depth - 1] : r;
    const rect vis = rect::intersect(r, clip_r);
    if (vis.w < 3.0f || vis.h < 3.0f) return;
    // a corner that the clip cuts away is square in the blurred patch
    corner_radii rr = corner_radii::all(radius);
    if (vis.y > r.y + 0.5f) rr.tl = rr.tr = 0.0f;
    if (vis.bottom() < r.bottom() - 0.5f) rr.bl = rr.br = 0.0f;
    if (vis.x > r.x + 0.5f) rr.tl = rr.bl = 0.0f;
    if (vis.right() < r.right() - 0.5f) rr.tr = rr.br = 0.0f;
    u.blur(vis, blur, rr, strength);

    u.draw_rounded_rect(
        r, fade(mixc(color{255, 255, 255, 20}, color{255, 255, 255, 150}, L), strength), radius);
    // sheen: a white-to-clear band over the top, fading toward the sides
    {
        const f32 inset = radius * 0.6f;
        const rect sh = rect::make(r.x + inset, r.y + 1.0f, max2(0.0f, r.w - inset * 2.0f),
                                   min2(r.h * 0.45f, 120.0f));
        const f32 cols[4] = {0.0f, 0.22f, 0.78f, 1.0f};
        const f32 amp[4] = {0.0f, 1.0f, 1.0f, 0.0f};
        const f32 k = (0.16f + 0.1f * L) * strength;
        vertex v[8];
        for (i32 i = 0; i < 4; ++i)
        {
            const f32 x = sh.x + sh.w * cols[i];
            v[i] = {x, sh.y, 0.0f, 0.0f, fade(color{255, 255, 255, 255}, k * amp[i])};
            v[4 + i] = {x, sh.bottom(), 0.0f, 0.0f, color{255, 255, 255, 0}};
        }
        i32 idx[18];
        for (i32 i = 0; i < 3; ++i)
        {
            idx[i * 6 + 0] = i;
            idx[i * 6 + 1] = i + 1;
            idx[i * 6 + 2] = 4 + i;
            idx[i * 6 + 3] = i + 1;
            idx[i * 6 + 4] = 5 + i;
            idx[i * 6 + 5] = 4 + i;
        }
        u.draw_triangles(nullptr, v, 8, idx, 18);
    }
    glass_style gs;
    gs.radii = corner_radii::all(radius);
    gs.rim_light = fade(mixc(color{255, 255, 255, 170}, color{255, 255, 255, 255}, L), strength);
    gs.rim_dark = fade(mixc(color{255, 255, 255, 24}, color{90, 100, 160, 70}, L), strength);
    glass_rim(u, r, gs);
}

// ============================================================ theme
// Two themes (a dark nebula and a light dawn) cross-faded with theme_lerp, then
// the animated accent and the animated density are written over the result.
static theme make_theme(const showcase &s, f32 light, color accent, f32 density)
{
    theme d = glass_theme(s.font);
    d.text_size = 15.0f;
    d.radius = 10.0f;
    d.bg = {10, 10, 26, 255};
    d.text = {246, 246, 255, 255};
    d.text_dim = {176, 180, 214, 255};
    d.caret = d.text;
    d.panel_bg = {255, 255, 255, 12};
    d.button.radius = 12.0f;
    d.accordion.header_bg = {255, 255, 255, 16};
    d.accordion.header_hover = {255, 255, 255, 32};
    d.accordion.text = d.text;
    d.accordion.chevron = d.text_dim;
    d.accordion.radius = 12.0f;
    d.accordion.header_h = 36.0f;
    d.drawer.bg = {0, 0, 0, 0}; // the settings drawer paints its own glass
    d.drawer.border = {0, 0, 0, 0};
    d.drawer.scrim = {0, 0, 0, 0};
    d.toast.bg = {28, 26, 58, 240};
    d.toast.text = d.text;
    d.toast.radius = 14.0f;
    d.toast.width = 300.0f;
    d.toast.height = 48.0f;
    d.palette.bg = {26, 24, 54, 250};
    d.palette.border = {255, 255, 255, 40};
    d.palette.text = d.text;
    d.palette.hint = d.text_dim;
    d.palette.selected = {255, 255, 255, 26};
    d.palette.scrim = {6, 6, 20, 80};
    d.palette.radius = 16.0f;
    d.palette.width = 480.0f;
    d.palette.item_h = 34.0f;
    d.switch_ctrl.track_off = {255, 255, 255, 40};
    d.radio.ring = {255, 255, 255, 120};
    d.segmented.bg = {0, 0, 0, 50};
    d.segmented.text = {255, 255, 255, 200};
    d.segmented.hover = {255, 255, 255, 24};

    theme l = d;
    l.bg = {236, 238, 252, 255};
    l.text = {26, 28, 52, 255};
    l.text_dim = {92, 98, 132, 255};
    l.caret = l.text;
    l.widget_bg = {255, 255, 255, 150};
    l.widget_hover = {255, 255, 255, 205};
    l.widget_active = {255, 255, 255, 110};
    l.border = {40, 50, 100, 46};
    l.panel_bg = {255, 255, 255, 110};
    l.button.bg = {255, 255, 255, 150};
    l.button.hover_bg = {255, 255, 255, 215};
    l.button.active_bg = {255, 255, 255, 110};
    l.button.border = {40, 50, 100, 40};
    l.button.text = l.text;
    l.scrollbar.thumb = {40, 50, 100, 70};
    l.scrollbar.thumb_hover = {40, 50, 100, 110};
    l.segmented.bg = {255, 255, 255, 130};
    l.segmented.text = l.text;
    l.segmented.hover = {0, 0, 0, 14};
    l.accordion.header_bg = {255, 255, 255, 140};
    l.accordion.header_hover = {255, 255, 255, 205};
    l.accordion.text = l.text;
    l.accordion.chevron = l.text_dim;
    l.toast.bg = {255, 255, 255, 245};
    l.toast.text = l.text;
    l.palette.bg = {250, 250, 255, 250};
    l.palette.border = {40, 50, 100, 40};
    l.palette.text = l.text;
    l.palette.hint = l.text_dim;
    l.palette.selected = {40, 50, 100, 20};
    l.palette.scrim = {40, 40, 80, 40};
    l.switch_ctrl.track_off = {40, 50, 100, 50};
    l.radio.ring = {40, 50, 100, 120};

    theme t = theme_lerp(d, l, light);
    const color hover = theme_lerp_color(accent, color{255, 255, 255, 255}, 0.22f);
    t.accent = accent;
    t.accent_hover = hover;
    t.focus_border = hover;
    t.tokens.primary = accent;
    t.tokens.primary_hover = hover;
    t.switch_ctrl.track_on = accent;
    t.radio.fill = accent;
    t.tabs.underline = accent;
    t.palette.accent = accent;
    t.toast.info = accent;
    t.segmented.selected = accent;
    t.segmented.text_selected = {255, 255, 255, 255};
    t.scrollbar.thumb_active = accent;
    t.selection = fade(accent, 0.45f);
    set_button_role(
        t, "primary"_id,
        button_override{.bg = some(accent),
                        .hover_bg = some(hover),
                        .active_bg = some(theme_lerp_color(accent, color::black(), 0.2f)),
                        .border = some(color{255, 255, 255, 0}),
                        .text = some(color::white())});
    // density: the control rhythm itself animates, so every row re-flows smoothly
    t.control_h = 26.0f + 4.0f * density;
    t.control_h_small = 20.0f + 3.0f * density;
    t.spacing = 6.0f + 2.0f * density;
    t.padding = 11.0f + 3.0f * density;
    t.button.pad_x = 10.0f + 3.0f * density;
    return t;
}

// ============================================================ app actions
static void notify(ui &u, showcase &s, const char *text, u32 kind)
{
    s.toasts.push(text, kind);
    for (i32 i = 5; i > 0; --i)
    {
        std::memcpy(s.history[i], s.history[i - 1], sizeof(s.history[i]));
        s.history_at[i] = s.history_at[i - 1];
    }
    std::snprintf(s.history[0], sizeof(s.history[0]), "%s", text);
    s.history_at[0] = u.ctx->now;
    if (s.history_count < 6) s.history_count += 1;
    s.unread += 1;
    s.badge_serial += 1;
    s.bell_at = u.ctx->now;
}

static void set_page(showcase &s, i32 p, f64 now)
{
    p = (p % PAGE_COUNT + PAGE_COUNT) % PAGE_COUNT;
    if (p == s.page) return;
    s.nav_dir = p > s.page ? 1.0f : -1.0f;
    s.prev_page = s.page;
    s.page = p;
    s.page_serial += 1;
    s.page_since = now;
}

static void burst(showcase &s, vec2 at, i32 n, f32 power)
{
    if (s.reduced_motion) return;
    for (i32 i = 0; i < n && s.particles.size() < 1200; ++i)
    {
        const f32 a = -PI * 0.5f + (frand(s) - 0.5f) * PI * 1.7f;
        const f32 sp = power * (0.3f + 0.7f * frand(s));
        particle p;
        p.x = at.x;
        p.y = at.y;
        p.vx = std::cos(a) * sp;
        p.vy = std::sin(a) * sp;
        p.rot = frand(s) * 2.0f * PI;
        p.vrot = (frand(s) - 0.5f) * 16.0f;
        p.ttl = 1.1f + frand(s) * 1.0f;
        p.size = 4.0f + frand(s) * 5.0f;
        p.c = hsv(frand(s), 0.62f, 1.0f);
        s.particles.push_back(p);
    }
}

static void bars_retarget(showcase &s, f64 now)
{
    for (i32 i = 0; i < 12; ++i)
    {
        s.bars_from[i] = s.bars_to[i];
        s.bars_to[i] = 0.12f + 0.84f * hash01(static_cast<u32>(i * 31 + s.bar_range * 977) +
                                              s.bar_seed * 7919u);
    }
    s.bars_at = now;
}

static void randomize_metrics(showcase &s, f64 now)
{
    for (i32 i = 0; i < 4; ++i)
    {
        const f32 f = 0.85f + 0.3f * frand(s);
        s.kpi_delta[i] = (f - 1.0f) * 100.0f;
        s.kpi[i] = i == 3 ? clampf(99.5f + frand(s) * 0.49f, 0.0f, 99.99f) : s.kpi[i] * f;
    }
    for (f32 &gv : s.gauges) gv = 0.15f + 0.8f * frand(s);
    s.bar_seed += 1;
    bars_retarget(s, now);
}

static void add_tile(showcase &s)
{
    lab_tile t;
    t.id = s.next_tile++;
    t.hue = frand(s);
    t.tall = frand(s);
    const usize at =
        s.tiles.empty() ? 0 : static_cast<usize>(frand(s) * static_cast<f32>(s.tiles.size()));
    s.tiles.insert(s.tiles.begin() + static_cast<std::ptrdiff_t>(at), t);
}

static void shuffle_tiles(showcase &s)
{
    for (usize i = s.tiles.size(); i > 1; --i)
    {
        const usize j = static_cast<usize>(frand(s) * static_cast<f32>(i)) % i;
        std::swap(s.tiles[i - 1], s.tiles[j]);
    }
}

static void add_card(showcase &s, const char *title, i32 col)
{
    board_card c;
    c.id = s.next_card++;
    c.col = col;
    c.tag = static_cast<i32>(frand(s) * 4.0f) % 4;
    c.progress = col == 2 ? 1.0f : 0.1f + 0.7f * frand(s);
    std::snprintf(c.title, sizeof(c.title), "%s", title);
    // insert after the column's last card so it lands at the bottom of its column
    usize pos = s.cards.size();
    for (usize i = 0; i < s.cards.size(); ++i)
        if (s.cards[i].col == col) pos = i + 1;
    s.cards.insert(s.cards.begin() + static_cast<std::ptrdiff_t>(pos), c);
}

static void seed(showcase &s)
{
    for (i32 i = 0; i < 14; ++i)
    {
        lab_tile t;
        t.id = s.next_tile++;
        t.hue = static_cast<f32>(i) / 14.0f;
        t.tall = hash01(static_cast<u32>(i) * 13u + 5u);
        s.tiles.push_back(t);
    }
    const char *const titles[8] = {
        "Telemetry dashboard", "Night-mode star map", "Docking checklist", "Thruster calibration",
        "Crew rota v2",        "Orbit planner",       "Launch countdown",  "Fuel gauge widget"};
    const i32 cols[8] = {0, 0, 0, 1, 1, 1, 2, 2};
    for (i32 i = 0; i < 8; ++i) add_card(s, titles[i], cols[i]);
    for (i32 i = 0; i < 4; ++i)
    {
        feed_event e;
        e.serial = s.feed_serial++;
        e.what = i;
        e.at = -static_cast<f64>(i) * 40.0;
        s.feed.insert(s.feed.begin(), e);
    }
    bars_retarget(s, 0.0);
    for (i32 i = 0; i < 12; ++i) s.bars_from[i] = s.bars_to[i];
}

// ============================================================ shared widgets
static bool kb_hit(ui &u, const interaction &in)
{
    if (!in.focused) return false;
    if (u.key_pressed(key::ENTER) || u.key_pressed(key::SPACE))
    {
        u.consume_key(key::ENTER);
        u.consume_key(key::SPACE);
        return true;
    }
    return false;
}

// Sutherland-Hodgman: clips the convex polygon `poly` to the convex, clockwise
// polygon `clip`. Writes at most `cap` points to `out`; returns the count.
static i32 clip_convex(const vec2 *poly, i32 n, const vec2 *clip, i32 m, vec2 *out, i32 cap)
{
    constexpr i32 MAXP = 128;
    vec2 a_buf[MAXP], b_buf[MAXP];
    vec2 *cur = a_buf, *nxt = b_buf;
    i32 count = n < MAXP ? n : MAXP;
    for (i32 i = 0; i < count; ++i) cur[i] = poly[i];
    for (i32 e = 0; e < m && count > 0; ++e)
    {
        const vec2 a = clip[e], b = clip[(e + 1) % m];
        const f32 ex = b.x - a.x, ey = b.y - a.y;
        if (ex * ex + ey * ey < 1e-8f) continue; // a repeated point: no edge
        // inside = on the right of a->b in screen space (clockwise interior)
        const auto side = [&](vec2 p) { return ex * (p.y - a.y) - ey * (p.x - a.x); };
        i32 k = 0;
        for (i32 i = 0; i < count && k < MAXP - 1; ++i)
        {
            const vec2 p = cur[i], q = cur[(i + 1) % count];
            const f32 sp = side(p), sq = side(q);
            if (sp >= 0.0f) nxt[k++] = p;
            if ((sp >= 0.0f) != (sq >= 0.0f))
            {
                const f32 t = sp / (sp - sq);
                nxt[k++] = {p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t};
            }
        }
        count = k;
        vec2 *tmp = cur;
        cur = nxt;
        nxt = tmp;
    }
    if (count > cap) count = cap;
    for (i32 i = 0; i < count; ++i) out[i] = cur[i];
    return count;
}

// Each ripple is a disc clipped to the button's own rounded shape (both are
// convex, so the intersection is one convex polygon draw_polygon can fill; a
// rectangular clip region would let the disc spill into the square corners).
static void draw_ripples(ui &u, showcase &s, uiid owner, rect b, f32 radius)
{
    const f64 now = u.ctx->now;
    const f32 reach = max2(b.w, b.h) * 1.15f;
    vec2 shape[4 * (ROUND_SEG + 1)];
    i32 shape_n = 0;
    for (const ripple &rp : s.ripples)
    {
        if (rp.owner != owner) continue;
        const f32 k = static_cast<f32>((now - rp.born) / 0.6);
        if (k >= 1.0f) continue;
        if (shape_n == 0) shape_n = rounded_points(b, radius, shape);
        constexpr i32 DISC = 40;
        const vec2 c{b.x + rp.p.x, b.y + rp.p.y};
        const f32 rr = 4.0f + reach * ease(easing::EASE_OUT, k);
        vec2 disc[DISC];
        for (i32 i = 0; i < DISC; ++i)
        {
            const f32 a = 2.0f * PI * static_cast<f32>(i) / DISC;
            disc[i] = {c.x + std::cos(a) * rr, c.y + std::sin(a) * rr};
        }
        vec2 piece[128];
        const i32 n = clip_convex(disc, DISC, shape, shape_n, piece, 128);
        if (n >= 3)
            u.draw_polygon(std::span<const vec2>(piece, static_cast<usize>(n)),
                           fade(color::white(), 0.28f * (1.0f - k)));
    }
}

struct pill_opts
{
    i32 icon = -1;
    bool primary = false;
};

// A pill button: hover lift + glow, a springy press squish, a ripple from the
// press point (clipped by a region), and a focus ring. Keyboard: Enter/Space.
static bool pill_button(ui &u, rect r, const char *label, uiid id, const pill_opts &o = {})
{
    showcase &s = g_app;
    if (r.w < 4.0f || r.h < 4.0f) return false;
    const theme &th = u.th();
    const interaction in = u.interact(id, r, !s.blocked);
    if (in.hovered) u.set_cursor(CURSOR_HAND);
    const bool hit = in.clicked || kb_hit(u, in);
    // ripples start at the press point (the center for a keyboard press), stored
    // relative to the button so they stay put while the page scrolls
    if (in.activated)
        s.ripples.push_back(ripple{id, {u.ctx->mouse_x - r.x, u.ctx->mouse_y - r.y}, u.ctx->now});
    if (hit && !in.clicked) s.ripples.push_back(ripple{id, {r.w * 0.5f, r.h * 0.5f}, u.ctx->now});
    const f32 hv = u.smooth(id_child(id, "hv"_id), in.hovered ? 1.0f : 0.0f, 0.06f);
    const f32 pr =
        u.animate(id_child(id, "pr"_id), in.pressed ? 1.0f : 0.0f, spring{700.0f, 0.55f});

    rect b = grow(r, -1.6f * clampf(pr, -0.2f, 1.3f));
    b.y -= hv * 1.5f;
    const f32 rad = b.h * 0.5f;
    if (o.primary)
    {
        glass_glow(u, {b.center_x(), b.center_y() + 6.0f}, b.w * 0.62f,
                   fade(s.accent_c, 0.22f + 0.22f * hv));
        const color a = mixc(s.accent_c, color::white(), 0.18f + 0.12f * hv);
        const color z = mixc(s.accent_c, color{40, 20, 120, 255}, 0.18f);
        gradient_round(u, b, rad, a, mixc(a, color{255, 120, 200, 255}, 0.25f), z, z);
    }
    else
    {
        const color bg = mixc(color{255, 255, 255, 22}, color{255, 255, 255, 150}, s.light);
        const color bh = mixc(color{255, 255, 255, 44}, color{255, 255, 255, 220}, s.light);
        u.draw_rounded_rect(b, mixc(bg, bh, hv), rad);
        outline(u, b, rad, fade(th.text, 0.12f + 0.1f * hv), 1.0f);
    }
    draw_ripples(u, s, id, b, rad);
    const color tc = o.primary ? color::white() : th.text;
    const f32 tw = u.text_width(label);
    const f32 iw = o.icon >= 0 ? 22.0f : 0.0f;
    const f32 x0 = b.center_x() - (tw + iw) * 0.5f;
    if (o.icon >= 0) icon(u, o.icon, {x0 + 8.0f, b.center_y()}, 16.0f, tc);
    u.text(rect::make(x0 + iw, b.y, tw + 2.0f, b.h), label, tc, ALIGN_LEFT);
    if (in.focused) outline(u, grow(b, 3.0f), rad + 3.0f, th.focus_border, 1.5f);
    return hit;
}

// A round icon button (top bar, sidebar, card headers).
static bool icon_button(ui &u, rect r, i32 kind, uiid id, const char *tip, f32 angle = 0.0f)
{
    showcase &s = g_app;
    const interaction in = u.interact(id, r, !s.blocked);
    if (in.hovered) u.set_cursor(CURSOR_HAND);
    const f32 hv = u.smooth(id_child(id, "hv"_id), in.hovered ? 1.0f : 0.0f, 0.06f);
    const f32 pr =
        u.animate(id_child(id, "pr"_id), in.pressed ? 1.0f : 0.0f, spring{700.0f, 0.55f});
    const vec2 c{r.center_x(), r.center_y()};
    const f32 rad = min2(r.w, r.h) * 0.5f - 1.0f - 2.0f * clampf(pr, 0.0f, 1.2f);
    fill_circle(u, c, rad, fade(u.th().text, 0.04f + 0.1f * hv));
    icon(u, kind, c, 18.0f + 1.5f * hv, u.th().text, angle);
    if (in.focused) u.draw_arc(c, rad + 3.0f, 1.5f, 0.0f, 2.0f * PI, u.th().focus_border);
    if (tip) (void)u.tooltip(r, id_child(id, "tip"_id), tip);
    return in.clicked || kb_hit(u, in);
}

// A slider with a label line, a gradient fill, a springy knob and a value bubble
// that pops up while dragging. Keyboard: Left/Right (Shift = fine).
static bool fancy_slider(ui &u, rect r, const char *label, f32 &value, f32 lo, f32 hi, uiid id,
                         const char *fmt)
{
    showcase &s = g_app;
    const theme &th = u.th();
    rect body = r;
    const rect head = body.cut_top(18.0f);
    const interaction in = u.interact(id, body, !s.blocked);
    if (in.hovered || in.pressed) u.set_cursor(CURSOR_HRESIZE);
    const rect track =
        rect::make(body.x + 9.0f, body.center_y() - 3.0f, max2(0.0f, body.w - 18.0f), 6.0f);
    const f32 range = max2(hi - lo, 1e-4f);
    const f32 before = value;
    if (in.focused && (u.key_pressed(key::LEFT) || u.key_pressed(key::RIGHT)))
    {
        const f32 step = range * (u.ctx->shift_down ? 0.01f : 0.05f);
        value += u.key_pressed(key::RIGHT) ? step : -step;
        u.consume_key(key::LEFT);
        u.consume_key(key::RIGHT);
    }
    if ((in.activated || in.pressed) && track.w > 0.0f)
        value = lo + (u.ctx->mouse_x - track.x) / track.w * range;
    value = clampf(value, lo, hi);

    char buf[32];
    std::snprintf(buf, sizeof(buf), fmt, static_cast<double>(value));
    u.text(head, label, th.text_dim, ALIGN_LEFT);
    u.text(head, buf, th.text, ALIGN_RIGHT);

    // the knob eases toward the value (instantly while dragging; presets glide)
    const f32 frac = sat((value - lo) / range);
    const f32 shown = u.smooth(id_child(id, "x"_id), frac, in.pressed ? 0.012f : 0.08f);
    const f32 hv = u.smooth(id_child(id, "hv"_id), (in.hovered || in.focused) ? 1.0f : 0.0f, 0.06f);
    u.draw_rounded_rect(track, fade(th.text, 0.12f), 3.0f);
    const f32 fw = track.w * shown;
    if (fw > 1.0f)
        gradient_round(u, rect::make(track.x, track.y, max2(fw, 6.0f), track.h), 3.0f,
                       fade(s.accent_c, 0.55f), th.accent_hover, th.accent_hover,
                       fade(s.accent_c, 0.55f));
    const vec2 kc{track.x + fw, track.center_y()};
    const f32 kr = u.animate(id_child(id, "kr"_id), in.pressed ? 10.5f : (7.0f + 2.0f * hv),
                             spring{420.0f, 0.5f});
    if (hv > 0.01f) glass_glow(u, kc, 26.0f, fade(s.accent_c, 0.4f * hv));
    fill_circle(u, kc, kr + 1.5f, fade(s.accent_c, 0.9f));
    fill_circle(u, kc, kr, color::white());
    if (in.focused) u.draw_arc(kc, kr + 4.0f, 1.5f, 0.0f, 2.0f * PI, th.focus_border);

    const f32 bubble =
        u.animate(id_child(id, "bb"_id), in.pressed ? 1.0f : 0.0f, spring{380.0f, 0.6f});
    if (bubble > 0.02f)
    {
        const f32 bw = max2(46.0f, u.text_width(buf) + 18.0f);
        const rect br =
            scaled(rect::make(kc.x - bw * 0.5f, kc.y - 42.0f + 8.0f * (1.0f - bubble), bw, 24.0f),
                   clampf(bubble, 0.0f, 1.2f), clampf(bubble, 0.0f, 1.2f));
        u.draw_rounded_rect(br, fade(s.accent_c, sat(bubble)), 12.0f);
        if (bubble > 0.6f) u.text(br, buf, color::white(), ALIGN_CENTER);
    }
    return value != before;
}

// Card chrome: a title line and a dim caption line.
static void card_title(ui &u, rect head, const char *title, const char *caption)
{
    const theme &th = u.th();
    {
        text_scope ts = u.text_style(16.0f, g_app.bold);
        u.text(head.cut_top(22.0f), title, th.text, ALIGN_LEFT);
    }
    if (caption && *caption)
    {
        text_scope ts = u.text_style(13.0f);
        u.text_ellipsis(head.cut_top(18.0f), caption, th.text_dim, ALIGN_LEFT);
    }
}

// Staggered entrance for the i-th card of a page (0 -> 1).
static f32 enter_k(const showcase &s, i32 i)
{
    if (s.reduced_motion || s.transition == 2) return 1.0f;
    return ease(easing::EASE_OUT, sat((s.age - 0.06f * static_cast<f32>(i)) / 0.5f));
}

// A FLIP-animated glass card that slides up as it enters.
static rect card(ui &u, showcase &s, uiid key, rect target, vec2 origin, f32 k, f32 radius = 20.0f)
{
    rect r = u.animate_rect(key, target, {.origin = origin});
    r.y += (1.0f - k) * 28.0f;
    glass_panel(u, r, radius, 0.25f + 0.75f * k);
    return r;
}

// ============================================================ background
static void aurora(ui &u, rect r, f32 t, f32 base, f32 thick, color c, f32 phase)
{
    constexpr i32 N = 48;
    vertex v[N * 3];
    i32 idx[(N - 1) * 12];
    color clear = c;
    clear.a = 0;
    for (i32 i = 0; i < N; ++i)
    {
        const f32 fi = static_cast<f32>(i);
        const f32 x = r.x + r.w * fi / static_cast<f32>(N - 1);
        const f32 y = r.y + r.h * base + std::sin(fi * 0.25f + t * 0.4f + phase) * r.h * 0.05f +
                      std::sin(fi * 0.11f - t * 0.23f + phase * 2.0f) * r.h * 0.06f;
        const f32 a = 0.5f + 0.5f * std::sin(fi * 0.3f + t * 0.7f + phase);
        const f32 th = thick * (0.55f + 0.45f * a);
        v[i * 3 + 0] = {x, y - th, 0.0f, 0.0f, clear};
        v[i * 3 + 1] = {x, y, 0.0f, 0.0f, fade(c, 0.35f + 0.65f * a)};
        v[i * 3 + 2] = {x, y + th * 0.3f, 0.0f, 0.0f, clear};
    }
    i32 m = 0;
    for (i32 i = 0; i + 1 < N; ++i)
        for (i32 b = 0; b < 2; ++b)
        {
            const i32 a0 = i * 3 + b, a1 = a0 + 1, b0 = a0 + 3, b1 = a0 + 4;
            idx[m++] = a0;
            idx[m++] = b0;
            idx[m++] = a1;
            idx[m++] = a1;
            idx[m++] = b0;
            idx[m++] = b1;
        }
    u.draw_triangles(nullptr, v, N * 3, idx, m);
}

static void draw_backdrop(ui &u, showcase &s, rect r)
{
    const f32 L = s.light, t = s.clock;
    gradient_quad(u, r, mixc({16, 12, 44, 255}, {222, 228, 255, 255}, L),
                  mixc({44, 14, 66, 255}, {255, 228, 242, 255}, L),
                  mixc({8, 28, 58, 255}, {212, 242, 255, 255}, L),
                  mixc({6, 8, 26, 255}, {238, 232, 255, 255}, L));
    const f32 m = max2(r.w, r.h);
    glass_glow(u,
               {r.x + r.w * (0.18f + 0.06f * std::sin(t * 0.21f)),
                r.y + r.h * (0.22f + 0.05f * std::cos(t * 0.17f))},
               m * 0.55f, fade(s.accent_c, 0.5f - 0.2f * L));
    glass_glow(u,
               {r.x + r.w * (0.86f + 0.05f * std::sin(t * 0.19f + 1.0f)),
                r.y + r.h * (0.7f + 0.06f * std::sin(t * 0.23f))},
               m * 0.5f,
               fade(mixc({255, 70, 170, 255}, {255, 150, 200, 255}, L), 0.4f - 0.15f * L));
    glass_glow(u, {r.x + r.w * (0.55f + 0.08f * std::sin(t * 0.13f + 2.0f)), r.y + r.h * 1.02f},
               m * 0.45f, fade(color{40, 220, 230, 255}, 0.3f - 0.1f * L));
    // stars, only at night
    if (L < 0.99f)
    {
        for (u32 i = 0; i < 110; ++i)
        {
            const f32 px = r.x + hash01(i * 3u + 1u) * r.w, py = r.y + hash01(i * 3u + 2u) * r.h;
            const f32 tw =
                0.5f + 0.5f * std::sin(t * (0.8f + hash01(i * 7u) * 2.2f) + static_cast<f32>(i));
            const f32 a = (1.0f - L) * (30.0f + 170.0f * tw * hash01(i * 11u));
            const f32 sz = 0.8f + hash01(i * 5u) * 1.5f;
            u.draw_rounded_rect(rect::make(px, py, sz, sz),
                                color{255, 255, 255, static_cast<u8>(a)}, sz * 0.5f);
        }
    }
    aurora(u, r, t, 0.34f, r.h * 0.16f,
           fade(mixc({80, 255, 200, 255}, {255, 255, 255, 255}, L), 0.22f), 0.0f);
    aurora(u, r, t * 0.8f, 0.62f, r.h * 0.12f, fade(s.accent_c, 0.2f), 2.4f);
    // a soft spotlight that follows the pointer
    const f32 mx = u.smooth("bg_mx"_id, u.ctx->mouse_x, 0.12f);
    const f32 my = u.smooth("bg_my"_id, u.ctx->mouse_y, 0.12f);
    glass_glow(u, {mx, my}, 300.0f, fade(s.accent_c, 0.14f + 0.06f * L));
}

// ============================================================ top bar
static void theme_toggle(ui &u, showcase &s, rect r)
{
    const uiid id = "tb_theme"_id;
    const interaction in = u.interact(id, r, !s.blocked);
    if (in.hovered) u.set_cursor(CURSOR_HAND);
    if (in.clicked || kb_hit(u, in)) s.theme_mode ^= 1;
    const f32 k =
        u.animate(id_child(id, "k"_id), static_cast<f32>(s.theme_mode), spring{260.0f, 0.6f});
    const f32 kc = sat(k);
    const rect track = rect::make(r.x, r.center_y() - 15.0f, r.w, 30.0f);
    const color night_a{30, 30, 80, 255}, night_b{70, 34, 100, 255};
    const color day_a{140, 200, 255, 255}, day_b{255, 210, 150, 255};
    gradient_round(u, track, 15.0f, mixc(night_a, day_a, kc), mixc(night_b, day_b, kc),
                   mixc(night_b, day_b, kc), mixc(night_a, day_a, kc));
    for (i32 j = 0; j < 3; ++j) // stars fade out at dawn
        fill_circle(u,
                    {track.right() - 12.0f - static_cast<f32>(j) * 8.0f,
                     track.y + 9.0f + static_cast<f32>(j % 2) * 10.0f},
                    1.3f, fade(color::white(), 1.0f - kc));
    const f32 kr = 11.0f;
    const vec2 c{lerpf(track.x + 4.0f + kr, track.right() - 4.0f - kr, k), track.center_y()};
    glass_glow(u, c, 28.0f, fade(mixc({200, 210, 255, 255}, {255, 200, 80, 255}, kc), 0.5f));
    for (i32 j = 0; j < 8 && kc > 0.01f; ++j) // sun rays grow in
    {
        const f32 a = static_cast<f32>(j) * PI * 0.25f + kc * PI * 0.5f;
        const f32 r0 = kr + 2.0f, r1 = kr + 2.0f + 4.0f * kc;
        u.draw_line(c.x + std::cos(a) * r0, c.y + std::sin(a) * r0, c.x + std::cos(a) * r1,
                    c.y + std::sin(a) * r1, fade(color{255, 230, 140, 255}, kc), 1.6f);
    }
    fill_circle(u, c, kr, mixc(color{236, 238, 255, 255}, color{255, 214, 90, 255}, kc));
    if (kc < 0.98f) // the moon's bite slides off as it becomes the sun
        fill_circle(u, {c.x - kr * 0.45f - kc * kr * 1.6f, c.y - kr * 0.35f},
                    kr * 0.8f * (1.0f - kc * 0.5f), mixc(night_a, day_a, kc));
    if (in.focused) outline(u, grow(track, 3.0f), 18.0f, u.th().focus_border, 1.5f);
    (void)u.tooltip(r, "tb_theme_tip"_id, "Dark / light");
}

static void accent_swatches(ui &u, showcase &s, rect r, uiid base)
{
    row rw(r, 8.0f);
    for (i32 i = 0; i < ACCENT_COUNT; ++i)
    {
        const rect cell = rw.next(18.0f);
        if (cell.w <= 0.0f) break;
        const uiid id = id_child(base, static_cast<uiid>(i));
        const interaction in = u.interact(id, cell, !s.blocked);
        if (in.hovered) u.set_cursor(CURSOR_HAND);
        if (in.clicked || kb_hit(u, in)) s.accent = i;
        const vec2 c{cell.center_x(), cell.center_y()};
        const f32 k =
            u.animate(id_child(id, "k"_id), s.accent == i ? 1.0f : 0.0f, spring{320.0f, 0.45f});
        const f32 hv = u.smooth(id_child(id, "hv"_id), in.hovered ? 1.0f : 0.0f, 0.05f);
        if (k > 0.01f)
            u.draw_arc(c, 9.0f + 3.5f * k, 2.0f, 0.0f, 2.0f * PI, fade(ACCENTS[i], sat(k)));
        fill_circle(u, c, 7.0f + 1.5f * hv, ACCENTS[i]);
        if (in.focused) u.draw_arc(c, 15.0f, 1.2f, 0.0f, 2.0f * PI, u.th().focus_border);
        (void)u.tooltip(cell, id_child(id, "tip"_id), ACCENT_NAMES[i]);
    }
}

static void search_pill(ui &u, showcase &s, rect r, bool compact)
{
    const theme &th = u.th();
    const uiid id = "tb_search"_id;
    const interaction in = u.interact(id, r, !s.blocked);
    if (in.hovered) u.set_cursor(CURSOR_HAND);
    if (in.clicked || kb_hit(u, in))
    {
        s.palette_open = true;
        s.palette = comp::palette_state{}; // a fresh query, highlight and scroll
        s.palette_serial += 1;
    }
    const f32 hv = u.smooth(id_child(id, "hv"_id), in.hovered ? 1.0f : 0.0f, 0.06f);
    const rect b = r.pad(0.0f, (r.h - 36.0f) * 0.5f);
    u.draw_rounded_rect(b, fade(th.text, 0.06f + 0.06f * hv), 18.0f);
    outline(u, b, 18.0f, fade(th.text, 0.1f + 0.15f * hv), 1.0f);
    icon(u, ICON_SEARCH, {b.x + 19.0f, b.center_y()}, 16.0f, th.text_dim);
    if (!compact)
    {
        rect tr = b.pad(36.0f, 0.0f, 10.0f, 0.0f);
        const f32 hint_w = 88.0f;
        if (tr.w > hint_w + 100.0f)
        {
            const rect chip = tr.cut_right(hint_w).pad(0.0f, 7.0f);
            u.draw_rounded_rect(chip, fade(th.text, 0.08f), 8.0f);
            text_scope ts = u.text_style(12.0f);
            u.text(chip, "Ctrl+Space", th.text_dim, ALIGN_CENTER);
        }
        u.text_ellipsis(tr, "Search or jump to...", th.text_dim, ALIGN_LEFT);
    }
    if (in.focused) outline(u, grow(b, 3.0f), 21.0f, th.focus_border, 1.5f);
}

static void draw_topbar(ui &u, showcase &s, rect bar)
{
    const theme &th = u.th();
    glass_panel(u, bar, 18.0f, 1.0f, 28.0f);
    // the page scrolls under the bar: claim presses over it (topmost wins), so
    // nothing beneath activates; the bar's own widgets come later and win theirs
    (void)u.interact("tb_shield"_id, bar, !s.blocked, false);
    const vec2 o{bar.x, bar.y};
    rect in = bar.pad(16.0f, 8.0f);
    const bool narrow = bar.w < 820.0f;

    // right cluster, cut from the right; the pieces FLIP when the bar changes mode
    const rect gear_t = in.cut_right(40.0f);
    (void)in.cut_right(4.0f);
    const rect bell_t = in.cut_right(40.0f);
    (void)in.cut_right(12.0f);
    rect sw_t{};
    if (!narrow)
    {
        sw_t = in.cut_right(5.0f * 18.0f + 4.0f * 8.0f);
        (void)in.cut_right(16.0f);
    }
    const rect tg_t = in.cut_right(64.0f);
    (void)in.cut_right(14.0f);
    const rect sr_t = in.cut_right(narrow ? 40.0f : min2(300.0f, in.w * 0.45f));
    (void)in.cut_right(12.0f);

    const rect gear = u.animate_rect("tb_gear"_id, gear_t, {.origin = o});
    const rect bell = u.animate_rect("tb_bell"_id, bell_t, {.origin = o});
    const rect tg = u.animate_rect("tb_toggle"_id, tg_t, {.origin = o});
    const rect sr = u.animate_rect("tb_search_r"_id, sr_t, {.origin = o});

    // title: the old page's name slides out while the new one slides in
    {
        region rg = u.region(in, "tb_title"_id);
        const f32 k = (s.reduced_motion || s.transition == 2)
                          ? 1.0f
                          : u.appear(id_child("tb_in"_id, static_cast<uiid>(s.page_serial)), 0.45f,
                                     easing::EASE_OUT);
        const f32 dir = s.nav_dir;
        const auto title_at = [&](i32 p, f32 dy, f32 a)
        {
            if (a <= 0.01f) return;
            rect tr = shifted(in, 0.0f, dy);
            {
                text_scope ts = u.text_style(20.0f, s.bold);
                u.text(tr.cut_top(24.0f), PAGE_NAMES[p], fade(th.text, a), ALIGN_LEFT);
            }
            text_scope ts = u.text_style(13.0f);
            u.text_ellipsis(tr.cut_top(16.0f), PAGE_CAPTIONS[p], fade(th.text_dim, a), ALIGN_LEFT);
        };
        if (k < 1.0f && s.prev_page != s.page) title_at(s.prev_page, -k * 22.0f * dir, 1.0f - k);
        title_at(s.page, (1.0f - k) * 22.0f * dir, k);
    }

    search_pill(u, s, sr, narrow);
    theme_toggle(u, s, tg);
    if (!narrow)
    {
        const rect sw = u.animate_rect("tb_swatch"_id, sw_t, {.origin = o});
        accent_swatches(u, s, sw, "tb_sw"_id);
    }

    // bell: wiggles when a notification lands, badge pops with the count
    const f32 since = static_cast<f32>(u.ctx->now - s.bell_at);
    const f32 wiggle = since < 1.4f && !s.reduced_motion
                           ? std::sin(since * 22.0f) * 0.45f * std::exp(-since * 3.5f)
                           : 0.0f;
    if (since < 1.4f) u.request_redraw();
    if (icon_button(u, bell, ICON_BELL, "tb_bell_btn"_id, "Notifications", wiggle))
    {
        s.notes_open = !s.notes_open;
        s.unread = 0;
    }
    s.bell_rect = bell;
    if (s.unread > 0)
    {
        const f32 pop = u.appear(id_child("tb_badge"_id, static_cast<uiid>(s.badge_serial)), 0.4f,
                                 easing::EASE_OUT_BACK);
        const vec2 bc{bell.right() - 9.0f, bell.y + 9.0f};
        fill_circle(u, bc, 8.5f * clampf(pop, 0.0f, 1.3f), color{255, 80, 110, 255});
        if (pop > 0.5f)
        {
            text_scope ts = u.text_style(11.0f, s.bold);
            u.textf(rect::make(bc.x - 9.0f, bc.y - 9.0f, 18.0f, 18.0f), color::white(),
                    ALIGN_CENTER, "%d", s.unread > 9 ? 9 : s.unread);
        }
    }

    // gear: turns with the drawer
    const f32 turn =
        u.animate("tb_gear_turn"_id, s.drawer_open ? PI * 0.75f : 0.0f, spring{140.0f, 0.55f});
    if (icon_button(u, gear, ICON_GEAR, "tb_gear_btn"_id, "Settings", turn))
        s.drawer_open = !s.drawer_open;
}

// ============================================================ sidebar
static void logo(ui &u, showcase &s, vec2 c, f32 r)
{
    const f32 t = s.clock;
    glass_glow(u, c, r * 2.2f, fade(s.accent_c, 0.45f));
    const color cols[3] = {s.accent_c, mixc(s.accent_c, color{255, 110, 200, 255}, 0.6f),
                           mixc(s.accent_c, color{110, 240, 255, 255}, 0.6f)};
    for (i32 i = 0; i < 3; ++i)
    {
        const f32 fi = static_cast<f32>(i);
        const f32 a0 = t * (0.9f + 0.55f * fi) * (i == 1 ? -1.0f : 1.0f) + fi * 2.1f;
        u.draw_arc(c, r * (0.48f + 0.2f * fi), 2.6f, a0, a0 + PI * 1.25f, cols[i]);
    }
    fill_circle(u, c, r * 0.2f * (1.0f + 0.15f * std::sin(t * 3.0f)), color::white());
}

static void draw_sidebar(ui &u, showcase &s, rect area, bool open_target, bool narrow)
{
    const theme &th = u.th();
    const rect panel = area.pad(12.0f, 12.0f, 2.0f, 12.0f);
    glass_panel(u, panel, 22.0f);
    region rg = u.region(panel, "sidebar"_id); // labels clip while the bar is narrow
    const f32 label_k = sat((panel.w - 110.0f) / 70.0f);
    rect in = panel.pad(10.0f);

    // brand
    {
        const rect brand = in.cut_top(48.0f);
        (void)in.cut_top(16.0f);
        logo(u, s, {brand.x + 22.0f, brand.center_y()}, 17.0f);
        if (label_k > 0.01f)
        {
            rect tr = rect::make(brand.x + 52.0f - (1.0f - label_k) * 12.0f, brand.y + 4.0f, 160.0f,
                                 40.0f);
            {
                text_scope ts = u.text_style(18.0f, s.bold);
                u.text(tr.cut_top(22.0f), "Nebula", fade(th.text, label_k), ALIGN_LEFT);
            }
            text_scope ts = u.text_style(12.0f);
            u.text(tr.cut_top(16.0f), "PufferUI showcase", fade(th.text_dim, label_k), ALIGN_LEFT);
        }
    }

    // the active pill: two springs, the leading edge stiffer - it stretches
    // toward the new item and then catches up (a gooey indicator)
    const f32 item_h = 44.0f, gap = 4.0f;
    const f32 top_t = in.y + static_cast<f32>(s.page) * (item_h + gap);
    const bool down = s.nav_dir > 0.0f;
    const f32 top = u.animate("nav_top"_id, top_t, spring{down ? 150.0f : 460.0f, 0.85f});
    const f32 bot = u.animate("nav_bot"_id, top_t + item_h, spring{down ? 460.0f : 150.0f, 0.85f});
    {
        const rect pill = rect::make(in.x, top, in.w, max2(10.0f, bot - top));
        glass_glow(u, {pill.x + 22.0f, pill.center_y()}, 70.0f, fade(s.accent_c, 0.35f));
        gradient_round(u, pill, 13.0f, fade(s.accent_c, 0.55f), fade(s.accent_c, 0.22f),
                       fade(s.accent_c, 0.16f), fade(s.accent_c, 0.42f));
        u.draw_rounded_rect(
            rect::make(pill.x + 1.0f, pill.y + 10.0f, 3.0f, max2(0.0f, pill.h - 20.0f)),
            mixc(s.accent_c, color::white(), 0.4f), 1.5f);
    }

    i32 in_flight = 0;
    for (const board_card &c : s.cards)
        if (c.col == 1 && c.dying < 0.0) in_flight += 1;

    for (i32 i = 0; i < PAGE_COUNT; ++i)
    {
        const rect r = in.cut_top(item_h);
        (void)in.cut_top(gap);
        const uiid nid = id_child("nav"_id, static_cast<uiid>(i));
        const interaction it = u.interact(nid, r, !s.blocked);
        if (it.hovered) u.set_cursor(CURSOR_HAND);
        const f32 hv = u.smooth(id_child(nid, "hv"_id), it.hovered ? 1.0f : 0.0f, 0.06f);
        if (hv > 0.01f) u.draw_rounded_rect(r, fade(th.text, 0.06f * hv), 13.0f);
        const bool active = s.page == i;
        const color ic =
            u.animate_color(id_child(nid, "ic"_id), active ? th.text : th.text_dim, tween{0.25f});
        // the icon nudges right on hover; it centers itself when collapsed
        const f32 ix = lerpf(r.center_x(), r.x + 24.0f, label_k) + 3.0f * hv * label_k;
        icon(u, PAGE_ICONS[i], {ix, r.center_y()}, 18.0f + 2.0f * hv, ic);
        if (label_k > 0.01f)
        {
            text_scope ts = u.text_style(15.0f, active ? s.bold : s.font);
            u.text(rect::make(r.x + 48.0f + 3.0f * hv, r.y, 150.0f, r.h), PAGE_NAMES[i],
                   fade(ic, label_k), ALIGN_LEFT);
        }
        if (i == PAGE_BOARD && in_flight > 0)
        {
            // the count itself springs (a bouncy number), and so does the dot's size
            const f32 shown =
                u.animate("nav_badge"_id, static_cast<f32>(in_flight), spring{300.0f, 0.45f});
            const f32 bump =
                1.0f + 0.4f * min2(1.0f, std::fabs(shown - static_cast<f32>(in_flight)));
            if (label_k > 0.5f)
            {
                const rect b = rect::make(r.right() - 34.0f, r.center_y() - 10.0f, 26.0f, 20.0f);
                u.draw_rounded_rect(scaled(b, bump, bump), fade(COLUMN_COLORS[1], 0.9f), 10.0f);
                text_scope ts = u.text_style(12.0f, s.bold);
                u.textf(b, color{30, 20, 10, 255}, ALIGN_CENTER, "%d",
                        static_cast<i32>(shown + 0.5f));
            }
            else
                fill_circle(u, {ix + 11.0f, r.center_y() - 10.0f}, 4.0f * bump, COLUMN_COLORS[1]);
        }
        if (it.focused) outline(u, grow(r, 1.0f), 14.0f, th.focus_border, 1.5f);
        if (it.clicked || kb_hit(u, it)) set_page(s, i, u.ctx->now);
        if (label_k < 0.5f) (void)u.tooltip(r, id_child(nid, "tip"_id), PAGE_NAMES[i]);
    }

    // footer: collapse toggle + the animation status (frame timing is the HUD's)
    const rect foot = in.cut_bottom(40.0f);
    (void)in.cut_bottom(8.0f);
    if (label_k > 0.05f && in.h > 56.0f)
    {
        const rect st = in.cut_bottom(36.0f);
        u.draw_rounded_rect(st, fade(th.text, 0.05f * label_k), 14.0f);
        const bool busy = u.animations_active();
        const vec2 dot{st.x + 16.0f, st.center_y()};
        fill_circle(u, dot, busy ? 3.0f + std::sin(s.clock * 8.0f) : 3.0f,
                    fade(busy ? color{90, 230, 140, 255} : th.text_dim, label_k));
        text_scope ts = u.text_style(12.0f);
        u.textf(st.pad(28.0f, 0.0f, 10.0f, 0.0f), fade(th.text_dim, label_k), ALIGN_LEFT, "%s",
                busy ? "animating" : "settled");
    }
    {
        const uiid cid = "nav_collapse"_id;
        const interaction it = u.interact(cid, foot, !s.blocked);
        if (it.hovered) u.set_cursor(CURSOR_HAND);
        const f32 hv = u.smooth(id_child(cid, "hv"_id), it.hovered ? 1.0f : 0.0f, 0.06f);
        if (hv > 0.01f) u.draw_rounded_rect(foot, fade(th.text, 0.06f * hv), 13.0f);
        const f32 rot = u.animate("nav_chev"_id, open_target ? 0.0f : PI, spring{200.0f, 0.6f});
        const f32 ix = lerpf(foot.center_x(), foot.x + 24.0f, label_k);
        icon(u, ICON_CHEVRON, {ix, foot.center_y()}, 18.0f, th.text_dim, rot);
        if (label_k > 0.01f)
            u.text(rect::make(foot.x + 48.0f, foot.y, 150.0f, foot.h), "Collapse",
                   fade(th.text_dim, label_k), ALIGN_LEFT);
        if (it.focused) outline(u, grow(foot, 1.0f), 14.0f, th.focus_border, 1.5f);
        if (it.clicked || kb_hit(u, it))
        {
            if (narrow && s.auto_collapse && !open_target)
            {
                s.auto_collapse = false; // the user wants it open on a narrow window
                s.sidebar_open = true;
            }
            else
                s.sidebar_open = !open_target;
        }
        if (label_k < 0.5f) (void)u.tooltip(foot, "nav_collapse_tip"_id, "Expand");
    }
}

// ============================================================ page: overview
static void kpi_card(ui &u, showcase &s, i32 i, rect target, vec2 origin, f32 k)
{
    static const char *const names[4] = {"Active crew", "Fuel budget", "p95 latency", "Uptime"};
    static const i32 icons[4] = {ICON_SPARK, ICON_ROCKET, ICON_WAVE, ICON_CHECK};
    const theme &th = u.th();
    const uiid key = id_child("kpi"_id, static_cast<uiid>(i));
    rect r = u.animate_rect(key, target, {.origin = origin});
    r.y += (1.0f - k) * 28.0f;
    const interaction in = u.interact(key, r, !s.blocked, false);
    const f32 hv = u.smooth(id_child(key, "hv"_id), in.hovered ? 1.0f : 0.0f, 0.08f);
    r.y -= hv * 4.0f;
    const color hue = i == 0 ? s.accent_c : TAG_COLORS[i];
    if (hv > 0.01f)
        glass_glow(u, {r.center_x(), r.bottom() - 8.0f}, r.w * 0.55f, fade(hue, 0.3f * hv));
    glass_panel(u, r, 20.0f, 0.25f + 0.75f * k);
    rect in_r = r.pad(18.0f, 16.0f);

    rect top = in_r.cut_top(34.0f);
    const rect badge = top.cut_left(34.0f);
    (void)top.cut_left(10.0f);
    gradient_round(u, badge, 11.0f, mixc(hue, color::white(), 0.3f), hue, fade(hue, 0.7f), hue);
    icon(u, icons[i], {badge.center_x(), badge.center_y()}, 16.0f, color::white());
    const bool up = s.kpi_delta[i] >= 0.0f;
    const bool good = i == 2 ? !up : up;
    {
        text_scope ts = u.text_style(12.0f, s.bold);
        char d[24];
        std::snprintf(d, sizeof(d), "%s%.1f%%", up ? "+" : "", static_cast<double>(s.kpi_delta[i]));
        const f32 dw = u.text_width(d) + 16.0f;
        const rect chip = top.cut_right(dw).pad(0.0f, 7.0f);
        const color cc = good ? color{80, 220, 140, 255} : color{255, 100, 120, 255};
        u.draw_rounded_rect(chip, fade(cc, 0.18f), 10.0f);
        u.text(chip, d, cc, ALIGN_CENTER);
    }
    u.text_ellipsis(top, names[i], th.text_dim, ALIGN_LEFT);

    // the number counts up: a spring on the value itself (from 0 the first time)
    (void)in_r.cut_top(4.0f);
    const f32 v = u.animate(id_child(key, "v"_id), s.kpi[i], spring{55.0f, 1.0f});
    char buf[32];
    if (i == 0)
        std::snprintf(buf, sizeof(buf), "%d,%03d", static_cast<i32>(v) / 1000,
                      static_cast<i32>(v) % 1000);
    else if (i == 1)
        std::snprintf(buf, sizeof(buf), "$%d,%03d", static_cast<i32>(v) / 1000,
                      static_cast<i32>(v) % 1000);
    else if (i == 2)
        std::snprintf(buf, sizeof(buf), "%.0f ms", static_cast<double>(v));
    else
        std::snprintf(buf, sizeof(buf), "%.2f%%", static_cast<double>(v));
    {
        text_scope ts = u.text_style(26.0f, s.bold);
        u.text(in_r.cut_top(32.0f), buf, th.text, ALIGN_LEFT);
    }

    // a live sparkline that draws itself in
    const rect sp = in_r.bottom_slice(min2(30.0f, in_r.h));
    if (sp.h > 6.0f)
    {
        const f32 reveal =
            s.reduced_motion ? 1.0f : sat((s.age - 0.25f - 0.08f * static_cast<f32>(i)) / 0.8f);
        const i32 n = 2 + static_cast<i32>(26.0f * ease(easing::EASE_OUT, reveal));
        vec2 pts[28];
        const f32 t = s.clock;
        for (i32 j = 0; j < n; ++j)
        {
            const f32 fj = static_cast<f32>(j);
            const f32 val = 0.5f +
                            0.28f * std::sin(fj * 0.55f + t * 1.3f + static_cast<f32>(i) * 1.7f) +
                            0.14f * std::sin(fj * 1.37f - t * 0.9f + static_cast<f32>(i));
            pts[j] = {sp.x + sp.w * fj / 27.0f, sp.bottom() - sat(val) * sp.h};
        }
        area_under(u, pts, n, sp.bottom(), fade(hue, 0.35f), fade(hue, 0.0f));
        stroke(u, pts, n, hue, 2.0f);
        const vec2 tip = pts[n - 1];
        fill_circle(u, tip, 3.0f + (in.hovered ? 1.5f * (1.0f + std::sin(s.clock * 6.0f)) : 0.0f),
                    hue);
    }
}

static f32 chart_value(f32 x, f32 amp)
{
    return 0.5f + amp * (0.2f * std::sin(x * 0.21f) + 0.12f * std::sin(x * 0.53f + 1.0f)) +
           0.07f * std::sin(x * 1.71f) + 0.04f * std::sin(x * 3.1f + 2.0f);
}

static void chart_card(ui &u, showcase &s, rect target, vec2 origin, f32 k)
{
    const theme &th = u.th();
    const rect r = card(u, s, "ov_chart"_id, target, origin, k);
    rect in = r.pad(20.0f, 16.0f);
    rect head = in.cut_top(40.0f);
    static const char *const ranges[3] = {"Live", "1h", "24h"};
    const rect seg = head.cut_right(min2(190.0f, head.w * 0.45f)).pad(0.0f, 4.0f);
    (void)comp::segmented(u, seg, ranges, s.chart_range, {.id = "ov_range"_id});
    card_title(u, head, "Throughput", "requests per second, streaming");
    (void)in.cut_top(10.0f);
    const rect plot = in;
    if (plot.w < 20.0f || plot.h < 20.0f) return;

    for (i32 j = 0; j <= 3; ++j) // grid
    {
        const f32 y = plot.y + plot.h * static_cast<f32>(j) / 3.0f;
        u.draw_line(plot.x, y, plot.right(), y, fade(th.text, 0.07f), 1.0f);
    }
    // the range changes how fast and how wild the stream is; both ease
    const f32 speed = u.smooth(
        "ov_speed"_id, s.chart_range == 0 ? 3.0f : (s.chart_range == 1 ? 0.8f : 0.25f), 0.3f);
    const f32 amp = u.smooth("ov_amp"_id,
                             s.chart_range == 0 ? 1.0f : (s.chart_range == 1 ? 0.7f : 1.4f), 0.25f);
    if (s.motion_on) s.chart_phase += speed * static_cast<f32>(u.ctx->dt);
    const f32 reveal = s.reduced_motion ? 1.0f : ease(easing::EASE_OUT, sat((s.age - 0.3f) / 0.7f));

    constexpr i32 N = 64;
    const f32 base = std::floor(s.chart_phase), frac = s.chart_phase - base;
    const f32 step = plot.w / static_cast<f32>(N - 2);
    vec2 a[N], b[N];
    for (i32 j = 0; j < N; ++j)
    {
        const f32 x = plot.x + (static_cast<f32>(j) - frac) * step;
        const f32 fx = base + static_cast<f32>(j);
        a[j] = {x, plot.bottom() - chart_value(fx, amp) * plot.h * reveal};
        b[j] = {x, plot.bottom() -
                       (0.32f + 0.14f * std::sin(fx * 0.17f + 2.0f) + 0.07f * std::sin(fx * 0.9f)) *
                           plot.h * reveal};
    }
    {
        region rg = u.region(plot, "ov_plot"_id); // the stream scrolls through a clip
        area_under(u, a, N, plot.bottom(), fade(s.accent_c, 0.45f), fade(s.accent_c, 0.0f));
        stroke(u, b, N, fade(color{80, 220, 230, 255}, 0.8f), 1.6f);
        stroke(u, a, N, mixc(s.accent_c, color::white(), 0.15f), 2.4f);
    }

    // hover: a crosshair and a bubble that glides after the pointer
    const interaction hin = u.interact("ov_plot_hit"_id, plot, !s.blocked, false);
    const f32 show = u.smooth("ov_bub"_id, hin.hovered ? 1.0f : 0.0f, 0.06f);
    i32 idx = static_cast<i32>((u.ctx->mouse_x - plot.x) / step + frac + 0.5f);
    idx = idx < 1 ? 1 : (idx > N - 2 ? N - 2 : idx);
    const f32 bx = u.smooth("ov_bx"_id, a[idx].x, 0.04f);
    const f32 by = u.smooth("ov_by"_id, a[idx].y, 0.04f);
    if (show > 0.01f)
    {
        u.draw_line(bx, plot.y, bx, plot.bottom(), fade(th.text, 0.25f * show), 1.0f);
        glass_glow(u, {bx, by}, 24.0f, fade(s.accent_c, 0.6f * show));
        fill_circle(u, {bx, by}, 5.0f, fade(color::white(), show));
        fill_circle(u, {bx, by}, 3.0f, fade(s.accent_c, show));
        char t[32];
        std::snprintf(
            t, sizeof(t), "%.0f req/s",
            static_cast<double>((plot.bottom() - a[idx].y) / max2(plot.h, 1.0f) * 1840.0f));
        const f32 w = u.text_width(t) + 20.0f;
        const rect bub = rect::make(clampf(bx - w * 0.5f, plot.x, plot.right() - w),
                                    by - 40.0f - 6.0f * (1.0f - show), w, 26.0f);
        u.draw_rounded_rect(
            bub, fade(mixc(color{20, 18, 44, 255}, color::white(), s.light), 0.92f * show), 13.0f);
        u.text(bub, t, fade(th.text, show), ALIGN_CENTER);
    }
    else
    {
        // a live dot breathing on the newest sample
        const vec2 last = a[N - 2];
        const f32 ph = s.clock * 2.0f - std::floor(s.clock * 2.0f);
        u.draw_arc(last, 4.0f + ph * 12.0f, 1.5f, 0.0f, 2.0f * PI, fade(s.accent_c, 1.0f - ph));
        fill_circle(u, last, 3.5f, s.accent_c);
    }
}

static void gauge_card(ui &u, showcase &s, rect target, vec2 origin, f32 k)
{
    const theme &th = u.th();
    const rect r = card(u, s, "ov_gauges"_id, target, origin, k);
    rect in = r.pad(20.0f, 16.0f);
    card_title(u, in.cut_top(40.0f), "Cluster health", "springs settle on every new reading");
    (void)in.cut_top(6.0f);
    static const char *const names[3] = {"Compute", "Memory", "Network"};
    const color ca[3] = {s.accent_c, color{255, 110, 180, 255}, color{60, 210, 200, 255}};
    track_row tr(in, {track_size::flex(), track_size::flex(), track_size::flex()}, 8.0f);
    for (i32 i = 0; i < 3; ++i)
    {
        rect cell = tr.next();
        const rect label = cell.cut_bottom(20.0f);
        const vec2 c{cell.center_x(), cell.center_y()};
        const f32 rad = max2(8.0f, min2(cell.w, cell.h) * 0.5f - 8.0f);
        const f32 v = u.animate(id_child("ov_g"_id, static_cast<uiid>(i)),
                                s.gauges[i] * (0.15f + 0.85f * k), spring{70.0f, 0.42f});
        u.draw_arc(c, rad, 9.0f, 0.0f, 2.0f * PI, fade(th.text, 0.09f));
        constexpr i32 SEGS = 36;
        const f32 a0 = -PI * 0.5f, span = clampf(v, 0.0f, 1.0f) * 2.0f * PI;
        for (i32 j = 0; j < SEGS; ++j) // a gradient arc: short segments, each its own color
        {
            const f32 f0 = static_cast<f32>(j) / SEGS, f1 = static_cast<f32>(j + 1) / SEGS;
            u.draw_arc(c, rad, 9.0f, a0 + span * f0, a0 + span * f1 + 0.01f,
                       mixc(fade(ca[i], 0.55f), mixc(ca[i], color::white(), 0.3f), f1));
        }
        const vec2 tip{c.x + std::cos(a0 + span) * rad, c.y + std::sin(a0 + span) * rad};
        glass_glow(u, tip, 16.0f, fade(ca[i], 0.7f));
        if (i == 0) // a radar sweep riding outside the first ring
        {
            const f32 sw = s.clock * 1.8f;
            u.draw_arc(c, rad + 9.0f, 2.0f, sw, sw + 0.7f, fade(ca[i], 0.5f));
        }
        {
            text_scope ts = u.text_style(20.0f, s.bold);
            u.textf(rect::make(c.x - 40.0f, c.y - 14.0f, 80.0f, 28.0f), th.text, ALIGN_CENTER,
                    "%d%%", static_cast<i32>(v * 100.0f + 0.5f));
        }
        u.text(label, names[i], th.text_dim, ALIGN_CENTER);
    }
}

static void bars_card(ui &u, showcase &s, rect target, vec2 origin, f32 k)
{
    const theme &th = u.th();
    const rect r = card(u, s, "ov_bars"_id, target, origin, k);
    rect in = r.pad(20.0f, 16.0f);
    rect head = in.cut_top(40.0f);
    static const char *const ranges[3] = {"Week", "Month", "Year"};
    const rect seg = head.cut_right(min2(220.0f, head.w * 0.5f)).pad(0.0f, 4.0f);
    (void)comp::segmented(u, seg, ranges, s.bar_range, {.id = "ov_bar_range"_id});
    if (s.bar_range != s.bar_range_seen)
    {
        s.bar_range_seen = s.bar_range;
        bars_retarget(s, u.ctx->now);
    }
    card_title(u, head, "Launch cadence", "each bar retargets a beat after its neighbor");
    (void)in.cut_top(10.0f);
    const rect labels = in.cut_bottom(18.0f);
    const rect plot = in;
    static const char *const week[12] = {"M", "T", "W", "T", "F", "S",
                                         "S", "M", "T", "W", "T", "F"};
    static const char *const year[12] = {"J", "F", "M", "A", "M", "J",
                                         "J", "A", "S", "O", "N", "D"};
    const f32 slot = plot.w / 12.0f;
    const f32 since = static_cast<f32>(u.ctx->now - s.bars_at);
    const f32 reveal = s.reduced_motion ? 1.0f : sat((s.age - 0.2f) / 0.6f);
    const bool over = plot.contains(u.ctx->mouse_x, u.ctx->mouse_y) && !s.blocked;
    const i32 hot = over ? static_cast<i32>((u.ctx->mouse_x - plot.x) / max2(slot, 1.0f)) : -1;
    for (i32 i = 0; i < 12; ++i)
    {
        // the stagger: bar i switches to its new target 35 ms after bar i-1
        const f32 target_v = since >= 0.035f * static_cast<f32>(i) ? s.bars_to[i] : s.bars_from[i];
        const f32 v = u.animate(id_child("ov_bar"_id, static_cast<uiid>(i)), target_v * reveal,
                                spring{200.0f, 0.5f});
        const f32 hv =
            u.smooth(id_child("ov_bar_hv"_id, static_cast<uiid>(i)), hot == i ? 1.0f : 0.0f, 0.05f);
        const f32 bw = slot * (0.52f + 0.1f * hv);
        const f32 bh = max2(4.0f, clampf(v, 0.0f, 1.15f) * plot.h);
        const rect b = rect::make(plot.x + slot * (static_cast<f32>(i) + 0.5f) - bw * 0.5f,
                                  plot.bottom() - bh, bw, bh);
        const color top = mixc(mixc(s.accent_c, color::white(), 0.25f), color::white(), 0.3f * hv);
        gradient_round(u, b, min2(8.0f, bw * 0.4f), top, top, fade(s.accent_c, 0.35f),
                       fade(s.accent_c, 0.35f));
        if (hv > 0.01f)
        {
            glass_glow(u, {b.center_x(), b.y}, 30.0f, fade(s.accent_c, 0.5f * hv));
            char t[16];
            std::snprintf(t, sizeof(t), "%d", static_cast<i32>(s.bars_to[i] * 48.0f));
            text_scope ts = u.text_style(13.0f, s.bold);
            u.text(rect::make(b.x - 20.0f, b.y - 24.0f - 4.0f * hv, b.w + 40.0f, 20.0f), t,
                   fade(th.text, hv), ALIGN_CENTER);
        }
        text_scope ts = u.text_style(12.0f);
        u.text(rect::make(plot.x + slot * static_cast<f32>(i), labels.y, slot, labels.h),
               s.bar_range == 2 ? year[i] : week[i], hot == i ? th.text : th.text_dim,
               ALIGN_CENTER);
    }
}

static void feed_card(ui &u, showcase &s, rect target, vec2 origin, f32 k)
{
    static const char *const what[8] = {
        "Ada merged the orbit planner",   "Grace deployed build 7.3",
        "Linus reviewed the thruster PR", "Margaret ran the launch drill",
        "Alan rotated the API keys",      "Katherine updated a trajectory",
        "Hedy patched telemetry",         "Barbara closed 12 issues"};
    const theme &th = u.th();
    const rect r = card(u, s, "ov_feed"_id, target, origin, k);
    rect in = r.pad(20.0f, 16.0f);
    rect head = in.cut_top(40.0f);
    {
        const rect live = head.cut_right(60.0f);
        const f32 ph = s.clock * 1.4f - std::floor(s.clock * 1.4f);
        const vec2 dc{live.x + 8.0f, live.y + 11.0f};
        u.draw_arc(dc, 4.0f + ph * 8.0f, 1.5f, 0.0f, 2.0f * PI,
                   fade(color{255, 90, 110, 255}, 1.0f - ph));
        fill_circle(u, dc, 4.0f, color{255, 90, 110, 255});
        text_scope ts = u.text_style(12.0f, s.bold);
        u.text(rect::make(dc.x + 10.0f, live.y, 40.0f, 22.0f), "LIVE", th.text_dim, ALIGN_LEFT);
    }
    card_title(u, head, "Mission feed", "new events push in; the rest glide down");
    (void)in.cut_top(8.0f);
    // rows FLIP relative to the card's own (animated) corner: they move with the
    // card rigidly and only animate when the list itself changes
    const vec2 o{r.x, r.y};
    const f32 row_h = 50.0f;
    const i32 fit = static_cast<i32>((in.h + 6.0f) / row_h);
    region rg = u.region(in, "ov_feed_clip"_id);
    for (i32 j = 0; j < static_cast<i32>(s.feed.size()) && j < fit + 1; ++j)
    {
        const feed_event &e = s.feed[static_cast<usize>(j)];
        const uiid key = id_child("feed"_id, static_cast<uiid>(e.serial));
        const rect slot = rect::make(in.x, in.y + static_cast<f32>(j) * row_h, in.w, row_h - 6.0f);
        const rect rr = u.animate_rect(key, slot, {.spec = spring{200.0f, 0.82f}, .origin = o});
        const f32 a = u.appear(id_child(key, "in"_id), 0.45f, easing::EASE_OUT_BACK);
        const rect row_r = scaled(rr, 0.92f + 0.08f * a, 0.6f + 0.4f * clampf(a, 0.0f, 1.0f));
        const f32 alpha = sat(a) * (j >= fit ? 0.35f : 1.0f);
        u.draw_rounded_rect(row_r, fade(th.text, 0.05f * alpha), 12.0f);
        const vec2 av{row_r.x + 22.0f, row_r.center_y()};
        const color hc = hsv(hash01(static_cast<u32>(e.what) * 17u + 3u), 0.55f, 0.95f);
        fill_circle(u, av, 14.0f * clampf(a, 0.0f, 1.2f), fade(hc, alpha));
        {
            text_scope ts = u.text_style(13.0f, s.bold);
            const char init[2] = {what[e.what % 8][0], 0};
            u.text(rect::make(av.x - 14.0f, av.y - 14.0f, 28.0f, 28.0f), init,
                   fade(color{20, 16, 40, 255}, alpha), ALIGN_CENTER);
        }
        const f32 ago = static_cast<f32>(u.ctx->now - e.at);
        char t[24];
        if (ago < 5.0f)
            std::snprintf(t, sizeof(t), "now");
        else if (ago < 60.0f)
            std::snprintf(t, sizeof(t), "%ds", static_cast<i32>(ago));
        else
            std::snprintf(t, sizeof(t), "%dm", static_cast<i32>(ago / 60.0f));
        rect tr = row_r.pad(44.0f, 0.0f, 12.0f, 0.0f);
        text_scope ts = u.text_style(13.0f);
        u.text(tr.cut_right(36.0f), t, fade(th.text_dim, alpha), ALIGN_RIGHT);
        u.text_ellipsis(tr, what[e.what % 8], fade(th.text, alpha), ALIGN_LEFT);
    }
}

static void orbit_art(ui &u, showcase &s, rect r)
{
    const vec2 c{r.center_x(), r.center_y()};
    const f32 R = min2(r.w, r.h) * 0.5f;
    const f32 t = s.clock;
    glass_glow(u, c, R * 1.3f, fade(s.accent_c, 0.35f));
    for (i32 i = 0; i < 3; ++i)
        u.draw_arc(c, R * (0.45f + 0.18f * static_cast<f32>(i)), 1.0f, 0.0f, 2.0f * PI,
                   fade(u.th().text, 0.12f));
    glass_orb(u, c, R * 0.26f, mixc(s.accent_c, color::white(), 0.5f), s.accent_c);
    for (i32 i = 0; i < 3; ++i)
    {
        const f32 fi = static_cast<f32>(i);
        const f32 a = t * (0.9f - 0.25f * fi) + fi * 2.2f;
        const f32 rr = R * (0.45f + 0.18f * fi);
        const vec2 p{c.x + std::cos(a) * rr, c.y + std::sin(a) * rr};
        glass_glow(u, p, 18.0f, fade(TAG_COLORS[i], 0.7f));
        fill_circle(u, p, 5.0f + fi, TAG_COLORS[i]);
    }
}

static void launch(ui &u, showcase &s, vec2 at)
{
    burst(s, at, 140, 900.0f);
    randomize_metrics(s, u.ctx->now);
    notify(u, s, "Launch sequence complete - metrics refreshed", 1);
}

static f32 page_overview(ui &u, showcase &s, rect area, vec2 origin)
{
    const theme &th = u.th();
    const f32 W = area.w;
    column col(area, 16.0f);

    // hero: typewriter greeting, actions, and an orbit illustration
    {
        const f32 k = enter_k(s, 0);
        const rect r = card(u, s, "ov_hero"_id, col.next(164.0f), origin, k, 24.0f);
        rect in = r.pad(26.0f, 22.0f);
        const bool art = in.w > 560.0f;
        const rect art_r = art ? in.cut_right(min2(300.0f, in.w * 0.38f)) : rect{};
        static const char greeting[] = "Good evening, Commander";
        const i32 n = static_cast<i32>(sizeof(greeting) - 1);
        const i32 shown =
            (s.reduced_motion || s.transition == 2)
                ? n
                : static_cast<i32>(clampf((s.age - 0.15f) * 34.0f, 0.0f, static_cast<f32>(n)));
        {
            text_scope ts = u.text_style(28.0f, s.bold);
            const rect tr = in.cut_top(38.0f);
            const std::string_view sv(greeting, static_cast<usize>(shown));
            u.text(tr, sv, th.text, ALIGN_LEFT);
            if (shown < n || std::fmod(u.ctx->now, 1.0) < 0.5) // the caret blinks once typing ends
            {
                const f32 cx = tr.x + u.text_width(sv) + 3.0f;
                u.draw_rect(rect::make(cx, tr.y + 6.0f, 2.5f, tr.h - 12.0f), s.accent_c);
            }
            if (shown < n) u.request_redraw();
        }
        u.text_ellipsis(in.cut_top(22.0f), "All systems nominal - 3 launches queued, 2 in flight.",
                        th.text_dim, ALIGN_LEFT);
        (void)in.cut_top(16.0f);
        row btns(in.cut_top(40.0f), 10.0f);
        const rect lb = btns.next(148.0f);
        if (pill_button(u, lb, "Launch", "ov_launch"_id, {.icon = ICON_ROCKET, .primary = true}))
            launch(u, s, {lb.center_x(), lb.center_y()});
        if (pill_button(u, btns.next(140.0f), "Randomize", "ov_rand"_id, {.icon = ICON_SHUFFLE}))
            randomize_metrics(s, u.ctx->now);
        if (art) orbit_art(u, s, art_r);
    }

    // KPIs: an auto-fit grid; when the column count changes, the cards glide
    {
        const f32 min_w = 220.0f, cell_h = 136.0f;
        const grid_cursor probe = auto_fit_grid(col.remaining(), 4, min_w, cell_h, 16.0f);
        const i32 rows = probe.rows();
        const rect grid_r =
            col.next(static_cast<f32>(rows) * cell_h + static_cast<f32>(rows - 1) * 16.0f);
        const grid_cursor gc = auto_fit_grid(grid_r, 4, min_w, cell_h, 16.0f);
        for (i32 i = 0; i < 4; ++i) kpi_card(u, s, i, gc.cell(i), origin, enter_k(s, 1 + i));
    }

    // chart + gauges side by side when wide, stacked when narrow (the swap animates)
    {
        if (W >= 900.0f)
        {
            track_row tr(col.next(300.0f), {track_size::flex(1.8f), track_size::flex(1.0f)}, 16.0f);
            const rect a = tr.next(), b = tr.next();
            chart_card(u, s, a, origin, enter_k(s, 5));
            gauge_card(u, s, b, origin, enter_k(s, 6));
        }
        else
        {
            const rect a = col.next(280.0f);
            const rect b = col.next(230.0f);
            chart_card(u, s, a, origin, enter_k(s, 5));
            gauge_card(u, s, b, origin, enter_k(s, 6));
        }
    }
    {
        if (W >= 900.0f)
        {
            track_row tr(col.next(320.0f), {track_size::flex(1.3f), track_size::flex(1.0f)}, 16.0f);
            const rect a = tr.next(), b = tr.next();
            bars_card(u, s, a, origin, enter_k(s, 7));
            feed_card(u, s, b, origin, enter_k(s, 8));
        }
        else
        {
            const rect a = col.next(280.0f);
            const rect b = col.next(320.0f);
            bars_card(u, s, a, origin, enter_k(s, 7));
            feed_card(u, s, b, origin, enter_k(s, 8));
        }
    }
    return col.remaining().y - area.y;
}

// ============================================================ page: layout lab
static void lab_menu_pick(showcase &s, usize ti, i32 pick)
{
    lab_tile &t = s.tiles[ti];
    if (pick == 0) s.lab_selected = (s.lab_selected == t.id) ? 0 : t.id;
    if (pick == 1)
    {
        lab_tile c = t;
        c.id = s.next_tile++;
        c.hue = t.hue + 0.04f;
        c.dying = -1.0;
        s.tiles.insert(s.tiles.begin() + static_cast<std::ptrdiff_t>(ti + 1), c);
    }
    if (pick == 2) t.hue = frand(s);
}

// Local slots (relative to the stage's inner top-left) for the live tiles, in
// order; returns the height the arrangement needs.
static f32 lab_layout(const showcase &s, f32 W, f32 t, std::vector<rect> &out)
{
    out.clear();
    i32 n = 0;
    for (const lab_tile &tl : s.tiles)
        if (tl.dying < 0.0) n += 1;
    const f32 size = s.lab_size, gap = s.lab_gap;
    const i32 cols = static_cast<i32>(max2(1.0f, std::floor((W + gap) / (size + gap))));
    const f32 cell = (W - gap * static_cast<f32>(cols - 1)) / static_cast<f32>(cols);
    f32 height = 0.0f;
    i32 i = 0;
    switch (s.lab_mode)
    {
    case 0: // grid packing: the selected tile spans 2x2, the rest flow around it
    {
        std::vector<u8> occ;
        for (const lab_tile &tl : s.tiles)
        {
            if (tl.dying >= 0.0) continue;
            const i32 span = (tl.id == s.lab_selected && cols >= 2) ? 2 : 1;
            for (i32 p = 0;; ++p)
            {
                const i32 rr = p / cols, cc = p % cols;
                if (cc + span > cols) continue;
                if (occ.size() < static_cast<usize>((rr + span) * cols))
                    occ.resize(static_cast<usize>((rr + span) * cols), 0);
                bool free = true;
                for (i32 y = 0; y < span && free; ++y)
                    for (i32 x = 0; x < span && free; ++x)
                        if (occ[static_cast<usize>((rr + y) * cols + cc + x)]) free = false;
                if (!free) continue;
                for (i32 y = 0; y < span; ++y)
                    for (i32 x = 0; x < span; ++x)
                        occ[static_cast<usize>((rr + y) * cols + cc + x)] = 1;
                const f32 ext = cell * static_cast<f32>(span) + gap * static_cast<f32>(span - 1);
                out.push_back(rect::make(static_cast<f32>(cc) * (cell + gap),
                                         static_cast<f32>(rr) * (cell + gap), ext, ext));
                height = max2(height, static_cast<f32>(rr + span) * (cell + gap) - gap);
                break;
            }
        }
        break;
    }
    case 1: // list: the selected row opens up
    {
        f32 y = 0.0f;
        for (const lab_tile &tl : s.tiles)
        {
            if (tl.dying >= 0.0) continue;
            const f32 h = tl.id == s.lab_selected ? 124.0f : 58.0f;
            out.push_back(rect::make(0.0f, y, W, h));
            y += h + max2(6.0f, gap * 0.5f);
        }
        height = y;
        break;
    }
    case 2: // masonry: shortest column first
    {
        std::vector<f32> colh(static_cast<usize>(cols), 0.0f);
        for (const lab_tile &tl : s.tiles)
        {
            if (tl.dying >= 0.0) continue;
            usize best = 0;
            for (usize c = 1; c < colh.size(); ++c)
                if (colh[c] < colh[best]) best = c;
            const f32 h = cell * (0.65f + 0.9f * tl.tall) * (tl.id == s.lab_selected ? 1.5f : 1.0f);
            out.push_back(rect::make(static_cast<f32>(best) * (cell + gap), colh[best], cell, h));
            colh[best] += h + gap;
            height = max2(height, colh[best] - gap);
        }
        break;
    }
    case 3: // orbit: the ring turns; the selected tile moves to the center
    {
        height = 520.0f;
        const vec2 c{W * 0.5f, height * 0.5f};
        const f32 R = min2(W, height) * 0.38f;
        const f32 sz = clampf(size * 0.55f, 40.0f, 84.0f);
        i32 ring_n = n;
        for (const lab_tile &tl : s.tiles)
            if (tl.dying < 0.0 && tl.id == s.lab_selected) ring_n -= 1;
        i32 j = 0;
        for (const lab_tile &tl : s.tiles)
        {
            if (tl.dying >= 0.0) continue;
            if (tl.id == s.lab_selected)
            {
                out.push_back(rect::make(c.x - sz, c.y - sz, sz * 2.0f, sz * 2.0f));
                continue;
            }
            const f32 a = static_cast<f32>(j++) /
                              static_cast<f32>(max2(1.0f, static_cast<f32>(ring_n))) * 2.0f * PI +
                          t * 0.35f;
            out.push_back(rect::make(c.x + std::cos(a) * R - sz * 0.5f,
                                     c.y + std::sin(a) * R - sz * 0.5f, sz, sz));
        }
        break;
    }
    default: // spiral: phyllotaxis, the golden angle, slowly turning
    {
        height = 540.0f;
        const vec2 c{W * 0.5f, height * 0.5f};
        const f32 spacing = clampf(size * 0.36f, 18.0f, 52.0f) * min2(1.0f, W / 600.0f + 0.3f);
        for (const lab_tile &tl : s.tiles)
        {
            if (tl.dying >= 0.0) continue;
            const f32 fi = static_cast<f32>(i++);
            const f32 rr = spacing * std::sqrt(fi + 0.6f);
            const f32 a = fi * 2.39996f + t * 0.12f;
            const f32 sz =
                clampf(size * 0.42f * (1.0f - fi / (static_cast<f32>(n) + 10.0f) * 0.45f), 18.0f,
                       70.0f) *
                (tl.id == s.lab_selected ? 1.7f : 1.0f);
            out.push_back(rect::make(c.x + std::cos(a) * rr - sz * 0.5f,
                                     c.y + std::sin(a) * rr - sz * 0.5f, sz, sz));
        }
        break;
    }
    }
    return height;
}

static void lab_tile_draw(ui &u, showcase &s, const lab_tile &tl, rect d, bool hovered, f32 alpha)
{
    const theme &th = u.th();
    const bool sel = tl.id == s.lab_selected;
    const f32 rad = min2(16.0f, min2(d.w, d.h) * 0.22f);
    const color c0 = fade(hsv(tl.hue, 0.5f, 1.0f), alpha),
                c1 = fade(hsv(tl.hue + 0.06f, 0.6f, 0.95f), alpha);
    const color c2 = fade(hsv(tl.hue + 0.12f, 0.72f, 0.78f), alpha),
                c3 = fade(hsv(tl.hue + 0.04f, 0.66f, 0.86f), alpha);
    gradient_round(u, d, rad, c0, c1, c2, c3);
    const rect gl = rect::make(d.x + 3.0f, d.y + 3.0f, max2(0.0f, d.w - 6.0f), d.h * 0.42f);
    gradient_round(u, gl, max2(0.0f, rad - 3.0f), fade(color::white(), 0.28f * alpha),
                   fade(color::white(), 0.18f * alpha), color{255, 255, 255, 0},
                   color{255, 255, 255, 0});
    if (sel)
    {
        const f32 p = 0.5f + 0.5f * std::sin(s.clock * 4.0f);
        outline(u, grow(d, 3.0f), rad + 3.0f, fade(color::white(), (0.6f + 0.4f * p) * alpha),
                2.0f);
    }
    else if (hovered)
        outline(u, grow(d, 2.0f), rad + 2.0f, fade(color::white(), 0.55f * alpha), 1.5f);
    if (d.w >= 34.0f && d.h >= 26.0f)
    {
        text_scope ts = u.text_style(d.w >= 60.0f ? 15.0f : 12.0f, s.bold);
        char t[16];
        std::snprintf(t, sizeof(t), "#%02u", tl.id);
        u.text(rect::make(d.x + 10.0f, d.y + 6.0f, d.w - 12.0f, 22.0f), t,
               fade(color{255, 255, 255, 240}, alpha), ALIGN_LEFT);
    }
    if (s.lab_mode == 1 && d.w > 220.0f && d.h > 40.0f)
    {
        text_scope ts = u.text_style(13.0f);
        static const char *const blurb[5] = {"Signal relay", "Docking port", "Solar array",
                                             "Cargo bay", "Science deck"};
        u.text(rect::make(d.x + 64.0f, d.y + 6.0f, d.w - 200.0f, 22.0f), blurb[tl.id % 5],
               fade(color::white(), alpha), ALIGN_LEFT);
        const rect bar = rect::make(d.right() - 130.0f, d.y + 15.0f, 110.0f, 6.0f);
        u.draw_rounded_rect(bar, fade(color::white(), 0.3f * alpha), 3.0f);
        u.draw_rounded_rect(rect::make(bar.x, bar.y, bar.w * (0.2f + 0.75f * tl.tall), bar.h),
                            fade(color::white(), alpha), 3.0f);
        if (sel && d.h > 100.0f)
            u.text_wrapped(
                rect::make(d.x + 14.0f, d.y + 40.0f, d.w - 28.0f, d.h - 48.0f),
                "Selected rows grow; the rows below slide down to make room. Every row is "
                "one u.animate_rect() call keyed by the tile's id.",
                fade(color::white(), 0.9f * alpha));
    }
    (void)th;
}

static f32 page_layout(ui &u, showcase &s, rect area, vec2 origin)
{
    const theme &th = u.th();
    column col(area, 16.0f);
    const bool narrow = area.w < 860.0f;

    // controls: on a narrow page the buttons drop to their own row (and glide)
    {
        // padding + head + gap + modes + gap + sliders (+ gap + the button row when narrow)
        const f32 ctrl_h =
            16.0f + 40.0f + 10.0f + 34.0f + 12.0f + 44.0f + 16.0f + (narrow ? 12.0f + 36.0f : 0.0f);
        const rect r = card(u, s, "lab_ctrl"_id, col.next(ctrl_h), origin, enter_k(s, 0));
        const vec2 o{r.x, r.y};
        rect in = r.pad(20.0f, 16.0f);
        rect head = in.cut_top(40.0f);
        i32 alive = 0;
        for (const lab_tile &tl : s.tiles)
            if (tl.dying < 0.0) alive += 1;
        {
            const rect chip = head.cut_right(90.0f).pad(0.0f, 8.0f);
            u.draw_rounded_rect(chip, fade(s.accent_c, 0.2f), 12.0f);
            text_scope ts = u.text_style(13.0f, s.bold);
            u.textf(chip, th.text, ALIGN_CENTER, "%d tiles", alive);
        }
        card_title(u, head, "Layout lab",
                   "grid packing, list, masonry, orbit, spiral - click a tile to feature it");
        (void)in.cut_top(10.0f);
        static const char *const modes[5] = {"Grid", "List", "Masonry", "Orbit", "Spiral"};
        rect line1 = in.cut_top(34.0f);
        (void)comp::segmented(u, line1.cut_left(min2(460.0f, line1.w)), modes, s.lab_mode,
                              {.id = "lab_mode"_id});
        (void)in.cut_top(12.0f);
        rect line2 = in.cut_top(44.0f);
        rect buttons{};
        if (!narrow)
        {
            buttons = line2.cut_right(4.0f * 100.0f + 3.0f * 8.0f).pad(0.0f, 4.0f);
            (void)line2.cut_right(20.0f);
        }
        else
        {
            (void)in.cut_top(12.0f);
            buttons = in.cut_top(36.0f);
        }
        track_row sl(line2, {track_size::flex(), track_size::flex()}, 20.0f);
        (void)fancy_slider(u, sl.next(), "Tile size", s.lab_size, 64.0f, 180.0f, "lab_size"_id,
                           "%.0f px");
        (void)fancy_slider(u, sl.next(), "Gap", s.lab_gap, 0.0f, 32.0f, "lab_gap"_id, "%.0f px");
        row br(buttons, 8.0f);
        const rect b0 = u.animate_rect("lab_b0"_id, br.next(100.0f), {.origin = o});
        const rect b1 = u.animate_rect("lab_b1"_id, br.next(100.0f), {.origin = o});
        const rect b2 = u.animate_rect("lab_b2"_id, br.next(100.0f), {.origin = o});
        const rect b3 = u.animate_rect("lab_b3"_id, br.next(100.0f), {.origin = o});
        if (pill_button(u, b0, "Add", "lab_add"_id, {.icon = ICON_PLUS, .primary = true}))
            add_tile(s);
        if (pill_button(u, b1, "Remove", "lab_rm"_id, {.icon = ICON_MINUS}))
        {
            // the featured tile goes first, otherwise the last one
            for (usize i = s.tiles.size(); i > 0; --i)
            {
                lab_tile &t = s.tiles[i - 1];
                if (t.dying >= 0.0) continue;
                const bool pick = s.lab_selected == 0 || t.id == s.lab_selected;
                if (!pick) continue;
                t.dying = u.ctx->now;
                if (t.id == s.lab_selected) s.lab_selected = 0;
                break;
            }
        }
        if (pill_button(u, b2, "Shuffle", "lab_shuffle"_id, {.icon = ICON_SHUFFLE}))
            shuffle_tiles(s);
        if (pill_button(u, b3, "Sort", "lab_sort"_id, {.icon = ICON_SORT}))
            std::stable_sort(s.tiles.begin(), s.tiles.end(),
                             [](const lab_tile &a, const lab_tile &b)
                             { return a.hue - std::floor(a.hue) < b.hue - std::floor(b.hue); });
    }

    // the stage
    {
        const f32 inner_w = max2(40.0f, area.w - 40.0f);
        std::vector<rect> slots;
        const f32 need = lab_layout(s, inner_w, s.motion_on ? s.clock : 0.0f, slots);
        const rect r =
            card(u, s, "lab_stage"_id, col.next(max2(440.0f, need + 40.0f)), origin, enter_k(s, 1));
        const rect inner = r.pad(20.0f);
        const vec2 io{inner.x, inner.y};
        for (f32 y = inner.y + 12.0f; y < inner.bottom(); y += 24.0f) // a dotted floor
            for (f32 x = inner.x + 12.0f; x < inner.right(); x += 24.0f)
                u.draw_rect(rect::make(x, y, 1.5f, 1.5f), fade(th.text, 0.12f));

        static const char *const menu[4] = {"Feature / unfeature", "Duplicate", "Recolor",
                                            "Remove"};
        usize slot_i = 0;
        i32 menu_tile = -1, menu_pick = -1;
        const f32 dying_s = 0.3f;
        for (usize ti = 0; ti < s.tiles.size(); ++ti)
        {
            const lab_tile &tl = s.tiles[ti];
            const uiid key = id_child("tile"_id, static_cast<uiid>(tl.id));
            rect shown{};
            f32 scale = 1.0f;
            if (tl.dying >= 0.0)
            {
                shown = u.animate_rect(key, u.motion_info(key).target, {.origin = io});
                scale = 1.0f - ease(easing::EASE_IN,
                                    sat(static_cast<f32>(u.ctx->now - tl.dying) / dying_s));
                u.request_redraw();
            }
            else
            {
                const rect lr = slots[slot_i++];
                shown = u.animate_rect(key, shifted(lr, io.x, io.y),
                                       {.spec = spring{s.lab_mode >= 3 ? 120.0f : 210.0f,
                                                       s.lab_mode >= 3 ? 0.9f : 0.7f},
                                        .origin = io});
                scale = clampf(u.appear(id_child(key, "in"_id), 0.5f, easing::EASE_OUT_BACK), 0.0f,
                               1.2f);
            }
            // squash & stretch along the direction of travel
            const vec2 v = u.motion_info(key).velocity;
            const f32 sp = std::sqrt(v.x * v.x + v.y * v.y);
            const f32 st = clampf(sp / 2800.0f, 0.0f, 0.16f);
            const bool horiz = std::fabs(v.x) > std::fabs(v.y);
            const rect d0 = scaled(shown, scale * (horiz ? 1.0f + st : 1.0f - st * 0.6f),
                                   scale * (horiz ? 1.0f - st * 0.6f : 1.0f + st));
            const interaction it = u.interact(key, d0, !s.blocked && tl.dying < 0.0, false);
            if (it.hovered) u.set_cursor(CURSOR_HAND);
            const f32 hv = u.smooth(id_child(key, "hv"_id), it.hovered ? 1.0f : 0.0f, 0.06f);
            const rect d =
                shifted(scaled(d0, 1.0f + 0.04f * hv, 1.0f + 0.04f * hv), 0.0f, -3.0f * hv);
            if (scale > 0.02f)
            {
                soft_shadow(u, d, min2(16.0f, min2(d.w, d.h) * 0.22f), 8.0f + 10.0f * hv,
                            0.8f * sat(scale), 4.0f + 6.0f * hv);
                lab_tile_draw(u, s, tl, d, it.hovered, sat(scale));
            }
            if (it.clicked) s.lab_selected = (s.lab_selected == tl.id) ? 0 : tl.id;
            if (tl.dying < 0.0 && !s.blocked)
            {
                const i32 pick = u.context_menu(id_child(key, "menu"_id), d, menu, 4);
                if (pick >= 0)
                {
                    menu_tile = static_cast<i32>(ti);
                    menu_pick = pick;
                }
            }
        }
        if (menu_tile >= 0)
        {
            if (menu_pick == 3)
                s.tiles[static_cast<usize>(menu_tile)].dying = u.ctx->now;
            else
                lab_menu_pick(s, static_cast<usize>(menu_tile), menu_pick);
        }
        const f64 now = u.ctx->now;
        std::erase_if(s.tiles,
                      [&](const lab_tile &t) { return t.dying >= 0.0 && now - t.dying > dying_s; });
    }
    return col.remaining().y - area.y;
}

// ============================================================ page: board
static constexpr f32 CARD_H = 92.0f;
static constexpr f32 CARD_H_OPEN = 156.0f;
static constexpr f32 CARD_GAP = 10.0f;
static constexpr f32 COL_HEAD = 50.0f;

static f32 card_height(const showcase &s, const board_card &c, f64 now)
{
    const f32 h = c.id == s.expanded ? CARD_H_OPEN : CARD_H;
    if (c.dying < 0.0) return h;
    return h * (1.0f - ease(easing::EASE_IN_OUT, sat(static_cast<f32>(now - c.dying) / 0.3f)));
}

static void board_card_draw(ui &u, showcase &s, const board_card &c, rect r, f32 hv, f32 lift,
                            f32 alpha)
{
    const theme &th = u.th();
    if (r.h < 6.0f) return;
    soft_shadow(u, r, 14.0f, 6.0f + 14.0f * lift + 4.0f * hv, (0.6f + 0.8f * lift) * alpha,
                3.0f + 10.0f * lift);
    const color bg = mixc(color{36, 34, 72, 235}, color{255, 255, 255, 235}, s.light);
    u.draw_rounded_rect(r, fade(mixc(bg, mixc(bg, color::white(), 0.1f), hv + lift), alpha), 14.0f);
    outline(u, r, 14.0f, fade(th.text, (0.1f + 0.12f * hv + 0.3f * lift) * alpha), 1.0f);
    u.draw_rounded_rect(rect::make(r.x + 6.0f, r.y + 10.0f, 3.5f, max2(0.0f, r.h - 20.0f)),
                        fade(TAG_COLORS[c.tag], alpha), 1.75f);
    region rg = u.region(r, id_child("bd_clip"_id, static_cast<uiid>(c.id)));
    rect in = r.pad(18.0f, 10.0f, 12.0f, 10.0f);
    rect top = in.cut_top(22.0f);
    if (c.col == 2)
    {
        const rect ck = top.cut_right(22.0f);
        fill_circle(u, {ck.center_x(), ck.center_y()}, 9.0f, fade(COLUMN_COLORS[2], alpha));
        icon(u, ICON_CHECK, {ck.center_x(), ck.center_y()}, 12.0f,
             fade(color{10, 40, 20, 255}, alpha));
    }
    {
        text_scope ts = u.text_style(14.0f, s.bold);
        u.text_ellipsis(top, c.title, fade(th.text, alpha), ALIGN_LEFT);
    }
    (void)in.cut_top(6.0f);
    rect mid = in.cut_top(20.0f);
    {
        text_scope ts = u.text_style(12.0f);
        const f32 tw = u.text_width(TAG_NAMES[c.tag]) + 16.0f;
        const rect chip = mid.cut_left(tw);
        u.draw_rounded_rect(chip, fade(TAG_COLORS[c.tag], 0.22f * alpha), 10.0f);
        u.text(chip, TAG_NAMES[c.tag], fade(mixc(TAG_COLORS[c.tag], th.text, 0.35f), alpha),
               ALIGN_CENTER);
    }
    for (i32 a = 0; a < 3; ++a) // a little crew of avatars
    {
        const vec2 ac{mid.right() - 10.0f - static_cast<f32>(a) * 13.0f, mid.center_y()};
        fill_circle(u, ac, 9.5f, fade(bg, alpha));
        fill_circle(u, ac, 8.0f,
                    fade(hsv(hash01(c.id * 7u + static_cast<u32>(a)), 0.5f, 0.95f), alpha));
    }
    if (r.h > CARD_H + 20.0f)
    {
        const rect body = rect::make(in.x, in.y + 4.0f, in.w, max2(0.0f, in.h - 18.0f));
        text_scope ts = u.text_style(13.0f);
        u.text_wrapped(
            body,
            "Opened: the cards below slid down to make room. Drag me to another column, or "
            "right-click for actions.",
            fade(th.text_dim, alpha));
    }
    const rect bar = in.bottom_slice(5.0f);
    const f32 pg = u.animate(id_child("bd_pg"_id, static_cast<uiid>(c.id)),
                             c.col == 2 ? 1.0f : c.progress, spring{120.0f, 0.8f});
    u.draw_rounded_rect(bar, fade(th.text, 0.1f * alpha), 2.5f);
    u.draw_rounded_rect(rect::make(bar.x, bar.y, bar.w * sat(pg), bar.h),
                        fade(c.col == 2 ? COLUMN_COLORS[2] : s.accent_c, alpha), 2.5f);
}

static f32 page_board(ui &u, showcase &s, rect area, vec2 origin)
{
    const theme &th = u.th();
    const f64 now = u.ctx->now;
    column col(area, 16.0f);
    const bool narrow = area.w < 760.0f;

    // header: the add form drops below the title on a narrow page
    {
        const rect r =
            card(u, s, "bd_head"_id, col.next(narrow ? 136.0f : 88.0f), origin, enter_k(s, 0));
        rect in = r.pad(20.0f, 16.0f);
        rect left = in, form{};
        if (narrow)
        {
            left = in.cut_top(44.0f);
            (void)in.cut_top(6.0f);
            form = in;
        }
        else
        {
            form = in.cut_right(min2(440.0f, in.w * 0.52f));
            left = in;
        }
        card_title(u, left, "Launch board", "drag cards between columns - right-click for actions");
        rect fr = rect::make(form.x, form.center_y() - 18.0f, form.w, 36.0f);
        const rect add_r = fr.cut_right(96.0f);
        (void)fr.cut_right(8.0f);
        const uiid fid = "bd_field"_id;
        const bool was_focused = u.ctx->focus == fid;
        (void)u.text_field(fr, s.draft, fid, field_opts{.radii = some(corner_radii::all(18.0f))});
        if (s.draft.empty() && u.ctx->focus != fid)
            u.text(fr.pad(14.0f, 0.0f), "Name a new mission...", th.text_dim, ALIGN_LEFT);
        bool submit =
            pill_button(u, add_r, "Add", "bd_add"_id, {.icon = ICON_PLUS, .primary = true});
        if (was_focused && u.key_pressed(key::ENTER)) submit = true;
        if (submit)
        {
            add_card(s, s.draft.empty() ? "Untitled mission" : s.draft.c_str(), 0);
            s.draft.clear();
            notify(u, s, "Added a card to Backlog", 0);
        }
    }

    // geometry: three equal columns
    const f32 gap = 16.0f;
    const f32 col_w = max2(60.0f, (area.w - gap * 2.0f) / 3.0f);
    const f32 board_y = col.remaining().y;
    const f32 inner_w = col_w - 24.0f;
    const auto col_x = [&](i32 c) { return area.x + static_cast<f32>(c) * (col_w + gap); };
    const f32 body_y = board_y + COL_HEAD;

    // where would the dragged card drop? (column under the pointer, index by height)
    const board_card *dragged = nullptr;
    for (const board_card &c : s.cards)
        if (c.id == s.drag_id) dragged = &c;
    const bool live = dragged && s.drag_live;
    s.drop_col = -1;
    s.drop_index = -1;
    if (live)
    {
        const f32 mx = u.ctx->mouse_x;
        s.drop_col = 0;
        for (i32 c = 0; c < 3; ++c)
            if (mx >= col_x(c) - gap * 0.5f) s.drop_col = c;
        const f32 probe_y = u.ctx->mouse_y - s.grab.y + CARD_H * 0.5f - body_y;
        f32 y = 0.0f;
        i32 idx = 0;
        for (const board_card &c : s.cards)
        {
            if (c.col != s.drop_col || c.id == s.drag_id) continue;
            const f32 h = card_height(s, c, now);
            if (y + h * 0.5f < probe_y) idx += 1;
            y += h + CARD_GAP;
        }
        s.drop_index = idx;
    }

    // local slots (placeholder included) and the column heights
    std::vector<rect> slot(s.cards.size());
    f32 ys[3] = {0.0f, 0.0f, 0.0f};
    i32 counts[3] = {0, 0, 0};
    bool ph_placed = false;
    rect ph{};
    const f32 drag_h = dragged ? card_height(s, *dragged, now) : CARD_H;
    for (usize i = 0; i < s.cards.size(); ++i)
    {
        const board_card &c = s.cards[i];
        if (live && c.id == s.drag_id) continue;
        if (live && c.col == s.drop_col && counts[c.col] == s.drop_index && !ph_placed)
        {
            ph = rect::make(0.0f, ys[c.col], inner_w, drag_h);
            ys[c.col] += drag_h + CARD_GAP;
            ph_placed = true;
        }
        const f32 h = card_height(s, c, now);
        const f32 k = c.dying >= 0.0 ? h / CARD_H : 1.0f;
        slot[i] = rect::make(0.0f, ys[c.col], inner_w, h);
        ys[c.col] += h + CARD_GAP * min2(1.0f, k);
        counts[c.col] += 1;
    }
    if (live && !ph_placed && s.drop_col >= 0)
    {
        ph = rect::make(0.0f, ys[s.drop_col], inner_w, drag_h);
        ys[s.drop_col] += drag_h + CARD_GAP;
        ph_placed = true;
    }
    f32 board_h = 380.0f;
    for (f32 y : ys) board_h = max2(board_h, COL_HEAD + y + 14.0f);
    (void)col.next(board_h);

    // columns. A "+" click is applied after the card loop: `slot` was sized from
    // the cards above, so the list must not grow while it is being drawn.
    i32 add_to_col = -1;
    for (i32 c = 0; c < 3; ++c)
    {
        const uiid ck = id_child("bd_col"_id, static_cast<uiid>(c));
        const rect target = rect::make(col_x(c), board_y, col_w, board_h);
        const f32 k = enter_k(s, 1 + c);
        const rect cr = card(u, s, ck, target, origin, k, 20.0f);
        const f32 hl =
            u.smooth(id_child(ck, "hl"_id), (live && s.drop_col == c) ? 1.0f : 0.0f, 0.06f);
        if (hl > 0.01f)
        {
            glass_glow(u, {cr.center_x(), cr.y + 30.0f}, cr.w * 0.8f,
                       fade(COLUMN_COLORS[c], 0.25f * hl));
            outline(u, cr, 20.0f, fade(COLUMN_COLORS[c], 0.8f * hl), 1.5f);
        }
        rect head = rect::make(cr.x + 14.0f, cr.y + 10.0f, max2(0.0f, cr.w - 28.0f), 32.0f);
        fill_circle(u, {head.x + 6.0f, head.center_y()}, 5.0f, COLUMN_COLORS[c]);
        const rect add_r = head.cut_right(30.0f);
        i32 live_count = 0;
        for (const board_card &bc : s.cards)
            if (bc.col == c && bc.dying < 0.0) live_count += 1;
        const f32 shown =
            u.animate(id_child(ck, "n"_id), static_cast<f32>(live_count), spring{300.0f, 0.45f});
        const f32 bump = 1.0f + 0.45f * min2(1.0f, std::fabs(shown - static_cast<f32>(live_count)));
        {
            text_scope ts = u.text_style(15.0f, s.bold);
            const f32 tw = u.text_width(COLUMN_NAMES[c]);
            u.text(rect::make(head.x + 18.0f, head.y, tw + 4.0f, head.h), COLUMN_NAMES[c], th.text,
                   ALIGN_LEFT);
            const rect badge =
                rect::make(head.x + 28.0f + tw, head.center_y() - 10.0f, 28.0f, 20.0f);
            u.draw_rounded_rect(scaled(badge, bump, bump), fade(COLUMN_COLORS[c], 0.25f), 10.0f);
            text_scope ts2 = u.text_style(12.0f, s.bold);
            u.textf(badge, th.text, ALIGN_CENTER, "%d", static_cast<i32>(shown + 0.5f));
        }
        if (icon_button(u, add_r, ICON_PLUS, id_child(ck, "add"_id), "Add a card here"))
            add_to_col = c;
        if (ph_placed && s.drop_col == c) // the gap the dragged card will land in
        {
            const rect pr = u.animate_rect("bd_ph"_id, shifted(ph, cr.x + 12.0f, body_y),
                                           {.spec = spring{320.0f, 0.85f}, .origin = origin});
            const f32 p = 0.5f + 0.5f * std::sin(s.clock * 5.0f);
            u.draw_rounded_rect(pr, fade(COLUMN_COLORS[c], 0.08f + 0.05f * p), 14.0f);
            outline(u, pr, 14.0f, fade(COLUMN_COLORS[c], 0.45f + 0.25f * p), 1.5f);
        }
    }

    // cards (the dragged one is drawn last, on top)
    static const char *const menu[5] = {"Move to Backlog", "Move to In flight", "Move to Shipped",
                                        "Duplicate", "Delete"};
    u32 menu_card = 0;
    i32 menu_pick = -1;
    rect drag_r{};
    for (usize i = 0; i < s.cards.size(); ++i)
    {
        const board_card &c = s.cards[i];
        const uiid key = id_child("card"_id, static_cast<uiid>(c.id));
        const bool is_drag = live && c.id == s.drag_id;
        rect r{};
        if (is_drag)
        {
            r = rect::make(u.ctx->mouse_x - s.grab.x, u.ctx->mouse_y - s.grab.y, inner_w,
                           card_height(s, c, now));
            (void)u.animate_rect(key, r, {.origin = origin, .snap = true});
            drag_r = r;
        }
        else
        {
            const rect target = shifted(slot[i], col_x(c.col) + 12.0f, body_y);
            r = u.animate_rect(key, target, {.spec = spring{260.0f, 0.78f}, .origin = origin});
            r.y += (1.0f - enter_k(s, 1 + c.col)) * 28.0f;
        }
        const interaction it = u.interact(key, r, !s.blocked && c.dying < 0.0, false);
        if (it.hovered && !s.drag_live) u.set_cursor(CURSOR_HAND);
        if (it.activated)
        {
            s.drag_id = c.id;
            s.drag_live = false;
            s.grab = {u.ctx->mouse_x - r.x, u.ctx->mouse_y - r.y};
            s.press = pointer(u);
        }
        if (it.clicked && !s.drag_live) s.expanded = s.expanded == c.id ? 0 : c.id;
        const f32 hv = u.smooth(id_child(key, "hv"_id), it.hovered ? 1.0f : 0.0f, 0.06f);
        const f32 a = c.dying >= 0.0 ? 1.0f - sat(static_cast<f32>(now - c.dying) / 0.3f) : 1.0f;
        if (!is_drag) board_card_draw(u, s, c, r, hv, 0.0f, a);
        if (!is_drag && c.dying < 0.0 && !s.blocked && s.drag_id == 0)
        {
            const i32 pick = u.context_menu(id_child(key, "menu"_id), r, menu, 5);
            if (pick >= 0)
            {
                menu_card = c.id;
                menu_pick = pick;
            }
        }
    }
    if (live && dragged)
    {
        // lifted: bigger shadow, a little scale, and a tilt-like sway from the pointer's speed
        const f32 sway =
            u.smooth("bd_sway"_id,
                     (u.ctx->mouse_x - u.smooth("bd_mx"_id, u.ctx->mouse_x, 0.08f)) * 0.02f, 0.05f);
        const rect lifted = shifted(scaled(drag_r, 1.04f, 1.04f), sway * 6.0f, -4.0f);
        board_card_draw(u, s, *dragged, lifted, 1.0f, 1.0f, 1.0f);
        u.set_cursor(CURSOR_HAND);
        u.request_redraw();
    }

    // drag state machine: arm on press, go live past a 5 px threshold, drop on release
    if (s.drag_id != 0)
    {
        const f32 dx = u.ctx->mouse_x - s.press.x, dy = u.ctx->mouse_y - s.press.y;
        if (u.ctx->mouse_down && !s.drag_live && dx * dx + dy * dy > 25.0f) s.drag_live = true;
        if (!u.ctx->mouse_down)
        {
            if (s.drag_live && dragged && s.drop_col >= 0)
            {
                board_card moved = *dragged;
                const i32 from = moved.col;
                std::erase_if(s.cards, [&](const board_card &c) { return c.id == moved.id; });
                moved.col = s.drop_col;
                usize pos = s.cards.size();
                i32 seen = 0;
                bool any = false;
                usize last = 0;
                for (usize i = 0; i < s.cards.size(); ++i)
                {
                    if (s.cards[i].col != s.drop_col) continue;
                    if (seen == s.drop_index)
                    {
                        pos = i;
                        break;
                    }
                    seen += 1;
                    any = true;
                    last = i;
                }
                if (pos == s.cards.size() && any) pos = last + 1;
                s.cards.insert(s.cards.begin() + static_cast<std::ptrdiff_t>(pos), moved);
                if (moved.col == 2 && from != 2)
                {
                    burst(s, pointer(u), 90, 700.0f);
                    char t[96];
                    std::snprintf(t, sizeof(t), "Shipped: %s", moved.title);
                    notify(u, s, t, 1);
                }
            }
            s.drag_id = 0;
            s.drag_live = false;
        }
    }

    if (menu_pick >= 0)
    {
        for (usize i = 0; i < s.cards.size(); ++i)
        {
            board_card &c = s.cards[i];
            if (c.id != menu_card) continue;
            if (menu_pick <= 2 && c.col != menu_pick)
            {
                // move to the end of the target column
                board_card moved = c;
                moved.col = menu_pick;
                s.cards.erase(s.cards.begin() + static_cast<std::ptrdiff_t>(i));
                usize pos = s.cards.size();
                for (usize j = 0; j < s.cards.size(); ++j)
                    if (s.cards[j].col == menu_pick) pos = j + 1;
                s.cards.insert(s.cards.begin() + static_cast<std::ptrdiff_t>(pos), moved);
                if (menu_pick == 2) notify(u, s, "Shipped from the context menu", 1);
            }
            else if (menu_pick == 3)
            {
                board_card copy = c;
                copy.id = s.next_card++;
                s.cards.insert(s.cards.begin() + static_cast<std::ptrdiff_t>(i + 1), copy);
            }
            else if (menu_pick == 4)
            {
                c.dying = now;
                notify(u, s, "Card deleted", 2);
            }
            break;
        }
    }
    if (add_to_col >= 0)
    {
        static const char *const names[5] = {"Antenna sweep", "Hull inspection", "Supply manifest",
                                             "Crew briefing", "Comms check"};
        add_card(s, names[s.next_card % 5], add_to_col);
    }
    std::erase_if(s.cards,
                  [&](const board_card &c) { return c.dying >= 0.0 && now - c.dying > 0.32; });
    return col.remaining().y - area.y;
}

// ============================================================ page: effects
static void fx_lens(ui &u, showcase &s, rect in)
{
    const theme &th = u.th();
    const rect slider = in.cut_bottom(44.0f);
    (void)in.cut_bottom(8.0f);
    const rect stage = in;
    u.draw_rounded_rect(stage, fade(color{0, 0, 10, 255}, 0.25f - 0.15f * s.light), 16.0f);
    {
        region rg = u.region(stage, "fx_lens_stage"_id);
        const f32 t = s.clock;
        glass_orb(
            u, {stage.x + stage.w * (0.25f + 0.1f * std::sin(t * 0.7f)), stage.y + stage.h * 0.4f},
            stage.h * 0.22f, color{255, 220, 120, 255}, color{255, 110, 80, 255});
        glass_orb(u,
                  {stage.x + stage.w * 0.7f,
                   stage.y + stage.h * (0.55f + 0.15f * std::sin(t * 0.9f + 1.0f))},
                  stage.h * 0.26f, color{150, 255, 240, 255}, color{30, 160, 230, 255});
        glass_orb(u,
                  {stage.x + stage.w * (0.5f + 0.2f * std::sin(t * 0.5f + 2.0f)),
                   stage.y + stage.h * 0.82f},
                  stage.h * 0.16f, color{255, 150, 220, 255}, color{200, 60, 200, 255});
        {
            text_scope ts = u.text_style(28.0f, s.bold);
            u.text(rect::make(stage.x, stage.y + 10.0f, stage.w, 40.0f), "PUFFERUI",
                   fade(th.text, 0.85f), ALIGN_CENTER);
        }
        // the lens: drag anchor on the press edge (`activated`), not every frame
        const f32 lw = min2(160.0f, stage.w * 0.55f), lh = min2(100.0f, stage.h * 0.6f);
        const f32 tx = stage.x + s.lens.x * max2(0.0f, stage.w - lw);
        const f32 ty = stage.y + s.lens.y * max2(0.0f, stage.h - lh);
        const f32 lx = u.smooth("fx_lens_x"_id, tx, 0.035f),
                  ly = u.smooth("fx_lens_y"_id, ty, 0.035f);
        const rect lens = rect::make(lx, ly, lw, lh);
        const interaction it = u.interact("fx_lens"_id, lens, !s.blocked);
        if (it.hovered || it.pressed) u.set_cursor(CURSOR_HAND);
        if (it.activated) s.lens_anchor = {u.ctx->mouse_x - tx, u.ctx->mouse_y - ty};
        if (it.pressed)
        {
            s.lens.x = sat((u.ctx->mouse_x - s.lens_anchor.x - stage.x) / max2(1.0f, stage.w - lw));
            s.lens.y = sat((u.ctx->mouse_y - s.lens_anchor.y - stage.y) / max2(1.0f, stage.h - lh));
        }
        const f32 hv = u.smooth("fx_lens_hv"_id, (it.hovered || it.pressed) ? 1.0f : 0.0f, 0.06f);
        soft_shadow(u, lens, 18.0f, 10.0f + 8.0f * hv, 0.7f, 6.0f);
        glass_panel(u, lens, 18.0f, 1.0f, s.lens_blur);
        u.text(lens, it.pressed ? "drop me" : "drag me", fade(color::white(), 0.85f + 0.15f * hv),
               ALIGN_CENTER);
    }
    (void)fancy_slider(u, slider, "Blur radius", s.lens_blur, 1.0f, 40.0f, "fx_blur"_id, "%.0f");
}

static void fx_ripples(ui &u, showcase &s, rect in)
{
    const theme &th = u.th();
    column c(rect::make(in.center_x() - min2(110.0f, in.w * 0.5f), in.y + 8.0f, min2(220.0f, in.w),
                        in.h),
             14.0f);
    if (pill_button(u, c.next(42.0f), "Ripple", "fx_ripple"_id))
        notify(u, s, "Ripples start where you pressed", 0);
    const rect cb = c.next(42.0f);
    if (pill_button(u, cb, "Confetti", "fx_confetti"_id, {.icon = ICON_SPARK, .primary = true}))
        burst(s, {cb.center_x(), cb.y}, 120, 820.0f);
    if (pill_button(u, c.next(42.0f), "Fireworks", "fx_fireworks"_id, {.icon = ICON_ROCKET}))
        for (i32 i = 0; i < 3; ++i)
        {
            s.fireworks_at[i] = u.ctx->now + 0.25 * static_cast<f64>(i);
            s.fireworks_pos[i] = {in.x + in.w * (0.2f + 0.6f * frand(s)),
                                  in.y + in.h * (0.2f + 0.3f * frand(s))};
        }
    text_scope ts = u.text_style(13.0f);
    u.text_wrapped(c.next(60.0f),
                   "Every pill ripples from the press point inside a clip region; confetti is a "
                   "particle list stepped with dt.",
                   th.text_dim);
}

static void fx_radial(ui &u, showcase &s, rect in)
{
    const theme &th = u.th();
    static const char *const names[6] = {"Launch", "Spark", "Layers", "Board", "Wave", "Tune"};
    static const i32 icons[6] = {ICON_ROCKET, ICON_SPARK, ICON_LAYERS,
                                 ICON_BOARD,  ICON_WAVE,  ICON_GEAR};
    const vec2 c{in.center_x(), in.center_y() + 6.0f};
    const f32 R0 = 30.0f;
    const f32 R1 = max2(R0 + 30.0f, min2(118.0f, min2(in.w, in.h) * 0.5f - 6.0f));
    const f32 open =
        u.animate("fx_radial_k"_id, s.radial_open ? 1.0f : 0.0f, spring{230.0f, 0.62f});
    const rect wheel =
        rect::make(c.x - R1 - 10.0f, c.y - R1 - 10.0f, (R1 + 10.0f) * 2.0f, (R1 + 10.0f) * 2.0f);
    const interaction it = u.interact("fx_wheel"_id, wheel, !s.blocked);
    const f32 dx = u.ctx->mouse_x - c.x, dy = u.ctx->mouse_y - c.y;
    const f32 dist = std::sqrt(dx * dx + dy * dy);
    const bool center_hot = it.hovered && dist <= R0 + 2.0f;
    i32 hot = -1;
    if (it.hovered && open > 0.5f && dist > R0 + 4.0f && dist < R1 + 14.0f)
    {
        f32 a = std::atan2(dy, dx) + PI * 0.5f + PI / 6.0f;
        a -= std::floor(a / (2.0f * PI)) * 2.0f * PI;
        hot = static_cast<i32>(a / (2.0f * PI / 6.0f)) % 6;
    }
    if (it.focused && open > 0.5f)
    {
        if (u.key_pressed(key::RIGHT)) s.radial_kb = (s.radial_kb + 1) % 6;
        if (u.key_pressed(key::LEFT)) s.radial_kb = (s.radial_kb + 5) % 6;
        if (hot < 0) hot = s.radial_kb;
    }
    if (center_hot || hot >= 0) u.set_cursor(CURSOR_HAND);
    glass_glow(u, c, R1 * 1.2f, fade(s.accent_c, 0.18f + 0.2f * sat(open)));
    for (i32 i = 0; i < 6; ++i)
    {
        const f32 ki =
            clampf(open * 1.5f - static_cast<f32>(i) * 0.09f, 0.0f, 1.2f); // a staggered fan-out
        if (ki <= 0.01f) continue;
        const f32 mid = -PI * 0.5f + static_cast<f32>(i) * PI / 3.0f;
        const f32 half = (PI / 6.0f - 0.05f) * sat(ki);
        const f32 hv =
            u.smooth(id_child("fx_rad_hv"_id, static_cast<uiid>(i)), hot == i ? 1.0f : 0.0f, 0.05f);
        const f32 outer = R0 + 8.0f + (R1 - R0 - 8.0f) * ki + 8.0f * hv;
        const color col = hsv(static_cast<f32>(i) / 6.0f + 0.62f, 0.5f - 0.15f * hv, 0.95f,
                              static_cast<u8>(170.0f * sat(ki) + 70.0f * hv));
        u.draw_sector(c, R0 + 8.0f, outer, mid - half, mid + half, col);
        const f32 ir = (R0 + 8.0f + outer) * 0.5f;
        icon(u, icons[i], {c.x + std::cos(mid) * ir, c.y + std::sin(mid) * ir}, 18.0f + 3.0f * hv,
             fade(color::white(), sat(ki)));
    }
    const f32 ch = u.smooth("fx_rad_c"_id, center_hot ? 1.0f : 0.0f, 0.05f);
    fill_circle(u, c, R0 + 3.0f * ch, s.accent_c);
    icon(u, ICON_PLUS, c, 22.0f, color::white(), open * PI * 0.25f);
    if (it.focused) u.draw_arc(c, R0 + 6.0f, 1.5f, 0.0f, 2.0f * PI, th.focus_border);
    {
        text_scope ts = u.text_style(13.0f, s.bold);
        const char *label = hot >= 0 ? names[hot] : (s.radial_open ? "pick one" : "click the hub");
        u.text(rect::make(in.x, in.y, in.w, 18.0f), label, hot >= 0 ? th.text : th.text_dim,
               ALIGN_CENTER);
    }
    const bool kb = kb_hit(u, it);
    if (it.clicked || kb)
    {
        if (hot >= 0 && s.radial_open && !center_hot)
        {
            char t[64];
            std::snprintf(t, sizeof(t), "Radial pick: %s", names[hot]);
            notify(u, s, t, 0);
            const f32 mid = -PI * 0.5f + static_cast<f32>(hot) * PI / 3.0f;
            burst(s, {c.x + std::cos(mid) * R1 * 0.7f, c.y + std::sin(mid) * R1 * 0.7f}, 30,
                  420.0f);
            s.radial_open = false;
        }
        else
            s.radial_open = !s.radial_open;
    }
}

static void fx_magnet_tilt(ui &u, showcase &s, rect in)
{
    const theme &th = u.th();
    rect left = in.cut_left(in.w * 0.5f);
    const rect right = in;
    // magnetic: the button leans toward a nearby pointer
    {
        const vec2 c{left.center_x(), left.center_y()};
        const f32 dx = u.ctx->mouse_x - c.x, dy = u.ctx->mouse_y - c.y;
        const f32 dist = std::sqrt(dx * dx + dy * dy);
        const f32 field = 120.0f;
        const bool near = dist < field && !s.blocked;
        const f32 ox =
            u.smooth("fx_mag_x"_id, near ? clampf(dx * 0.35f, -34.0f, 34.0f) : 0.0f, 0.07f);
        const f32 oy =
            u.smooth("fx_mag_y"_id, near ? clampf(dy * 0.35f, -34.0f, 34.0f) : 0.0f, 0.07f);
        const f32 pull = u.smooth("fx_mag_p"_id, near ? 1.0f - dist / field : 0.0f, 0.08f);
        for (i32 i = 0; i < 3; ++i)
            u.draw_arc(
                c, 40.0f + static_cast<f32>(i) * 22.0f, 1.0f, 0.0f, 2.0f * PI,
                fade(s.accent_c, (0.08f + 0.25f * pull) * (1.0f - static_cast<f32>(i) * 0.3f)));
        const rect b = rect::make(c.x - 62.0f + ox, c.y - 20.0f + oy, 124.0f, 40.0f);
        if (pill_button(u, b, "Magnetic", "fx_magnet"_id, {.primary = true}))
            burst(s, c, 40, 500.0f);
    }
    // tilt: a pseudo-3D card, corners pushed along the pointer offset
    {
        const vec2 c{right.center_x(), right.center_y()};
        const f32 hw = min2(84.0f, right.w * 0.42f), hh = min2(60.0f, right.h * 0.36f);
        const rect hit =
            rect::make(c.x - hw - 10.0f, c.y - hh - 10.0f, hw * 2.0f + 20.0f, hh * 2.0f + 20.0f);
        const interaction it = u.interact("fx_tilt"_id, hit, !s.blocked, false);
        const f32 nx =
            u.smooth("fx_tilt_x"_id,
                     it.hovered ? clampf((u.ctx->mouse_x - c.x) / hw, -1.0f, 1.0f) : 0.0f, 0.08f);
        const f32 ny =
            u.smooth("fx_tilt_y"_id,
                     it.hovered ? clampf((u.ctx->mouse_y - c.y) / hh, -1.0f, 1.0f) : 0.0f, 0.08f);
        const f32 hv = u.smooth("fx_tilt_hv"_id, it.hovered ? 1.0f : 0.0f, 0.08f);
        const f32 sx[4] = {-1.0f, 1.0f, 1.0f, -1.0f}, sy[4] = {-1.0f, -1.0f, 1.0f, 1.0f};
        vec2 q[4], sh[4];
        f32 z[4];
        for (i32 i = 0; i < 4; ++i)
        {
            z[i] = (sx[i] * nx + sy[i] * ny) * 0.12f;
            q[i] = {c.x + sx[i] * hw * (1.0f + z[i]) + nx * 4.0f,
                    c.y + sy[i] * hh * (1.0f + z[i]) + ny * 4.0f};
            sh[i] = {q[i].x - nx * 10.0f, q[i].y - ny * 10.0f + 14.0f};
        }
        u.draw_polygon(sh, fade(color{0, 0, 20, 255}, 0.25f + 0.1f * hv));
        vertex v[4];
        for (i32 i = 0; i < 4; ++i)
        {
            const color base = hsv(0.72f + 0.08f * static_cast<f32>(i), 0.6f, 0.9f);
            v[i] = {q[i].x, q[i].y, 0.0f, 0.0f, mixc(base, color::white(), sat(z[i] * 3.0f))};
        }
        const i32 idx[6] = {0, 1, 2, 0, 2, 3};
        u.draw_triangles(nullptr, v, 4, idx, 6);
        {
            region rg = u.region(hit, "fx_tilt_rg"_id); // the glare stays near the card
            glass_glow(u, {c.x + nx * hw * 0.8f, c.y + ny * hh * 0.8f}, 70.0f,
                       fade(color::white(), 0.35f * hv));
        }
        {
            text_scope ts = u.text_style(20.0f, s.bold);
            u.text(rect::make(c.x - hw + nx * 10.0f, c.y - 16.0f + ny * 8.0f, hw * 2.0f, 24.0f),
                   "Tilt", color::white(), ALIGN_CENTER);
        }
        text_scope ts = u.text_style(12.0f);
        u.text(rect::make(c.x - hw + nx * 5.0f, c.y + 8.0f + ny * 4.0f, hw * 2.0f, 18.0f),
               "hover me", fade(color::white(), 0.8f), ALIGN_CENTER);
        (void)th;
    }
}

static void shimmer_bar(ui &u, rect bar, f32 band_x, color base, color hi)
{
    u.draw_rounded_rect(bar, base, min2(6.0f, bar.h * 0.5f));
    const f32 half = 70.0f;
    const f32 x0 = max2(bar.x, band_x - half), x1 = min2(bar.right(), band_x + half);
    if (x1 <= x0) return;
    const auto at = [&](f32 x) { return fade(hi, 1.0f - std::fabs(x - band_x) / half); };
    const f32 xm = clampf(band_x, x0, x1);
    gradient_quad(u, rect::make(x0, bar.y + 1.0f, xm - x0, bar.h - 2.0f), at(x0), at(xm), at(xm),
                  at(x0));
    gradient_quad(u, rect::make(xm, bar.y + 1.0f, x1 - xm, bar.h - 2.0f), at(xm), at(x1), at(x1),
                  at(xm));
}

static void fx_shimmer(ui &u, showcase &s, rect in)
{
    const theme &th = u.th();
    const rect btn = in.cut_bottom(40.0f);
    (void)in.cut_bottom(8.0f);
    if (pill_button(u, rect::make(btn.x, btn.y, min2(160.0f, btn.w), btn.h),
                    s.loaded ? "Reset" : "Load", "fx_load"_id, {.primary = !s.loaded}))
        s.loaded = !s.loaded;
    const f32 k =
        u.animate("fx_load_k"_id, s.loaded ? 1.0f : 0.0f, tween{0.5f, easing::EASE_IN_OUT});
    const f32 band = in.x - 80.0f + (s.clock * 0.8f - std::floor(s.clock * 0.8f)) * (in.w + 160.0f);
    static const char *const who[3] = {"Ada Lovelace", "Grace Hopper", "Katherine Johnson"};
    static const char *const role[3] = {"Flight software", "Compilers & tooling",
                                        "Trajectory analysis"};
    column c(in, 12.0f);
    for (i32 i = 0; i < 3; ++i)
    {
        rect r = c.next(44.0f);
        const vec2 av{r.x + 22.0f, r.center_y()};
        const rect body = r.pad(56.0f, 4.0f, 0.0f, 4.0f);
        const color base = fade(th.text, 0.08f * (1.0f - k));
        const color hi = fade(color::white(), 0.22f * (1.0f - k));
        if (k < 0.99f)
        {
            shimmer_bar(u, rect::make(av.x - 20.0f, av.y - 20.0f, 40.0f, 40.0f), band, base, hi);
            shimmer_bar(u, rect::make(body.x, body.y + 4.0f, body.w * 0.62f, 12.0f), band, base,
                        hi);
            shimmer_bar(u, rect::make(body.x, body.y + 22.0f, body.w * 0.38f, 10.0f), band, base,
                        hi);
        }
        if (k > 0.01f)
        {
            const f32 ki = sat(k * 1.6f - static_cast<f32>(i) * 0.25f);
            const color hc = hsv(0.08f + 0.27f * static_cast<f32>(i), 0.55f, 0.95f);
            fill_circle(u, av, 20.0f * (0.7f + 0.3f * ki), fade(hc, ki));
            text_scope ts = u.text_style(14.0f, s.bold);
            u.text(rect::make(body.x + (1.0f - ki) * 10.0f, body.y, body.w, 20.0f), who[i],
                   fade(th.text, ki), ALIGN_LEFT);
            text_scope ts2 = u.text_style(12.0f);
            u.text(rect::make(body.x + (1.0f - ki) * 16.0f, body.y + 18.0f, body.w, 18.0f), role[i],
                   fade(th.text_dim, ki), ALIGN_LEFT);
        }
    }
}

static void fx_waves(ui &u, showcase &s, rect in)
{
    const f32 t = s.clock;
    const rect waves = in.cut_top(in.h * 0.48f);
    const color cols[3] = {s.accent_c, color{255, 110, 180, 255}, color{60, 220, 210, 255}};
    for (i32 w = 0; w < 3; ++w)
    {
        vec2 pts[64];
        const f32 fw = static_cast<f32>(w);
        for (i32 i = 0; i < 64; ++i)
        {
            const f32 x = waves.x + waves.w * static_cast<f32>(i) / 63.0f;
            const f32 env = std::sin(PI * static_cast<f32>(i) / 63.0f); // pinned at both ends
            pts[i] = {x, waves.center_y() +
                             env * waves.h * 0.34f * (0.6f + 0.4f * std::sin(t * 0.7f + fw)) *
                                 std::sin(static_cast<f32>(i) * (0.16f + 0.05f * fw) +
                                          t * (1.6f + 0.5f * fw) + fw * 2.0f)};
        }
        stroke(u, pts, 64, fade(cols[w], 0.85f), 2.2f);
    }
    track_row tr(in, {track_size::flex(), track_size::flex(), track_size::flex()}, 8.0f);
    // pulse rings
    {
        const rect cell = tr.next();
        const vec2 c{cell.center_x(), cell.center_y()};
        for (i32 j = 0; j < 3; ++j)
        {
            f32 ph = t * 0.7f + static_cast<f32>(j) / 3.0f;
            ph -= std::floor(ph);
            u.draw_arc(c, 6.0f + ph * 40.0f, 2.0f * (1.0f - ph) + 0.5f, 0.0f, 2.0f * PI,
                       fade(cols[1], 1.0f - ph));
        }
        fill_circle(u, c, 6.0f, cols[1]);
    }
    // spinner: the arc's head runs ahead, its tail catches up
    {
        const rect cell = tr.next();
        const vec2 c{cell.center_x(), cell.center_y()};
        const f32 head = t * 5.0f, len = 0.5f + 1.1f * (0.5f + 0.5f * std::sin(t * 2.4f));
        u.draw_arc(c, 20.0f, 4.0f, 0.0f, 2.0f * PI, fade(u.th().text, 0.08f));
        u.draw_arc(c, 20.0f, 4.0f, head, head + len * PI, s.accent_c);
    }
    // morphing blob: a center fan over a noisy radius
    {
        const rect cell = tr.next();
        const vec2 c{cell.center_x(), cell.center_y()};
        const f32 R = min2(cell.w, cell.h) * 0.32f;
        constexpr i32 SEG = 48;
        vertex v[SEG + 2];
        i32 idx[SEG * 3];
        v[0] = {c.x, c.y, 0.0f, 0.0f, mixc(cols[2], color::white(), 0.5f)};
        for (i32 i = 0; i <= SEG; ++i)
        {
            const f32 a = 2.0f * PI * static_cast<f32>(i) / SEG;
            const f32 rr =
                R * (1.0f + 0.12f * std::sin(3.0f * a + t * 1.4f) +
                     0.08f * std::sin(5.0f * a - t * 1.9f) + 0.05f * std::sin(7.0f * a + t * 0.7f));
            v[i + 1] = {c.x + std::cos(a) * rr, c.y + std::sin(a) * rr, 0.0f, 0.0f,
                        mixc(cols[2], s.accent_c, 0.5f + 0.5f * std::sin(a + t))};
        }
        for (i32 i = 0; i < SEG; ++i)
        {
            idx[i * 3 + 0] = 0;
            idx[i * 3 + 1] = i + 1;
            idx[i * 3 + 2] = i + 2;
        }
        glass_glow(u, c, R * 2.0f, fade(cols[2], 0.3f));
        u.draw_triangles(nullptr, v, SEG + 2, idx, SEG * 3);
    }
}

static f32 page_effects(ui &u, showcase &s, rect area, vec2 origin)
{
    static const char *const titles[6] = {"Liquid lens",      "Ripples & particles",
                                          "Radial menu",      "Magnetic & tilt",
                                          "Shimmer skeleton", "Waves, pulses, blobs"};
    static const char *const captions[6] = {"drag the glass; u.blur samples what is drawn below it",
                                            "clipped ripples, confetti and fireworks",
                                            "sectors fan out on staggered springs",
                                            "a pointer field and a pseudo-3D quad",
                                            "a moving highlight until the data lands",
                                            "strokes, rings and a center-fan blob"};
    const f32 cell_h = 330.0f;
    const grid_cursor gc =
        auto_fit_grid(rect::make(area.x, area.y, area.w, 100000.0f), 6, 340.0f, cell_h, 16.0f);
    for (i32 i = 0; i < 6; ++i)
    {
        const rect r =
            card(u, s, id_child("fx"_id, static_cast<uiid>(i)), gc.cell(i), origin, enter_k(s, i));
        rect in = r.pad(20.0f, 16.0f);
        card_title(u, in.cut_top(40.0f), titles[i], captions[i]);
        (void)in.cut_top(10.0f);
        switch (i)
        {
        case 0:
            fx_lens(u, s, in);
            break;
        case 1:
            fx_ripples(u, s, in);
            break;
        case 2:
            fx_radial(u, s, in);
            break;
        case 3:
            fx_magnet_tilt(u, s, in);
            break;
        case 4:
            fx_shimmer(u, s, in);
            break;
        default:
            fx_waves(u, s, in);
            break;
        }
    }
    return static_cast<f32>(gc.rows()) * (cell_h + 16.0f) - 16.0f;
}

// ============================================================ page: motion
static void spring_curve(const showcase &s, rect plot, f32 lo, f32 hi, f32 T, vec2 *out, i32 n)
{
    // the same integrator as u.animate(..., spring{}) - the plot is the truth
    f32 x = 0.0f, v = 0.0f;
    const f32 m = max2(s.mass, 0.001f);
    const f32 d = 2.0f * s.damp * std::sqrt(s.stiff * m);
    const f32 dt = T / static_cast<f32>(n - 1);
    for (i32 i = 0; i < n; ++i)
    {
        out[i] = {plot.x + plot.w * static_cast<f32>(i) / static_cast<f32>(n - 1),
                  plot.bottom() - (clampf(x, lo, hi) - lo) / (hi - lo) * plot.h};
        for (i32 k = 0; k < 8; ++k)
        {
            const f32 h = dt / 8.0f;
            const f32 acc = (s.stiff * (1.0f - x) - d * v) / m;
            v += acc * h;
            x += v * h;
        }
    }
}

static f32 page_motion(ui &u, showcase &s, rect area, vec2 origin)
{
    const theme &th = u.th();
    column col(area, 16.0f);
    const bool wide = area.w >= 880.0f;

    // spring lab
    {
        const rect r =
            card(u, s, "mo_spring"_id, col.next(wide ? 340.0f : 580.0f), origin, enter_k(s, 0));
        rect in = r.pad(20.0f, 16.0f);
        rect head = in.cut_top(40.0f);
        const rect sw = head.cut_right(min2(200.0f, head.w * 0.4f));
        (void)comp::switch_toggle(u, sw.pad(0.0f, 6.0f), "Reduced motion", s.reduced_motion,
                                  {.id = "mo_reduced"_id});
        card_title(u, head, "Spring lab",
                   "u.animate(key, target, spring{stiffness, damping, mass})");
        (void)in.cut_top(10.0f);
        rect ctrl = wide ? in.cut_left(300.0f) : in.cut_top(250.0f);
        if (wide)
            (void)in.cut_left(24.0f);
        else
            (void)in.cut_top(12.0f);

        column cc(ctrl, 10.0f);
        static const char *const presets[4] = {"Gentle", "Wobbly", "Stiff", "Molasses"};
        if (comp::segmented(u, cc.next(32.0f), presets, s.preset, {.id = "mo_preset"_id}))
        {
            const f32 table[4][3] = {{120.0f, 1.0f, 1.0f},
                                     {180.0f, 0.3f, 1.0f},
                                     {520.0f, 0.85f, 1.0f},
                                     {60.0f, 1.2f, 3.0f}};
            s.stiff = table[s.preset][0];
            s.damp = table[s.preset][1];
            s.mass = table[s.preset][2];
        }
        (void)fancy_slider(u, cc.next(44.0f), "Stiffness", s.stiff, 20.0f, 600.0f, "mo_stiff"_id,
                           "%.0f");
        (void)fancy_slider(u, cc.next(44.0f), "Damping ratio", s.damp, 0.05f, 1.6f, "mo_damp"_id,
                           "%.2f");
        (void)fancy_slider(u, cc.next(44.0f), "Mass", s.mass, 0.2f, 4.0f, "mo_mass"_id, "%.1f");
        if (pill_button(u, cc.next(40.0f), s.kick ? "Kick back" : "Kick", "mo_kick"_id,
                        {.icon = ICON_ROCKET, .primary = true}))
        {
            s.kick = !s.kick;
            s.kick_at = u.ctx->now;
        }

        // the response plot
        const rect track_r = in.cut_bottom(46.0f);
        (void)in.cut_bottom(10.0f);
        const rect plot = in.pad(4.0f, 8.0f);
        if (plot.w > 20.0f && plot.h > 20.0f)
        {
            const f32 lo = -0.25f, hi = 1.75f, T = 2.5f;
            const f32 y0 = plot.bottom() - (0.0f - lo) / (hi - lo) * plot.h;
            const f32 y1 = plot.bottom() - (1.0f - lo) / (hi - lo) * plot.h;
            u.draw_rounded_rect(grow(plot, 4.0f), fade(th.text, 0.04f), 12.0f);
            u.draw_line(plot.x, y0, plot.right(), y0, fade(th.text, 0.15f), 1.0f);
            for (f32 x = plot.x; x < plot.right(); x += 12.0f) // dashed target line
                u.draw_line(x, y1, min2(x + 6.0f, plot.right()), y1, fade(s.accent_c, 0.6f), 1.0f);
            vec2 pts[150];
            spring_curve(s, plot, lo, hi, T, pts, 150);
            area_under(u, pts, 150, y0, fade(s.accent_c, 0.3f), fade(s.accent_c, 0.0f));
            stroke(u, pts, 150, mixc(s.accent_c, color::white(), 0.2f), 2.4f);
            const f32 el = static_cast<f32>(u.ctx->now - s.kick_at);
            if (el < T)
            {
                const i32 i = static_cast<i32>(clampf(el / T, 0.0f, 1.0f) * 149.0f);
                glass_glow(u, pts[i], 18.0f, fade(s.accent_c, 0.8f));
                fill_circle(u, pts[i], 4.5f, color::white());
                u.request_redraw();
            }
            text_scope ts = u.text_style(12.0f);
            u.text(rect::make(plot.right() - 120.0f, y1 - 18.0f, 120.0f, 16.0f), "target",
                   th.text_dim, ALIGN_RIGHT);
            u.textf(rect::make(plot.x + 4.0f, plot.y, 260.0f, 16.0f), th.text_dim, ALIGN_LEFT,
                    "step response, 0 - %.1f s", static_cast<double>(T));
        }
        // the live ball: squashes with its speed and leaves a fading trail
        {
            u.draw_rounded_rect(track_r.pad(0.0f, 18.0f), fade(th.text, 0.08f), 5.0f);
            const f32 x =
                u.animate("mo_ball"_id, s.kick ? 1.0f : 0.0f, spring{s.stiff, s.damp, s.mass});
            const f32 dt = max2(static_cast<f32>(u.ctx->dt), 1e-4f);
            const f32 vel = (x - s.ball_prev) / dt;
            s.ball_prev = x;
            const vec2 p{track_r.x + 18.0f + x * (track_r.w - 36.0f), track_r.center_y()};
            // one trail sample per 1/60 s of real time: the same length at any fps
            s.trail_acc += static_cast<f32>(u.ctx->dt);
            for (i32 n = 0; s.trail_acc >= 1.0f / 60.0f && n < 10; ++n)
            {
                s.trail_acc -= 1.0f / 60.0f;
                s.trail[s.trail_head] = p;
                s.trail_head = (s.trail_head + 1) % 10;
            }
            s.trail_acc = min2(s.trail_acc, 1.0f / 60.0f);
            for (i32 i = 0; i < 10; ++i)
            {
                const i32 j = (s.trail_head + i) % 10;
                fill_circle(u, s.trail[j], 4.0f + static_cast<f32>(i) * 0.9f,
                            fade(s.accent_c, 0.04f * static_cast<f32>(i)));
            }
            const f32 st = clampf(std::fabs(vel) * 0.06f, 0.0f, 0.4f);
            const rect ball =
                rect::make(p.x - 14.0f * (1.0f + st), p.y - 14.0f * (1.0f - st * 0.6f),
                           28.0f * (1.0f + st), 28.0f * (1.0f - st * 0.6f));
            glass_glow(u, p, 36.0f, fade(s.accent_c, 0.5f));
            gradient_round(u, ball, ball.h * 0.5f, mixc(s.accent_c, color::white(), 0.5f),
                           s.accent_c, mixc(s.accent_c, color::black(), 0.2f), s.accent_c);
        }
    }

    // easing gallery
    {
        static const easing curves[5] = {easing::LINEAR, easing::EASE_IN, easing::EASE_OUT,
                                         easing::EASE_IN_OUT, easing::EASE_OUT_BACK};
        static const char *const names[5] = {"LINEAR", "EASE_IN", "EASE_OUT", "EASE_IN_OUT",
                                             "EASE_OUT_BACK"};
        const f32 inner_w = area.w - 40.0f;
        const grid_cursor probe =
            auto_fit_grid(rect::make(0.0f, 0.0f, inner_w, 1000.0f), 5, 170.0f, 186.0f, 12.0f);
        const f32 h = 56.0f + static_cast<f32>(probe.rows()) * 198.0f;
        const rect r = card(u, s, "mo_ease"_id, col.next(h + 16.0f), origin, enter_k(s, 1));
        rect in = r.pad(20.0f, 16.0f);
        rect head = in.cut_top(40.0f);
        if (pill_button(u, head.cut_right(110.0f).pad(0.0f, 2.0f), "Replay", "mo_replay"_id,
                        {.icon = ICON_SHUFFLE}))
            s.curves_at = u.ctx->now;
        card_title(u, head, "Easing curves", "ease(curve, t) - the curves tween{} uses");
        (void)in.cut_top(10.0f);
        const grid_cursor gc = auto_fit_grid(in, 5, 170.0f, 186.0f, 12.0f);
        f32 ph = static_cast<f32>(u.ctx->now - s.curves_at);
        ph = std::fmod(ph, 2.4f);
        const f32 tt = sat(ph / 1.4f);
        u.request_redraw();
        for (i32 i = 0; i < 5; ++i)
        {
            rect cell = gc.cell(i);
            u.draw_rounded_rect(cell, fade(th.text, 0.05f), 14.0f);
            rect ci = cell.pad(12.0f);
            {
                text_scope ts = u.text_style(12.0f, s.bold);
                u.text(ci.cut_top(16.0f), names[i], th.text_dim, ALIGN_LEFT);
            }
            const rect bar = ci.cut_bottom(14.0f);
            (void)ci.cut_bottom(8.0f);
            const rect pl = ci.pad(4.0f, 14.0f, 4.0f, 4.0f);
            vec2 pts[48];
            const f32 lo = -0.15f, hi = 1.2f;
            for (i32 j = 0; j < 48; ++j)
            {
                const f32 x = static_cast<f32>(j) / 47.0f;
                pts[j] = {pl.x + pl.w * x,
                          pl.bottom() - (ease(curves[i], x) - lo) / (hi - lo) * pl.h};
            }
            stroke(u, pts, 48, fade(s.accent_c, 0.8f), 2.0f);
            const f32 e = ease(curves[i], tt);
            const vec2 dot{pl.x + pl.w * tt, pl.bottom() - (e - lo) / (hi - lo) * pl.h};
            u.draw_line(dot.x, pl.y, dot.x, pl.bottom(), fade(th.text, 0.12f), 1.0f);
            glass_glow(u, dot, 14.0f, fade(s.accent_c, 0.8f));
            fill_circle(u, dot, 4.0f, color::white());
            u.draw_rounded_rect(bar.pad(0.0f, 5.0f), fade(th.text, 0.08f), 2.0f);
            const f32 bx = bar.x + 7.0f + clampf(e, -0.1f, 1.1f) * (bar.w - 14.0f);
            u.draw_rounded_rect(rect::make(bx - 7.0f, bar.y, 14.0f, 14.0f), s.accent_c, 4.0f);
        }
    }

    // a cascade of springs: each dot flips a beat after its neighbor
    {
        const rect r = card(u, s, "mo_stagger"_id, col.next(200.0f), origin, enter_k(s, 2));
        rect in = r.pad(20.0f, 16.0f);
        rect head = in.cut_top(40.0f);
        if (pill_button(u, head.cut_right(120.0f).pad(0.0f, 2.0f), "Cascade", "mo_wave"_id,
                        {.icon = ICON_WAVE, .primary = true}))
        {
            s.wave = !s.wave;
            s.wave_at = u.ctx->now;
        }
        card_title(u, head, "Stagger", "the same spring per dot, retargeted 35 ms apart");
        const i32 n = 28;
        vec2 pts[28];
        const f32 since = static_cast<f32>(u.ctx->now - s.wave_at);
        for (i32 i = 0; i < n; ++i)
        {
            const bool on = since >= 0.035f * static_cast<f32>(i) ? s.wave : !s.wave;
            const f32 y = u.animate(id_child("mo_dot"_id, static_cast<uiid>(i)), on ? 1.0f : 0.0f,
                                    spring{260.0f, 0.4f});
            pts[i] = {in.x + 10.0f + (in.w - 20.0f) * static_cast<f32>(i) / static_cast<f32>(n - 1),
                      in.bottom() - 16.0f - y * (in.h - 40.0f)};
        }
        stroke(u, pts, n, fade(th.text, 0.18f), 1.5f);
        for (i32 i = 0; i < n; ++i)
            fill_circle(u, pts[i], 6.0f,
                        hsv(0.62f + 0.4f * static_cast<f32>(i) / static_cast<f32>(n), 0.55f, 1.0f));
        if (since < 2.0f) u.request_redraw();
    }

    // color morph: animate_color with two speeds makes a gradient that "pours"
    {
        static const color swatches[6] = {{132, 112, 255, 255}, {255, 92, 160, 255},
                                          {32, 196, 184, 255},  {255, 156, 56, 255},
                                          {72, 156, 255, 255},  {120, 220, 90, 255}};
        const rect r = card(u, s, "mo_color"_id, col.next(170.0f), origin, enter_k(s, 3));
        rect in = r.pad(20.0f, 16.0f);
        card_title(u, in.cut_top(40.0f), "Color",
                   "u.animate_color(key, target, tween{}) - two speeds, one gradient");
        (void)in.cut_top(10.0f);
        rect sw_row = in.cut_left(min2(6.0f * 40.0f, in.w * 0.5f));
        (void)in.cut_left(20.0f);
        row rw(sw_row, 8.0f);
        for (i32 i = 0; i < 6; ++i)
        {
            const rect cell = rw.next(32.0f);
            const uiid id = id_child("mo_sw"_id, static_cast<uiid>(i));
            const rect dot = rect::make(cell.x, cell.center_y() - 16.0f, 32.0f, 32.0f);
            const interaction it = u.interact(id, dot, !s.blocked);
            if (it.hovered) u.set_cursor(CURSOR_HAND);
            if (it.clicked || kb_hit(u, it)) s.swatch = i;
            const f32 k =
                u.animate(id_child(id, "k"_id), s.swatch == i ? 1.0f : 0.0f, spring{320.0f, 0.45f});
            fill_circle(u, {dot.center_x(), dot.center_y()}, 12.0f + 3.0f * k, swatches[i]);
            if (k > 0.01f)
                u.draw_arc({dot.center_x(), dot.center_y()}, 18.0f + 2.0f * k, 2.0f, 0.0f,
                           2.0f * PI, fade(swatches[i], sat(k)));
            if (it.focused)
                u.draw_arc({dot.center_x(), dot.center_y()}, 23.0f, 1.2f, 0.0f, 2.0f * PI,
                           th.focus_border);
        }
        const color fast =
            u.animate_color("mo_cfast"_id, swatches[s.swatch], tween{0.35f, easing::EASE_OUT});
        const color slow =
            u.animate_color("mo_cslow"_id, swatches[s.swatch], tween{1.1f, easing::EASE_IN_OUT});
        const rect blob = in.pad(0.0f, 4.0f);
        glass_glow(u, {blob.center_x(), blob.center_y()}, blob.w * 0.5f, fade(fast, 0.35f));
        gradient_round(u, blob, 22.0f, fast, slow, slow, fast);
    }
    return col.remaining().y - area.y;
}

// ============================================================ overlays
static void draw_notes(ui &u, showcase &s)
{
    const theme &th = u.th();
    const f32 k = u.animate("notes_k"_id, s.notes_open ? 1.0f : 0.0f, spring{340.0f, 0.75f});
    if (k <= 0.01f) return;
    const i32 n = s.history_count;
    const f32 h = 52.0f + static_cast<f32>(n > 0 ? n : 1) * 48.0f;
    const rect area =
        rect::make(s.bell_rect.right() - 320.0f, s.bell_rect.bottom() + 16.0f, 320.0f, h);
    // the panel grows out of the bell: scaled from its top-right corner
    const f32 kc = clampf(k, 0.0f, 1.1f);
    const rect shown = rect::make(area.right() - area.w * (0.6f + 0.4f * kc), area.y,
                                  area.w * (0.6f + 0.4f * kc), area.h * kc);
    if (s.notes_open)
    {
        popup_scope p =
            u.popup("notes"_id, area, POPUP_CLOSE_ON_CLICK_OUTSIDE | POPUP_CLOSE_ON_ESCAPE);
        if (p.close_requested) s.notes_open = false;
    }
    soft_shadow(u, shown, 18.0f, 16.0f, sat(k), 10.0f);
    glass_panel(u, shown, 18.0f, sat(k), 30.0f);
    region rg = u.region(shown, "notes_rg"_id);
    rect in = area.pad(16.0f, 12.0f);
    {
        text_scope ts = u.text_style(15.0f, s.bold);
        u.text(in.cut_top(24.0f), "Notifications", fade(th.text, sat(k)), ALIGN_LEFT);
    }
    (void)in.cut_top(8.0f);
    if (n == 0)
        u.text(in.cut_top(40.0f), "All quiet. Launch something!", fade(th.text_dim, sat(k)),
               ALIGN_LEFT);
    for (i32 i = 0; i < n; ++i)
    {
        const f32 ki = sat(k * 1.4f - static_cast<f32>(i) * 0.08f);
        rect r = in.cut_top(48.0f);
        r.x += (1.0f - ki) * 24.0f;
        u.draw_rounded_rect(r.pad(0.0f, 3.0f), fade(th.text, 0.05f * ki), 12.0f);
        fill_circle(u, {r.x + 16.0f, r.center_y()}, 4.0f, fade(s.accent_c, ki));
        rect tr = r.pad(30.0f, 4.0f, 8.0f, 4.0f);
        text_scope ts = u.text_style(13.0f);
        u.text_ellipsis(tr.cut_top(20.0f), s.history[i], fade(th.text, ki), ALIGN_LEFT);
        const i32 ago = static_cast<i32>(u.ctx->now - s.history_at[i]);
        if (ago < 2)
            u.text(tr, "just now", fade(th.text_dim, ki), ALIGN_LEFT);
        else
            u.textf(tr, fade(th.text_dim, ki), ALIGN_LEFT, "%d s ago", ago);
    }
}

static void draw_settings(ui &u, showcase &s, rect host)
{
    const theme &th = u.th();
    const uiid did = "settings"_id;
    const f32 w = clampf(host.w - 40.0f, 0.0f, 360.0f); // never negative on a tiny window
    // the drawer's own slide value (same key, same tween): lets the glass go UNDER it
    const f32 t = u.animate(id_child(did, "t"_id), s.drawer_open ? 1.0f : 0.0f,
                            tween{th.drawer.anim.duration * 1.6f, th.drawer.anim.curve});
    if (t > 0.001f)
    {
        u.blur(host, 10.0f, 0.0f, 0.85f * t); // a frosted scrim
        u.draw_rect(host,
                    fade(mixc(color{6, 6, 20, 255}, color{60, 60, 100, 255}, s.light), 0.35f * t));
        const rect panel = rect::make(host.right() - w + w * (1.0f - t), host.y, w, host.h);
        const rect glass_r = panel.pad(10.0f, 10.0f, 12.0f, 12.0f);
        soft_shadow(u, glass_r, 24.0f, 24.0f, t, 0.0f);
        glass_panel(u, glass_r, 24.0f, 1.0f, 30.0f);
    }
    drawer_style dst = th.drawer;
    dst.anim.duration *= 1.6f;
    comp::drawer_scope dr(u, host, s.drawer_open,
                          {.id = did, .width = w, .edge = comp::drawer_edge::RIGHT, .style = &dst});
    if (t <= 0.001f) return;

    const bool was_blocked = s.blocked;
    s.blocked = s.palette_open; // the drawer's own content stays live
    rect in = dr.content().pad(10.0f, 12.0f, 14.0f, 12.0f);
    {
        rect head = in.cut_top(36.0f);
        if (icon_button(u, head.cut_right(36.0f), ICON_PLUS, "set_close"_id, "Close", PI * 0.25f))
            s.drawer_open = false;
        text_scope ts = u.text_style(20.0f, s.bold);
        u.text(head, "Settings", th.text, ALIGN_LEFT);
    }
    (void)in.cut_top(12.0f);
    column c(in, 10.0f);

    // An accordion's height animates; the section below follows because the
    // area we cut for it is sized from the same tween value.
    const auto acc_area = [&](uiid id, bool open, f32 content_h)
    {
        const f32 at = u.animate(id_child(id, "t"_id), open ? 1.0f : 0.0f,
                                 tween{th.accordion.anim.duration, th.accordion.anim.curve});
        return c.next(th.accordion.header_h + content_h * at);
    };
    {
        const uiid id = "set_look"_id;
        const f32 ch = 128.0f;
        comp::accordion_scope acc(u, acc_area(id, s.acc_look, ch), "Appearance", s.acc_look,
                                  {.id = id, .content_h = ch});
        if (acc)
        {
            column a(acc.content().pad(4.0f, 10.0f, 4.0f, 0.0f), 10.0f);
            static const char *const modes[2] = {"Dark", "Light"};
            (void)comp::segmented(u, a.next(u.control_h()), modes, s.theme_mode,
                                  {.id = "set_theme"_id});
            rect sw = a.next(28.0f);
            u.text(sw.cut_left(70.0f), "Accent", th.text_dim, ALIGN_LEFT);
            accent_swatches(u, s, sw, "set_sw"_id);
            (void)comp::switch_toggle(u, a.next(28.0f), "Ambient background", s.ambient,
                                      {.id = "set_ambient"_id});
        }
    }
    {
        const uiid id = "set_motion"_id;
        const f32 ch = 136.0f;
        comp::accordion_scope acc(u, acc_area(id, s.acc_motion, ch), "Motion", s.acc_motion,
                                  {.id = id, .content_h = ch});
        if (acc)
        {
            column a(acc.content().pad(4.0f, 10.0f, 4.0f, 0.0f), 8.0f);
            (void)comp::switch_toggle(u, a.next(28.0f), "Reduced motion", s.reduced_motion,
                                      {.id = "set_reduced"_id});
            u.text(a.next(18.0f), "Page transition", th.text_dim, ALIGN_LEFT);
            static const char *const tr[3] = {"Slide + focus pull", "Slide only", "None"};
            (void)comp::radio_group(u, a.next(78.0f), tr, s.transition,
                                    {.id = "set_transition"_id, .row_h = 26.0f});
        }
    }
    {
        const uiid id = "set_layout"_id;
        const f32 ch = 112.0f;
        comp::accordion_scope acc(u, acc_area(id, s.acc_layout, ch), "Layout", s.acc_layout,
                                  {.id = id, .content_h = ch});
        if (acc)
        {
            column a(acc.content().pad(4.0f, 10.0f, 4.0f, 0.0f), 10.0f);
            static const char *const dens[3] = {"Compact", "Cozy", "Roomy"};
            (void)comp::segmented(u, a.next(u.control_h()), dens, s.density,
                                  {.id = "set_density"_id});
            (void)comp::switch_toggle(u, a.next(28.0f), "Auto-collapse sidebar", s.auto_collapse,
                                      {.id = "set_auto"_id});
            (void)comp::switch_toggle(u, a.next(28.0f), "Sidebar open", s.sidebar_open,
                                      {.id = "set_side"_id});
        }
    }
    {
        const rect foot = c.cut_bottom(40.0f);
        text_scope ts = u.text_style(12.0f);
        u.text_wrapped(
            foot,
            "Everything here is immediate mode: the drawer, the accordions and this text are "
            "re-cut every frame.",
            th.text_dim);
    }
    s.blocked = was_blocked;
}

static const comp::palette_command COMMANDS[] = {{"Go to Overview", "page"},
                                                 {"Go to Layout lab", "page"},
                                                 {"Go to Board", "page"},
                                                 {"Go to Effects", "page"},
                                                 {"Go to Motion", "page"},
                                                 {"Toggle dark / light", "theme"},
                                                 {"Cycle accent color", "theme"},
                                                 {"Toggle sidebar", "layout"},
                                                 {"Open settings", "drawer"},
                                                 {"Launch confetti", "fun"},
                                                 {"Shuffle the lab tiles", "lab"},
                                                 {"Add a board card", "board"},
                                                 {"Toggle reduced motion", "motion"}};

static void run_command(ui &u, showcase &s, i32 i, rect screen)
{
    if (i >= 0 && i < PAGE_COUNT)
        set_page(s, i, u.ctx->now);
    else if (i == 5)
        s.theme_mode ^= 1;
    else if (i == 6)
        s.accent = (s.accent + 1) % ACCENT_COUNT;
    else if (i == 7)
        s.sidebar_open = !s.sidebar_open;
    else if (i == 8)
        s.drawer_open = true;
    else if (i == 9)
        burst(s, {screen.center_x(), screen.center_y()}, 160, 950.0f);
    else if (i == 10)
        shuffle_tiles(s);
    else if (i == 11)
        add_card(s, "From the palette", 0);
    else if (i == 12)
        s.reduced_motion = !s.reduced_motion;
}

static void draw_particles(ui &u, showcase &s)
{
    const f32 dt = clampf(static_cast<f32>(u.ctx->dt), 0.0f, 0.05f);
    for (particle &p : s.particles)
    {
        p.age += dt;
        p.vy += 980.0f * dt;
        const f32 drag = 1.0f - 1.3f * dt;
        p.vx *= drag;
        p.vy *= drag;
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        p.rot += p.vrot * dt;
        const f32 life = 1.0f - p.age / p.ttl;
        if (life <= 0.0f) continue;
        const f32 flutter = std::fabs(std::cos(p.rot * 1.3f)); // confetti turning over
        const f32 hw = p.size * (0.25f + 0.75f * flutter), hh = p.size * 0.55f;
        const f32 ca = std::cos(p.rot), sa = std::sin(p.rot);
        const vec2 q[4] = {{p.x + (-hw * ca + hh * sa), p.y + (-hw * sa - hh * ca)},
                           {p.x + (hw * ca + hh * sa), p.y + (hw * sa - hh * ca)},
                           {p.x + (hw * ca - hh * sa), p.y + (hw * sa + hh * ca)},
                           {p.x + (-hw * ca - hh * sa), p.y + (-hw * sa + hh * ca)}};
        u.draw_polygon(q, fade(p.c, min2(1.0f, life * 2.5f)));
    }
    std::erase_if(s.particles, [](const particle &p) { return p.age >= p.ttl; });
    if (!s.particles.empty()) u.request_redraw();

    // a soft ring wherever the pointer presses
    const f64 now = u.ctx->now;
    for (const pulse &pl : s.pulses)
    {
        const f32 k = static_cast<f32>((now - pl.born) / 0.45);
        if (k >= 1.0f) continue;
        u.draw_arc(pl.p, 4.0f + 26.0f * ease(easing::EASE_OUT, k), 2.0f * (1.0f - k) + 0.5f, 0.0f,
                   2.0f * PI, fade(mixc(s.accent_c, color::white(), 0.5f), 0.7f * (1.0f - k)));
    }
    std::erase_if(s.pulses, [&](const pulse &pl) { return now - pl.born > 0.5; });
    std::erase_if(s.ripples, [&](const ripple &r) { return now - r.born > 0.7; });
    if (!s.pulses.empty() || !s.ripples.empty()) u.request_redraw();
}

// ============================================================ performance HUD
// A floating glass card in the work area's bottom-right corner, measuring the
// FULL loop: "frame" is start-to-start of consecutive frames (input pump, UI
// build, render, present and - with vsync on - the wait for the display).
// It is split into "ui" (this program building the frame) and "rest"
// (everything else). Numbers are 0.25 s averages; "max" is that window's worst
// frame. The graph auto-scales, so 1 ms frames do not sit on the floor. VSync
// starts off; the chip toggles it. Click the card to fold it to an fps chip.
static color budget_color(f32 ms)
{
    if (ms <= 17.5f) return color{90, 225, 140, 255}; // 60 fps or better
    if (ms <= 34.0f) return color{250, 190, 80, 255}; // 30+ fps
    return color{255, 95, 110, 255};
}

static void draw_perf_hud(ui &u, showcase &s, rect area)
{
    const theme &th = u.th();
    const f32 full_w = 344.0f, small_w = 112.0f, h = 70.0f;
    const f32 w = u.animate("hud_w"_id, s.hud_compact ? small_w : full_w, spring{320.0f, 0.8f});
    if (area.w < w + 32.0f || area.h < h + 32.0f) return; // no room on a tiny window
    const rect r = rect::make(area.right() - 16.0f - w, area.bottom() - 16.0f - h, w, h);
    const interaction it = u.interact("hud"_id, r, !s.blocked); // also shields what is below
    if (it.hovered) u.set_cursor(CURSOR_HAND);
    if (it.clicked || kb_hit(u, it)) s.hud_compact = !s.hud_compact;

    glass_panel(u, r, 16.0f, 1.0f, 18.0f);
    if (it.focused) outline(u, grow(r, 3.0f), 19.0f, th.focus_border, 1.5f);
    region rg = u.region(r, "hud_rg"_id); // the graph clips while the card folds

    const color c = budget_color(s.ms_shown);
    rect in = r.pad(12.0f, 8.0f);
    rect left = in.cut_left(min2(112.0f, in.w));
    {
        text_scope ts = u.text_style(20.0f, s.bold);
        rect top = left.cut_top(26.0f);
        const i32 fps = static_cast<i32>(s.fps_shown + 0.5f);
        char num[16];
        std::snprintf(num, sizeof(num), "%d", fps);
        const f32 nw = u.text_width(num) + 4.0f;
        u.text(top.cut_left(nw), num, c, ALIGN_LEFT);
        text_scope small = u.text_style(12.0f, s.bold);
        u.text(top, "fps", th.text_dim, ALIGN_LEFT);
    }
    {
        text_scope ts = u.text_style(12.0f);
        u.textf(left.cut_top(15.0f), th.text, ALIGN_LEFT, "%.2f ms / frame",
                static_cast<double>(s.ms_shown));
        text_scope tiny = u.text_style(11.0f);
        u.textf(left, th.text_dim, ALIGN_LEFT, "max %.2f ms", static_cast<double>(s.max_shown));
    }
    if (in.w < 120.0f) return; // folded: the numbers only

    (void)in.cut_left(10.0f);
    rect g = in;
    rect foot = g.cut_bottom(16.0f);
    (void)g.cut_bottom(4.0f);

    // the vsync chip (after the card's interact: topmost wins, so a click here
    // toggles vsync instead of folding the card)
    {
        const rect chip = foot.cut_right(76.0f);
        const interaction ci = u.interact("hud_vsync"_id, chip, !s.blocked);
        if (ci.hovered) u.set_cursor(CURSOR_HAND);
        if (ci.clicked || kb_hit(u, ci)) s.vsync = !s.vsync;
        const color vc = s.vsync ? color{90, 225, 140, 255} : color{250, 170, 70, 255};
        const f32 hv = u.smooth("hud_vsync_hv"_id, ci.hovered ? 1.0f : 0.0f, 0.06f);
        u.draw_rounded_rect(chip, fade(vc, 0.18f + 0.14f * hv), 8.0f);
        if (ci.focused) outline(u, grow(chip, 2.0f), 10.0f, th.focus_border, 1.5f);
        text_scope ts = u.text_style(10.0f, s.bold);
        u.text(chip, s.vsync ? "VSYNC ON" : "VSYNC OFF", vc, ALIGN_CENTER);
        (void)u.tooltip(chip, "hud_vsync_tip"_id,
                        s.vsync ? "Uncap: present without waiting for the display"
                                : "Cap to the display refresh");
    }
    {
        const f32 rest = max2(0.0f, s.ms_shown - s.build_shown);
        text_scope ts = u.text_style(11.0f);
        u.textf(foot, th.text_dim, ALIGN_LEFT, "ui %.2f + rest %.2f ms",
                static_cast<double>(s.build_shown), static_cast<double>(rest));
    }

    // the graph: newest sample on the right; the scale follows the recent peak
    // and is printed in its own strip above the plot
    const rect scale_r = g.cut_top(11.0f);
    constexpr i32 N = showcase::FT_N;
    const i32 n = s.ft_count;
    f32 peak = 0.0f;
    for (i32 i = 0; i < n; ++i) peak = max2(peak, s.ft_hist[(s.ft_head - n + i + N) % N]);
    const f32 scale = u.smooth("hud_scale"_id, max2(peak * 1.25f, 2.0f), 0.3f);
    const auto ms_y = [&](f32 ms) { return g.bottom() - clampf(ms / scale, 0.0f, 1.0f) * g.h; };
    {
        text_scope ts = u.text_style(10.0f);
        u.textf(scale_r, fade(th.text_dim, 0.8f), ALIGN_RIGHT, "0 - %.1f ms",
                static_cast<double>(scale));
    }
    for (f32 budget : {1000.0f / 144.0f, 1000.0f / 60.0f, 1000.0f / 30.0f}) // in-range budgets
    {
        if (budget > scale) continue;
        const f32 y = ms_y(budget);
        for (f32 x = g.x; x < g.right(); x += 6.0f)
            u.draw_line(x, y, min2(x + 3.0f, g.right()), y, fade(th.text, 0.18f), 1.0f);
    }
    // only recorded samples, right-aligned: an unfilled history is not 0 ms
    if (n < 2) return;
    vec2 pts[N];
    for (i32 i = 0; i < n; ++i)
    {
        const f32 ms = s.ft_hist[(s.ft_head - n + i + N) % N]; // oldest first
        pts[i] = {g.x + g.w * static_cast<f32>(N - n + i) / static_cast<f32>(N - 1), ms_y(ms)};
    }
    area_under(u, pts, n, g.bottom(), fade(c, 0.25f), fade(c, 0.0f));
    stroke(u, pts, n, c, 1.4f);
}

// ============================================================ the frame
static void selftest_script(showcase &s, example_app &app, f64 now)
{
    // offscreen runs visit every page first, then open and close each overlay
    const i32 f = app.frame_index;
    if (f < PAGE_COUNT)
    {
        set_page(s, f, now);
        return;
    }
    switch (f % 12)
    {
    case 0:
        s.drawer_open = true;
        break;
    case 2:
        s.drawer_open = false;
        s.palette_open = true;
        break;
    case 4:
        s.palette_open = false;
        break;
    case 5:
        s.theme_mode ^= 1;
        break;
    case 6:
        s.sidebar_open = !s.sidebar_open;
        break;
    case 7:
        set_page(s, (f / 12) % PAGE_COUNT, now);
        break;
    case 8:
        s.notes_open = true;
        break;
    case 9:
        s.notes_open = false;
        break;
    case 10:
        s.lab_mode = (s.lab_mode + 1) % 5;
        s.density = (s.density + 1) % 3;
        break;
    default:
        s.loaded = !s.loaded;
        s.radial_open = !s.radial_open;
        break;
    }
}

// --fuzz SEED: random pointer, wheel, keys, overlay toggles and window resizes
// (down to a few pixels) instead of the scripted input. Violations print with
// file:line; the selftest still fails on any.
static u32 g_fuzz = 0;
static bool g_no_hud = false; // --no-hud: screenshots without the performance card
static void fuzz_input(showcase &s, example_app &app)
{
    static bool held = false;
    window &w = *app.win;
    const auto r = [&]() { return frand(s); };
    mouse_move(w, r() * w.area.w, r() * w.area.h);
    if (!held && r() < 0.25f)
    {
        mouse_button(w, true);
        held = true;
    }
    else if (held && r() < 0.3f)
    {
        mouse_button(w, false);
        held = false;
    }
    if (r() < 0.04f)
    {
        mouse_button(w, pointer_button::RIGHT, true);
        mouse_button(w, pointer_button::RIGHT, false);
    }
    if (r() < 0.1f) mouse_wheel(w, 0.0f, (r() - 0.5f) * 6.0f);
    if (r() < 0.05f)
    {
        const key ks[6] = {key::TAB, key::ENTER, key::ESCAPE, key::SPACE, key::LEFT, key::RIGHT};
        const key k = ks[static_cast<i32>(r() * 6.0f) % 6];
        key_event(w, k, true);
        key_event(w, k, false);
    }
    if (r() < 0.03f) text_input_event(w, "x");
    if (r() < 0.02f) set_page(s, static_cast<i32>(r() * 5.0f), app.now);
    if (r() < 0.01f) s.drawer_open = !s.drawer_open;
    if (r() < 0.01f) s.palette_open = !s.palette_open;
    if (r() < 0.01f) s.notes_open = !s.notes_open;
    if (r() < 0.01f) s.lab_mode = static_cast<i32>(r() * 5.0f) % 5;
    if (r() < 0.01f) s.theme_mode ^= 1;
    if (r() < 0.01f) s.density = static_cast<i32>(r() * 3.0f) % 3;
    if (r() < 0.01f) s.sidebar_open = !s.sidebar_open;
    if (r() < 0.015f)
    {
        const int ww = r() < 0.15f ? 1 + static_cast<int>(r() * 120.0f)
                                   : 300 + static_cast<int>(r() * 1300.0f);
        const int hh =
            r() < 0.15f ? 1 + static_cast<int>(r() * 120.0f) : 200 + static_cast<int>(r() * 800.0f);
        SDL_SetWindowSize(app.sdl_window, ww, hh);
    }
}

static void showcase_frame(ui &u, example_app &app)
{
    const u64 build_t0 = SDL_GetPerformanceCounter(); // the HUD's cpu figure
    showcase &s = g_app;
    const f64 now = u.ctx->now;
    if (s.font == FONT_INVALID)
    {
        s.font = app.font;
        s.bold = app.font_bold;
        if (g_start_page >= 0) set_page(s, g_start_page, now);
    }
    if (app.selftest && g_start_page < 0 && g_fuzz == 0)
        selftest_script(s, app, now); // --page pins one page
    if (g_fuzz != 0) fuzz_input(s, app);

    // ---- keyboard shortcuts
    if (u.ctx->ctrl_down && u.key_pressed(key::SPACE))
    {
        u.consume_key(key::SPACE);
        s.palette_open = !s.palette_open;
        s.palette = comp::palette_state{}; // a fresh query, highlight and scroll
        s.palette_serial += 1;
    }
    if (u.ctx->ctrl_down && !s.palette_open && (u.key_pressed(key::UP) || u.key_pressed(key::DOWN)))
    {
        set_page(s, s.page + (u.key_pressed(key::DOWN) ? 1 : -1), now);
        u.consume_key(key::UP);
        u.consume_key(key::DOWN);
    }
    if (s.drawer_open && !s.palette_open && u.key_pressed(key::ESCAPE))
    {
        s.drawer_open = false;
        u.consume_key(key::ESCAPE);
    }

    // ---- per-frame model ticks
    s.motion_on = s.ambient && !s.reduced_motion;
    if (s.motion_on) s.clock += static_cast<f32>(u.ctx->dt);
    s.age = static_cast<f32>(now - s.page_since);
    // frame timing for the HUD: history + quarter-second averages (readable numbers)
    s.ft_hist[s.ft_head] = static_cast<f32>(u.ctx->dt * 1000.0);
    s.ft_head = (s.ft_head + 1) % showcase::FT_N;
    s.ft_count = s.ft_count < showcase::FT_N ? s.ft_count + 1 : s.ft_count;
    s.fps_acc_t += u.ctx->dt;
    s.fps_acc_build += static_cast<f64>(s.build_ms);
    s.fps_acc_max = u.ctx->dt > s.fps_acc_max ? u.ctx->dt : s.fps_acc_max;
    s.fps_acc_n += 1;
    if (s.fps_acc_t >= 0.25)
    {
        s.fps_shown = static_cast<f32>(s.fps_acc_n / s.fps_acc_t);
        s.ms_shown = static_cast<f32>(s.fps_acc_t * 1000.0 / s.fps_acc_n);
        s.build_shown = static_cast<f32>(s.fps_acc_build / s.fps_acc_n);
        s.max_shown = static_cast<f32>(s.fps_acc_max * 1000.0);
        s.fps_acc_t = 0.0;
        s.fps_acc_build = 0.0;
        s.fps_acc_max = 0.0;
        s.fps_acc_n = 0;
    }
    if (s.vsync != s.vsync_applied && u.ctx->device)
    {
        u.ctx->device->set_vsync(s.vsync);
        s.vsync_applied = s.vsync;
    }
    if (now >= s.gauge_next)
    {
        s.gauge_next = now + 2.6;
        for (f32 &gv : s.gauges) gv = clampf(gv + (frand(s) - 0.5f) * 0.5f, 0.08f, 0.97f);
    }
    if (now >= s.feed_next)
    {
        s.feed_next = now + 3.4;
        feed_event e;
        e.serial = s.feed_serial++;
        e.what = static_cast<i32>(frand(s) * 8.0f) % 8;
        e.at = now;
        s.feed.insert(s.feed.begin(), e);
        if (s.feed.size() > 10) s.feed.pop_back();
    }
    for (i32 i = 0; i < 3; ++i)
        if (s.fireworks_at[i] >= 0.0 && now >= s.fireworks_at[i])
        {
            s.fireworks_at[i] = -1.0;
            burst(s, s.fireworks_pos[i], 70, 520.0f);
        }
    if (u.ctx->mouse_pressed) s.pulses.push_back(pulse{pointer(u), now});

    // ---- theme: light/dark, accent and density all animate, then apply
    u.set_reduced_motion(s.reduced_motion);
    s.light =
        u.animate("theme_t"_id, static_cast<f32>(s.theme_mode), tween{0.6f, easing::EASE_IN_OUT});
    s.accent_c = u.animate_color("accent"_id, ACCENTS[s.accent], tween{0.45f, easing::EASE_OUT});
    const f32 dens = u.animate("density"_id, static_cast<f32>(s.density), spring{220.0f, 0.9f});
    set_theme(u.ctx, make_theme(s, s.light, s.accent_c, dens));
    if (s.motion_on) u.request_redraw();
    s.blocked = s.drawer_open || s.palette_open || s.notes_open;

    // ---- shell layout
    window &win = *u.ctx->current_window;
    const rect client = win.area;
    draw_backdrop(u, s, client);
    rect page = client;
    (void)u.titlebar(win, page, "Nebula - PufferUI showcase");

    const bool narrow = page.w < 980.0f;
    const bool side_open = s.sidebar_open && !(s.auto_collapse && narrow);
    const f32 side_w = u.animate("side_w"_id, side_open ? 236.0f : 80.0f, spring{240.0f, 0.82f});
    const rect side = page.cut_left(side_w);
    const rect work = page;

    // ---- the page (drawn first: the top bar blurs it as it scrolls under)
    const f32 top_pad = 92.0f;
    {
        const i32 p = s.page;
        f32 &target = s.scroll_target[p];
        const f32 max_scroll = max2(0.0f, s.content_h[p] + top_pad + 28.0f - work.h);
        if (!s.blocked && work.contains(u.ctx->mouse_x, u.ctx->mouse_y))
            target -= u.ctx->wheel_y * 90.0f;
        target = clampf(target, 0.0f, max_scroll);
        const f32 off = u.smooth(id_child("scroll"_id, static_cast<uiid>(p)), target,
                                 0.07f); // smooth scrolling
        const f32 k = enter_k(s, 0);
        if (k < 1.0f) u.request_redraw();
        const f32 slide = s.transition == 2 ? 0.0f : (1.0f - k) * 40.0f * s.nav_dir;
        {
            region rg = u.region(work, id_child("page"_id, static_cast<uiid>(p)));
            const rect content = rect::make(work.x + 22.0f, work.y + top_pad - off + slide,
                                            max2(80.0f, work.w - 44.0f), 100000.0f);
            const vec2 origin{content.x, content.y};
            f32 used = 0.0f;
            switch (p)
            {
            case PAGE_OVERVIEW:
                used = page_overview(u, s, content, origin);
                break;
            case PAGE_LAYOUT:
                used = page_layout(u, s, content, origin);
                break;
            case PAGE_BOARD:
                used = page_board(u, s, content, origin);
                break;
            case PAGE_EFFECTS:
                used = page_effects(u, s, content, origin);
                break;
            default:
                used = page_motion(u, s, content, origin);
                break;
            }
            s.content_h[p] = used;
            // the focus pull: a fresh page starts blurred and sharpens as it lands
            if (k < 1.0f && s.transition == 0) u.blur(work, 16.0f, 0.0f, (1.0f - k) * (1.0f - k));
        }
        // a slim scrollbar that only shows while scrolling or hovered; draggable
        if (max_scroll > 0.5f)
        {
            const rect track = rect::make(work.right() - 9.0f, work.y + top_pad, 4.0f,
                                          max2(0.0f, work.h - top_pad - 14.0f));
            const f32 view_h = work.h;
            const f32 total = s.content_h[p] + top_pad + 28.0f;
            const f32 th_h = max2(30.0f, track.h * view_h / total);
            const f32 th_y = track.y + (track.h - th_h) * (off / max_scroll);
            const rect thumb = rect::make(track.x - 3.0f, th_y, 10.0f, th_h);
            const interaction it = u.interact("page_sbar"_id, thumb, !s.blocked, false);
            if (it.hovered || it.pressed) u.set_cursor(CURSOR_HAND);
            if (it.activated)
            {
                s.scroll_anchor_y = u.ctx->mouse_y;
                s.scroll_anchor_t = target;
            }
            if (it.pressed && track.h > th_h)
                target = clampf(s.scroll_anchor_t + (u.ctx->mouse_y - s.scroll_anchor_y) *
                                                        max_scroll / (track.h - th_h),
                                0.0f, max_scroll);
            const bool near = work.contains(u.ctx->mouse_x, u.ctx->mouse_y) &&
                              u.ctx->mouse_x > work.right() - 40.0f;
            const f32 vis = u.smooth(
                "page_sbar_vis"_id,
                (it.hovered || it.pressed || near || std::fabs(off - target) > 1.0f) ? 1.0f : 0.25f,
                0.1f);
            const f32 wv =
                u.smooth("page_sbar_w"_id, (it.hovered || it.pressed) ? 8.0f : 4.0f, 0.05f);
            u.draw_rounded_rect(rect::make(track.right() - wv, th_y, wv, th_h),
                                fade(u.th().text, 0.4f * vis), wv * 0.5f);
        }
    }

    // ---- chrome over the page
    draw_topbar(u, s,
                rect::make(work.x + 12.0f, work.y + 12.0f, max2(80.0f, work.w - 24.0f), 60.0f));
    draw_sidebar(u, s, side, side_open, narrow);
    if (!g_no_hud) draw_perf_hud(u, s, work); // under the modal layers below

    // ---- overlays, back to front
    draw_notes(u, s);
    draw_settings(u, s, work);
    if (s.palette_open)
    {
        const f32 pk = u.appear(id_child("pal_in"_id, static_cast<uiid>(s.palette_serial)), 0.25f);
        u.blur(client, 12.0f, 0.0f, 0.9f * pk);
    }
    {
        const comp::palette_result pr = comp::command_palette(u, client, s.palette_open, s.palette,
                                                              COMMANDS, {.id = "palette"_id});
        if (pr.chosen >= 0)
        {
            s.palette_open = false;
            run_command(u, s, pr.chosen, client);
        }
    }
    comp::toast_draw(u, rect::make(work.x, work.y + 84.0f, max2(0.0f, work.w - 20.0f), 10.0f),
                     s.toasts, {.id = "toasts"_id});
    draw_particles(u, s);
    s.build_ms = static_cast<f32>(static_cast<f64>(SDL_GetPerformanceCounter() - build_t0) *
                                  1000.0 / static_cast<f64>(SDL_GetPerformanceFrequency()));
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i)
    {
        if (i + 1 < argc && std::strcmp(argv[i], "--page") == 0)
            g_start_page = std::atoi(argv[i + 1]);
        if (i + 1 < argc && std::strcmp(argv[i], "--fuzz") == 0)
            g_fuzz = static_cast<u32>(std::atoi(argv[i + 1]));
        if (std::strcmp(argv[i], "--no-hud") == 0) g_no_hud = true;
        if (std::strcmp(argv[i], "--light") == 0) g_app.theme_mode = 1; // start in the light theme
    }
    if (g_fuzz) g_app.rng = g_fuzz * 2654435761u + 1u;
    seed(g_app);
    return example_run("showcase", 1360, 860, argc, argv, showcase_frame);
}
