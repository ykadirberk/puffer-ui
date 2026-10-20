// Independent r111 painting references; do not migrate them to optimized helpers.
namespace
{
static void reference_soft_shadow(ui &u, rect r, f32 radius, f32 spread, f32 strength, f32 dy)
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

static void reference_lab_tile_draw(ui &u, showcase &s, const lab_tile &tl, rect d, bool hovered,
                                    f32 alpha)
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

} // namespace

PUI_TEST(showcase_shadow_preserves_clipped_layers_and_feather)
{
    geometry_device dev;
    context *c = create_context(&dev, dev.create_surface());
    for (rect clip : {rect{10, 10, 120, 70}, rect{130.25f, 0, 0.25f, 200}, rect{}})
        for (rect r :
             {rect{15.25f, 12.75f, 40, 30}, rect{135, 20, 40, 30}, rect{130.5f, 20, 0.1f, 0.1f},
              rect{-30, 10, 20, 40}, rect{10, 84, 40, 2}, rect{350, 20, 40, 30}})
            for (f32 spread : {0.0f, 8.0f, 18.0f})
                for (f32 dy : {-30.0f, -6.0f, 0.0f, 6.0f, 30.0f})
                {
                    u64 expected = 0;
                    for (i32 pass = 0; pass < 2; ++pass)
                    {
                        dev.hash = 1469598103934665603ull;
                        begin_frame(c, 1, 1.0 / 60.0, {0, 0, 320, 240});
                        {
                            ui u(c);
                            auto rg = u.region(clip, "shadow_clip"_id);
                            if (pass == 0)
                                reference_soft_shadow(u, r, 8, spread, 0.8f, dy);
                            else
                                soft_shadow(u, r, 8, spread, 0.8f, dy);
                        }
                        end_frame(c);
                        if (pass == 0)
                            expected = dev.hash;
                        else
                            CHECK(dev.hash == expected);
                    }
                }
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

PUI_TEST(showcase_tile_paint_preserves_gloss_outlines_and_text_at_clip_edges)
{
    geometry_device dev;
    context *c = create_context(&dev, dev.create_surface());
    showcase s;
    s.bold = load_font(c, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans-Bold.ttf");
    theme th = default_dark();
    th.font = load_font(c, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
    set_theme(c, th);
    CHECK(s.bold != FONT_INVALID && th.font != FONT_INVALID);
    const lab_tile tile{.id = 1150, .hue = 0.63f, .tall = 0.7f};
    // Populate both font atlases before comparing batched geometry streams.
    s.lab_mode = 1;
    s.lab_selected = tile.id;
    begin_frame(c, 0, 1.0 / 60.0, {0, 0, 320, 240});
    {
        ui u(c);
        reference_lab_tile_draw(u, s, tile, {15, 30, 280, 124}, false, 0.8f);
    }
    end_frame(c);
    for (rect clip :
         {rect{10, 10, 120, 70}, rect{130.25f, 0, 0.25f, 200}, rect{10, 9.7f, 120, 0.1f}, rect{}})
        for (rect r : {rect{15.25f, 12.75f, 40, 30}, rect{135, 20, 40, 30}, rect{131, 20, 18, 18},
                       rect{10, 6, 60, 2}, rect{10, 6, 0.25f, 18}, rect{350, 20, 40, 30},
                       rect{15, 30, 280, 124}})
            for (i32 state : {0, 1, 2})
                for (i32 mode : {1, 4})
                {
                    s.lab_selected = state == 2 ? tile.id : 0;
                    s.lab_mode = mode;
                    s.clock = 1.25f;
                    u64 expected = 0;
                    for (i32 pass = 0; pass < 2; ++pass)
                    {
                        dev.hash = 1469598103934665603ull;
                        begin_frame(c, 1, 1.0 / 60.0, {0, 0, 320, 240});
                        {
                            ui u(c);
                            auto rg = u.region(clip, "tile_clip"_id);
                            if (pass == 0)
                                reference_lab_tile_draw(u, s, tile, r, state == 1, 0.8f);
                            else
                                lab_tile_draw(u, s, tile, r, state == 1, 0.8f);
                        }
                        end_frame(c);
                        if (pass == 0)
                            expected = dev.hash;
                        else
                            CHECK(dev.hash == expected);
                    }
                }
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}
