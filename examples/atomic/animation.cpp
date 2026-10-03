// animation — keyed values that move across frames.
//
// Shows: `animate` with `tween` / `spring`, `smooth`, `appear`, `animate_color`,
// `animations_active`, and `set_reduced_motion`.
//
//   pui_ex_animation   (press Toggle / Replay)
#include "../example_common.h"

static bool g_toggle = false;
static bool g_reduced = false;
static i32 g_appear_key = 0;

static void animation_frame(ui &u, example_app &app)
{
    const theme &th = u.th();
    example_page page =
        example_begin_page(u, app, "Animation", "tweens, springs and keyed values across frames");
    column col(page.content, example_ui::SECTION_GAP);

    {
        const rect body = example_section(
            u, col.next(122.0f), "Animation controls",
            "u.animate(key, target, spec) keeps state per key and returns the value",
            app.font_bold);
        column c(body, 8.0f);
        row r(c.next(28.0f), 8.0f);
        if (u.button(r.next(110.0f), "Toggle", "an_toggle"_id, "primary"_id)) g_toggle = !g_toggle;
        if (u.button(r.next(110.0f), "Replay", "an_replay"_id)) g_appear_key += 1;
        (void)u.checkbox(r.next(150.0f), "Reduced motion", g_reduced, "an_reduced"_id);
        u.textf(r.remaining(), th.text_dim, ALIGN_RIGHT, "animations_active(): %s",
                u.animations_active() ? "yes" : "no");

        u.text(c.next(18.0f),
               "keys are collected after 5 s without use; set_reduced_motion() snaps to targets",
               th.text_dim, ALIGN_LEFT);
    }
    u.set_reduced_motion(g_reduced);

    // Lane helper: a label plus a lane body.
    auto lane = [&](rect area, const char *label) -> rect
    {
        row r(area, 8.0f);
        u.text(r.next(86.0f), label, th.text_dim, ALIGN_LEFT);
        return r.next(r.remaining().w);
    };

    {
        const rect body = example_section(
            u, col.next(210.0f), "Tween, spring and smooth",
            "a time curve, physics, and an exponential follow with a half-life", app.font_bold);
        column c(body, 8.0f);

        // tween: time-parametric with an easing curve.
        {
            const rect lane_body = lane(c.next(42.0f), "tween");
            u.draw_rect(lane_body, th.widget_bg);
            const f32 t = u.animate("an_tween"_id, g_toggle ? 1.0f : 0.0f,
                                    tween{.duration = 0.5f, .curve = easing::EASE_OUT_BACK});
            u.draw_rounded_rect(rect::make(lane_body.x + 4.0f + (lane_body.w - 74.0f) * t,
                                           lane_body.y + 6.0f, 70.0f, 30.0f),
                                th.accent, 6.0f);
        }

        // spring: physics; keeps velocity when the target flips mid-flight.
        {
            const rect lane_body = lane(c.next(42.0f), "spring");
            u.draw_rect(lane_body, th.widget_bg);
            const f32 s = u.animate("an_spring"_id, g_toggle ? 1.0f : 0.0f, spring{260.0f, 0.7f});
            const f32 w = 40.0f + 170.0f * clampf(s, 0.0f, 1.2f);
            u.draw_rounded_rect(rect::make(lane_body.x + 4.0f, lane_body.y + 6.0f, w, 30.0f),
                                th.widget_hover, 6.0f);
        }

        // smooth: exponential follow with a half-life (good for pointers/scroll).
        {
            const rect lane_body = lane(c.next(42.0f), "smooth");
            u.draw_rect(lane_body, th.widget_bg);
            const f32 sm = u.smooth("an_smooth"_id, g_toggle ? 1.0f : 0.0f, 0.25f);
            u.draw_rounded_rect(rect::make(lane_body.x + 4.0f + (lane_body.w - 74.0f) * sm,
                                           lane_body.y + 6.0f, 70.0f, 30.0f),
                                th.text_dim, 6.0f);
        }
    }

    {
        const rect body = example_section(
            u, col.next(160.0f), "Appear and color",
            "fade/scale-in for freshly created keys; animate_color interpolates any color",
            app.font_bold);
        column stack(body, 8.0f);

        // appear: fade/scale-in for freshly created keys.
        {
            const rect lane_body = lane(stack.next(42.0f), "appear");
            u.draw_rect(lane_body, th.widget_bg);
            const f32 a = u.appear(id_child("an_appear"_id, static_cast<uiid>(g_appear_key)), 0.5f);
            color c = th.accent;
            c.a = static_cast<u8>(255.0f * clampf(a, 0.0f, 1.0f));
            const f32 w = 70.0f + 90.0f * clampf(a, 0.0f, 1.0f);
            u.draw_rounded_rect(rect::make(lane_body.x + 4.0f,
                                           lane_body.y + 6.0f + (30.0f - 30.0f * a) * 0.5f, w,
                                           30.0f * a + 1.0f),
                                c, 6.0f);
        }

        // animate_color: interpolate any color; a tween to a target.
        {
            const rect lane_body = lane(stack.next(42.0f), "color");
            u.draw_rect(lane_body, th.widget_bg);
            const color target = g_toggle ? th.accent : color{220, 120, 60, 255};
            const color c = u.animate_color("an_color"_id, target, tween{.duration = 0.4f});
            u.draw_rounded_rect(
                lane_body.pad(4.0f).fit_aspect(2.0f).align_left(lane_body.pad(4.0f)), c, 6.0f);
        }
    }
}

int main(int argc, char **argv)
{
    return example_run("animation", 720, 630, argc, argv, animation_frame);
}
