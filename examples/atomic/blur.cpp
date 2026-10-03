// blur — frosted-glass backdrops.
//
// Shows: `blur(rect, radius, corner_radius, alpha)`, the
// `backend_caps::RENDER_TARGETS` check / flat fallback, and how to get the
// frosted look right: blur the backdrop, then overlay a *translucent* surface
// (an opaque panel over the blur hides it completely).
//
// The scene below the header is split in half: the left half is the raw scene,
// the right half is the same scene with `u.blur` applied — so the smear is
// obvious.
//
//   pui_ex_blur   (drag the radius/alpha sliders)
#include "../example_common.h"

static f32 g_radius = 14.0f;
static f32 g_alpha = 1.0f;

static void draw_scene(ui &u, rect area, const theme &th)
{
    // Bands + high-contrast shapes: something worth smearing.
    for (i32 i = 0; i < 9; ++i)
    {
        const u8 v = static_cast<u8>(30 + i * 16);
        u.draw_rect(rect::make(area.x, area.y + static_cast<f32>(i) * 52.0f, area.w, 52.0f),
                    color{v, static_cast<u8>(60 + i * 12), static_cast<u8>(150 - i * 10), 255});
    }
    u.draw_rounded_rect(rect::make(area.x + area.w * 0.12f, area.y + 80.0f, 120.0f, 120.0f),
                        th.accent, 24.0f);
    u.draw_sector(vec2{area.x + area.w * 0.60f, area.y + 330.0f}, 0.0f, 58.0f, 0.0f, 6.2832f,
                  color{220, 170, 60, 255});
    const rect label = rect::make(area.x + area.w * 0.16f, area.y + 220.0f, 160.0f, 80.0f);
    u.draw_rounded_rect(label, color{230, 240, 250, 255}, 14.0f);
    u.text(label, "sharp text", color{20, 24, 30, 255}, ALIGN_CENTER);
}

static void blur_frame(ui &u, example_app &app)
{
    const theme &th = u.th();
    example_page page =
        example_begin_page(u, app, "Blur", "frosted-glass backdrops over a high-contrast scene");
    const rect content = page.content;
    const f32 half = content.x + content.w * 0.5f;
    draw_scene(u, content, th); // the same content on both halves

    // Controls first: the blurred half samples the scene, not the controls.
    {
        const rect body =
            example_section(u, rect::make(content.x + 12.0f, content.y + 12.0f, 264.0f, 174.0f),
                            "Controls", "blur(rect, radius, corner_radius, alpha)", app.font_bold);
        column col(body, 6.0f);
        (void)u.slider_float(col.next(26.0f), "radius", g_radius, 0.0f, 30.0f, "bl_radius"_id,
                             "%.0f");
        (void)u.slider_float(col.next(26.0f), "alpha", g_alpha, 0.0f, 1.0f, "bl_alpha"_id, "%.2f");
        const bool supported = has_cap(app.device->caps(), backend_caps::RENDER_TARGETS);
        u.textf(col.next(18.0f), th.text_dim, ALIGN_LEFT, "RENDER_TARGETS: %s",
                supported ? "yes" : "no (flat fallback)");
        u.text(col.next(18.0f), "left: raw   |   right: u.blur(...)", th.text_dim, ALIGN_LEFT);
    }

    // The frosted look = blurred backdrop below + translucent surface above.
    {
        const rect card = rect::make(half + 30.0f, content.y + 30.0f, 280.0f, 150.0f);
        u.blur(card, g_radius, 12.0f, g_alpha);
        u.draw_rounded_rect(card, color{16, 20, 28, 110}, 12.0f);
        u.draw_rounded_rect(card, color{120, 180, 240, 35}, 12.0f);
        column cc(card.pad(14.0f), 6.0f);
        u.text(cc.next(20.0f), "Frosted card", th.text, ALIGN_LEFT);
        u.text(cc.next(18.0f), "the backdrop is blurred;", th.text_dim, ALIGN_LEFT);
        u.text(cc.next(18.0f), "this surface is translucent", th.text_dim, ALIGN_LEFT);
        u.text(cc.next(18.0f), "(alpha ~110, not 245).", th.text_dim, ALIGN_LEFT);
    }
    {
        const rect box = rect::make(half + 40.0f, content.y + 210.0f, 260.0f, 170.0f);
        // Blur the right half: the left half stays sharp for comparison.
        u.blur(box, g_radius, 10.0f, g_alpha);
        u.draw_rounded_rect(box, color{16, 20, 28, 120}, 10.0f);
        u.draw_rounded_rect(box.top_slice(26.0f), color{16, 20, 28, 175}, 10.0f);
        u.text(box.top_slice(26.0f), "Frosted box", th.text, ALIGN_CENTER);
        column bc(box.pad(12.0f, 36.0f, 12.0f, 12.0f), 6.0f);
        u.text(bc.next(18.0f), "one u.blur rect covers both", th.text_dim, ALIGN_LEFT);
        u.text(bc.next(18.0f), "surfaces: blur the region,", th.text_dim, ALIGN_LEFT);
        u.text(bc.next(18.0f), "then draw translucent cards.", th.text_dim, ALIGN_LEFT);
    }
}

int main(int argc, char **argv)
{
    return example_run("blur", 760, 480, argc, argv, blur_frame);
}
