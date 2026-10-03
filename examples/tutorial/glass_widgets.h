// glass_widgets.h - custom widgets in the glass style (chapter 6).
//
// A widget is an ordinary function: `interact()` gives the pointer and focus
// state for a rect, animation functions give values that move toward a target,
// and the drawing primitives paint the result.
#pragma once

#include "glass.h"

// ---------------------------------------------------------------- shared
// Enter/Space on a focused widget counts like a click (and is consumed so
// nothing else reacts to the same key).
inline bool activated(ui &u, const interaction &in)
{
    const bool kb = in.focused && (u.key_pressed(key::ENTER) || u.key_pressed(key::SPACE));
    if (kb)
    {
        u.consume_key(key::ENTER);
        u.consume_key(key::SPACE);
    }
    return in.clicked || kb;
}

// A round checkbox: an outline, a disk that springs in, and a check mark.
inline bool glass_check(ui &u, rect r, bool &value, uiid id, f32 k = 1.0f)
{
    const interaction in = u.interact(id, r);
    if (in.hovered) u.set_cursor(CURSOR_HAND);
    const f32 t = u.animate(id_child(id, "on"_id), value ? 1.0f : 0.0f, spring{380.0f, 0.55f});
    const vec2 c{r.center_x(), r.center_y()};
    const f32 rad = min2(r.w, r.h) * 0.5f - 2.0f;
    const color white{255, 255, 255, 255};
    u.draw_arc(c, rad - 0.8f, 1.6f, 0.0f, 2.0f * PI, fade(white, (in.hovered ? 0.95f : 0.7f) * k));
    if (t > 0.01f)
    {
        // a disk is a rounded rect whose radius is half its size
        const f32 dr = rad * clampf(t, 0.0f, 1.15f);
        u.draw_rounded_rect(rect::make(c.x - dr, c.y - dr, dr * 2.0f, dr * 2.0f),
                            fade(white, clampf(t, 0.0f, 1.0f) * 0.92f * k), dr);
        const color tick = fade(color{82, 56, 190, 255}, clampf(t, 0.0f, 1.0f) * k);
        u.draw_line(c.x - rad * 0.38f, c.y + rad * 0.02f, c.x - rad * 0.10f, c.y + rad * 0.32f,
                    tick, 2.2f);
        u.draw_line(c.x - rad * 0.10f, c.y + rad * 0.32f, c.x + rad * 0.42f, c.y - rad * 0.28f,
                    tick, 2.2f);
    }
    if (in.focused) u.draw_arc(c, rad + 3.0f, 1.5f, 0.0f, 2.0f * PI, u.th().focus_border);
    if (activated(u, in))
    {
        value = !value;
        return true;
    }
    return false;
}

// The small "x" that deletes a row. Returns true when pressed.
inline bool glass_delete(ui &u, rect r, uiid id, bool row_hot, f32 k)
{
    const interaction in = u.interact(id, r);
    if (in.hovered) u.set_cursor(CURSOR_HAND);
    const f32 show =
        u.smooth(id_child(id, "show"_id), (row_hot || in.focused) ? 1.0f : 0.0f, 0.06f);
    if (show > 0.02f)
    {
        const vec2 c{r.center_x(), r.center_y()};
        if (in.hovered)
            u.draw_rounded_rect(rect::make(c.x - 13.0f, c.y - 13.0f, 26.0f, 26.0f),
                                color{255, 255, 255, 40}, 13.0f);
        const color x = fade(color{255, 255, 255, 255}, show * (in.hovered ? 1.0f : 0.7f) * k);
        u.draw_line(c.x - 4.5f, c.y - 4.5f, c.x + 4.5f, c.y + 4.5f, x, 1.8f);
        u.draw_line(c.x - 4.5f, c.y + 4.5f, c.x + 4.5f, c.y - 4.5f, x, 1.8f);
        if (in.focused) u.draw_arc(c, 15.0f, 1.5f, 0.0f, 2.0f * PI, u.th().focus_border);
    }
    u.tooltip(r, id_child(id, "tip"_id), "Delete");
    return activated(u, in);
}

// ---------------------------------------------------------------- the glass button
// Radii grown by `by` pixels (a square corner stays square): for an outline or a
// glow drawn around a shape.
inline corner_radii grown(const corner_radii &q, f32 by)
{
    return {q.tl > 0.0f ? q.tl + by : 0.0f, q.tr > 0.0f ? q.tr + by : 0.0f,
            q.br > 0.0f ? q.br + by : 0.0f, q.bl > 0.0f ? q.bl + by : 0.0f};
}

struct glass_button_style
{
    corner_radii radii = corner_radii::all(14.0f); // per corner: flush against a field, a tab, ...
    bool accent = false; // a solid frosted-white main action instead of translucent glass
};

// One frame of a button's pixels for the given state amounts, 0..1: `hover` blooms
// a glow and brightens the fill and rim, `press` pushes the button in and dims it,
// `focused` adds the keyboard focus ring. It only draws (no input), so the same
// function paints the live button and a gallery of its states.
inline void glass_button_paint(ui &u, rect r, std::string_view label, const glass_button_style &st,
                               f32 hover, f32 press, bool focused)
{
    const color white{255, 255, 255, 255};
    // hover glow: three growing, fading layers behind the button
    for (i32 i = 3; i >= 1; --i)
    {
        const f32 g = static_cast<f32>(i) * 2.5f;
        u.draw_rounded_rect(r.pad(-g), fade(white, hover * 0.035f * static_cast<f32>(4 - i)),
                            grown(st.radii, g));
    }

    // pressed: the body shrinks a little and the fill dims
    const rect body = r.pad(press * 1.5f);
    const f32 base = st.accent ? 214.0f : 34.0f;
    const f32 lit = st.accent ? 250.0f : 74.0f;
    const f32 dim = st.accent ? 168.0f : 20.0f;
    f32 alpha = base + (lit - base) * hover;
    alpha = alpha + (dim - alpha) * press;
    u.draw_rounded_rect(body, color{255, 255, 255, static_cast<u8>(alpha)}, st.radii);

    if (!st.accent) // a rim that brightens with hover (the same light-following rim as the cards)
    {
        glass_style rim{.radii = st.radii};
        rim.rim_light = {255, 255, 255, static_cast<u8>(150.0f + 105.0f * hover)};
        rim.rim_dark = {255, 255, 255, static_cast<u8>(55.0f + 60.0f * hover)};
        rim.rim_width = 1.2f;
        glass_rim(u, body, rim);
    }

    rect text_r = body;
    text_r.y += press; // the label sinks with the body
    u.text(text_r, label, st.accent ? color{70, 46, 170, 255} : color{255, 255, 255, 250},
           ALIGN_CENTER);

    if (focused)
    {
        glass_style ring{.radii = grown(st.radii, 3.0f)};
        ring.rim_light = ring.rim_dark = {255, 255, 255, 235};
        ring.rim_width = 1.6f;
        glass_rim(u, r.pad(-3.0f), ring);
    }
}

// A button: interact, ease the hover and press amounts, paint, report the click.
// Hover and press are `smooth` values, so the highlight fades instead of snapping.
inline bool glass_button(ui &u, rect r, std::string_view label, uiid id,
                         const glass_button_style &st = {})
{
    const interaction in = u.interact(id, r);
    if (in.hovered) u.set_cursor(CURSOR_HAND);
    const f32 hover = u.smooth(id_child(id, "hover"_id), in.hovered ? 1.0f : 0.0f, 0.05f);
    const f32 press = u.smooth(id_child(id, "press"_id), in.held ? 1.0f : 0.0f, 0.03f);
    glass_button_paint(u, r, label, st, hover, press, in.focused);
    return activated(u, in);
}
