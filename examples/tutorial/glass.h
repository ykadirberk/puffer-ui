// glass.h - the "liquid glass" look, built from the public drawing API (chapter 5).
//
// Four ideas, each a few lines:
//   1. a vivid background (gradient quad + soft glowing blobs)  -> glass_background
//   2. blur what is behind a rect, then lay a translucent tint  -> glass_card
//   3. a thin rim whose brightness follows the light direction  -> glass_rim
//   4. a theme whose colors are translucent whites               -> glass_theme
//
// Nothing here touches the library's internals: only draw_triangles (custom
// vertex colors), draw_rounded_rect, blur, and the theme.
#pragma once

#include <pufferui/pufferui.h>

#include <cmath>

using namespace pui;

// A color with its alpha multiplied by k (0..1).
inline color fade(color c, f32 k)
{
    c.a = static_cast<u8>(clampf(static_cast<f32>(c.a) * k, 0.0f, 255.0f));
    return c;
}

// ---------------------------------------------------------------- background
// A soft round glow: a triangle fan whose center is `c` and whose rim is the same
// color at alpha 0. The renderer interpolates the vertex colors, so this is a
// true radial gradient with no texture.
inline void glass_glow(ui &u, vec2 center, f32 radius, color c)
{
    constexpr i32 SEG = 40;
    vertex v[SEG + 2];
    i32 idx[SEG * 3];
    v[0] = {center.x, center.y, 0.0f, 0.0f, c};
    color edge = c;
    edge.a = 0;
    for (i32 i = 0; i <= SEG; ++i)
    {
        const f32 a = 2.0f * PI * static_cast<f32>(i) / static_cast<f32>(SEG);
        v[i + 1] = {center.x + std::cos(a) * radius, center.y + std::sin(a) * radius, 0.0f, 0.0f,
                    edge};
    }
    for (i32 i = 0; i < SEG; ++i)
    {
        idx[i * 3 + 0] = 0;
        idx[i * 3 + 1] = i + 1;
        idx[i * 3 + 2] = i + 2;
    }
    u.draw_triangles(nullptr, v, SEG + 2, idx, SEG * 3);
}

// A crisp round "orb": a fan whose center and edge colors differ (both opaque),
// so it has a visible rim. Behind glass it shows blurred; beside it, sharp.
inline void glass_orb(ui &u, vec2 center, f32 radius, color inner, color outer)
{
    constexpr i32 SEG = 48;
    vertex v[SEG + 2];
    i32 idx[SEG * 3];
    v[0] = {center.x, center.y, 0.0f, 0.0f, inner};
    for (i32 i = 0; i <= SEG; ++i)
    {
        const f32 a = 2.0f * PI * static_cast<f32>(i) / static_cast<f32>(SEG);
        v[i + 1] = {center.x + std::cos(a) * radius, center.y + std::sin(a) * radius, 0.0f, 0.0f,
                    outer};
    }
    for (i32 i = 0; i < SEG; ++i)
    {
        idx[i * 3 + 0] = 0;
        idx[i * 3 + 1] = i + 1;
        idx[i * 3 + 2] = i + 2;
    }
    u.draw_triangles(nullptr, v, SEG + 2, idx, SEG * 3);
}

// The scene the glass sits on: a gradient, soft glows, and a few crisp orbs
// (so the blur has something to blur). `time` (seconds) drifts them slowly;
// pass a constant to freeze the scene.
inline void glass_background(ui &u, rect r, f64 time)
{
    // four corner colors; the renderer blends between them
    const color tl{66, 52, 214, 255}, tr{214, 64, 168, 255};
    const color bl{18, 128, 220, 255}, br{104, 44, 200, 255};
    vertex v[4] = {{r.x, r.y, 0, 0, tl},
                   {r.right(), r.y, 1, 0, tr},
                   {r.right(), r.bottom(), 1, 1, br},
                   {r.x, r.bottom(), 0, 1, bl}};
    const i32 idx[6] = {0, 1, 2, 0, 2, 3};
    u.draw_triangles(nullptr, v, 4, idx, 6);

    const f32 t = static_cast<f32>(time);
    const f32 s = max2(r.w, r.h);
    glass_glow(u, {r.x + r.w * 0.15f, r.y + r.h * 0.12f}, s * 0.55f, color{255, 150, 70, 170});
    glass_glow(u, {r.x + r.w * 0.90f, r.y + r.h * 0.55f}, s * 0.50f, color{40, 232, 226, 130});
    glass_glow(u, {r.x + r.w * 0.30f, r.y + r.h * 1.00f}, s * 0.55f, color{255, 70, 190, 150});

    glass_orb(u, {r.x + r.w * (0.20f + 0.05f * std::sin(t * 0.30f)), r.y + r.h * 0.20f}, s * 0.11f,
              color{255, 214, 120, 255}, color{255, 120, 80, 255});
    glass_orb(u, {r.x + r.w * 0.86f, r.y + r.h * (0.47f + 0.04f * std::sin(t * 0.26f + 1.0f))},
              s * 0.13f, color{150, 255, 240, 255}, color{30, 170, 230, 255});
    glass_orb(u, {r.x + r.w * (0.30f + 0.06f * std::sin(t * 0.22f + 2.0f)), r.y + r.h * 0.84f},
              s * 0.10f, color{255, 150, 220, 255}, color{200, 60, 200, 255});
}

// ---------------------------------------------------------------- the card
struct glass_style
{
    corner_radii radii = corner_radii::all(24.0f); // per-corner radii; 0 = a square corner
    f32 blur = 24.0f;                              // backdrop blur radius (see chapter 5)
    color tint = {255, 255, 255, 34};              // translucent fill over the blur
    f32 sheen = 0.20f;                             // brightness of the top highlight (0 = none)
    color rim_light = {255, 255, 255, 190};        // rim where it faces the light
    color rim_dark = {255, 255, 255, 30};          // rim where it does not
    f32 rim_width = 1.3f;
};

// A thin outline whose color depends on which way the edge faces: bright toward
// the top-left light (and a weaker echo on the opposite side, like refraction),
// dim elsewhere. One draw_triangles call: four rings of vertices (a faded outer
// edge, the bright core, a faded inner edge) around the rounded rectangle.
inline void glass_rim(ui &u, rect r, const glass_style &st)
{
    constexpr i32 SEG = 8;           // segments per corner
    constexpr i32 N = 4 * (SEG + 1); // points around the outline
    constexpr i32 RINGS = 4;
    // corners clockwise from the top-left, each with its own radius (0 = square:
    // its arc collapses to the corner point)
    const f32 half = min2(r.w, r.h) * 0.5f;
    const f32 rad[4] = {clampf(st.radii.tl, 0.0f, half), clampf(st.radii.tr, 0.0f, half),
                        clampf(st.radii.br, 0.0f, half), clampf(st.radii.bl, 0.0f, half)};
    // centers of the corner arcs, and the angle each arc starts at
    const vec2 centers[4] = {{r.x + rad[0], r.y + rad[0]},
                             {r.right() - rad[1], r.y + rad[1]},
                             {r.right() - rad[2], r.bottom() - rad[2]},
                             {r.x + rad[3], r.bottom() - rad[3]}};
    const f32 start[4] = {PI, PI * 1.5f, 0.0f, PI * 0.5f};

    vec2 pos[N], nrm[N];
    for (i32 c = 0; c < 4; ++c)
    {
        for (i32 s = 0; s <= SEG; ++s)
        {
            const f32 a = start[c] + (PI * 0.5f) * static_cast<f32>(s) / static_cast<f32>(SEG);
            const i32 i = c * (SEG + 1) + s;
            nrm[i] = {std::cos(a), std::sin(a)};
            pos[i] = {centers[c].x + nrm[i].x * rad[c], centers[c].y + nrm[i].y * rad[c]};
        }
    }

    // ring offsets along the outward normal, and whether the ring is faded out
    const f32 offs[RINGS] = {0.7f, 0.0f, -st.rim_width, -st.rim_width - 0.7f};
    const bool edge[RINGS] = {true, false, false, true};
    vertex v[RINGS * N];
    for (i32 k = 0; k < RINGS; ++k)
    {
        for (i32 i = 0; i < N; ++i)
        {
            const f32 d = nrm[i].x * -0.7071f + nrm[i].y * -0.7071f; // facing the light?
            const f32 lit = d > 0.0f ? d * d : 0.0f;
            const f32 echo = d < 0.0f ? 0.5f * d * d : 0.0f;
            color c = theme_lerp_color(st.rim_dark, st.rim_light, clampf(lit + echo, 0.0f, 1.0f));
            if (edge[k]) c.a = 0;
            v[k * N + i] = {pos[i].x + nrm[i].x * offs[k], pos[i].y + nrm[i].y * offs[k], 0.0f,
                            0.0f, c};
        }
    }
    i32 idx[(RINGS - 1) * N * 6];
    i32 n = 0;
    for (i32 k = 0; k < RINGS - 1; ++k)
    {
        for (i32 i = 0; i < N; ++i)
        {
            const i32 j = (i + 1) % N;
            const i32 a = k * N + i, b = k * N + j, c = (k + 1) * N + i, d = (k + 1) * N + j;
            idx[n++] = a;
            idx[n++] = b;
            idx[n++] = c;
            idx[n++] = b;
            idx[n++] = d;
            idx[n++] = c;
        }
    }
    u.draw_triangles(nullptr, v, RINGS * N, idx, n);
}

// The card: blur whatever is behind `r` (everything drawn so far), lay a
// translucent tint over it, add the sheen, then the rim.
inline void glass_card(ui &u, rect r, const glass_style &st = {})
{
    u.blur(r, st.blur, st.radii, 1.0f);        // the blur and the tint share one shape ...
    u.draw_rounded_rect(r, st.tint, st.radii); // ... so square corners stay square
    if (st.sheen > 0.0f)
    {
        // a white-to-clear gradient over the upper part; it also fades out
        // toward both sides so it never shows a hard edge
        const f32 inset_l = st.radii.tl * 0.6f, inset_r = st.radii.tr * 0.6f;
        const rect s = rect::make(r.x + inset_l, r.y + 1.0f, r.w - inset_l - inset_r, r.h * 0.4f);
        const f32 cols[4] = {0.0f, 0.22f, 0.78f, 1.0f}; // x positions across the strip
        const f32 amp[4] = {0.0f, 1.0f, 1.0f, 0.0f};    // brightness at the top row
        vertex v[8];
        for (i32 i = 0; i < 4; ++i)
        {
            const f32 x = s.x + s.w * cols[i];
            v[i] = {x, s.y, 0.0f, 0.0f, fade(color{255, 255, 255, 255}, st.sheen * amp[i])};
            v[4 + i] = {x, s.bottom(), 0.0f, 0.0f, color{255, 255, 255, 0}};
        }
        i32 idx[18];
        for (i32 i = 0; i < 3; ++i)
        {
            const i32 o = i * 6;
            idx[o + 0] = i;
            idx[o + 1] = i + 1;
            idx[o + 2] = 4 + i;
            idx[o + 3] = i + 1;
            idx[o + 4] = 5 + i;
            idx[o + 5] = 4 + i;
        }
        u.draw_triangles(nullptr, v, 8, idx, 18);
    }
    glass_rim(u, r, st);
}

// ---------------------------------------------------------------- the theme
// Widgets read their colors from the theme, so making them glass is a matter of
// translucent whites: fields, buttons, scrollbars and the titlebar all follow.
inline theme glass_theme(font_handle font)
{
    theme t = default_dark();
    t.font = font;
    t.text_size = 18.0f;
    t.text = {255, 255, 255, 245};
    t.text_dim = {236, 238, 255, 175};
    t.radius = 14.0f;
    // the stock checkbox reads accent (fill) and bg (check mark)
    t.accent = {255, 255, 255, 235};
    t.accent_hover = {255, 255, 255, 255};
    t.bg = {70, 46, 170, 255};
    t.selection = {255, 255, 255, 70};
    t.caret = {255, 255, 255, 255};

    t.widget_bg = {255, 255, 255, 30};
    t.widget_hover = {255, 255, 255, 52};
    t.widget_active = {255, 255, 255, 18};
    t.border = {255, 255, 255, 70};
    t.border_thickness = 1.0f;
    t.focus_border = {255, 255, 255, 235};
    t.focus_border_thickness = 1.5f;
    t.panel_bg = {255, 255, 255, 22}; // the titlebar band

    t.button.bg = {255, 255, 255, 34};
    t.button.hover_bg = {255, 255, 255, 62};
    t.button.active_bg = {255, 255, 255, 20};
    t.button.border = {255, 255, 255, 84};
    t.button.border_thickness = 1.0f;
    t.button.radius = 14.0f;
    t.button.text = {255, 255, 255, 250};
    t.button.transition = transition{.duration = 0.12f, .curve = easing::EASE_OUT};
    // a solid "frosted white" button for the main action
    set_button_role(t, "accent"_id,
                    button_override{.bg = some(color{255, 255, 255, 214}),
                                    .hover_bg = some(color{255, 255, 255, 240}),
                                    .active_bg = some(color{255, 255, 255, 170}),
                                    .border = some(color{255, 255, 255, 0}),
                                    .text = some(color{70, 46, 170, 255})});

    t.scrollbar.thumb = {255, 255, 255, 70};
    t.scrollbar.thumb_hover = {255, 255, 255, 120};
    t.scrollbar.thumb_active = {255, 255, 255, 170};
    t.scrollbar.track = {255, 255, 255, 0};

    t.segmented.bg = {255, 255, 255, 0}; // it sits inside a glass card
    t.segmented.selected = {255, 255, 255, 84};
    t.segmented.text = {255, 255, 255, 190};
    t.segmented.text_selected = {255, 255, 255, 255};
    t.segmented.hover = {255, 255, 255, 34}; // laid over a hovered segment
    return t;
}
