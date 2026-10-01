// render: batching, flush, shapes, blur, device contract (r88 split).
#include "test_util.h"

void test_draw_list_snapshot()
{
    // Draw-list snapshot tests: record the flushed draw calls as text and
    // assert on the commands — stable across platforms, reviewable in a PR,
    // no pixels involved. A rect + a card produce predictable calls.
    snapshot_device nd;
    context *c = create_context(&nd, nd.create_surface());
    set_violation_handler(c, capture_violation, nullptr);
    g_violation_events = 0;

    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 300, 200));
    {
        ui u(c);
        u.draw_rect(rect::make(10, 10, 80, 28), color{30, 34, 42, 255});
        u.card(rect::make(0, 50, 120, 60));
    }
    end_frame(c);

    // At least one flush captured, and the first rect's geometry lands where
    // it was sent. (Vertex counts per flush vary — rings emit sector
    // triangles, not just quads — so no count shape is asserted.)
    CHECK(nd.calls.size() >= 1);
    i32 total_verts = 0;
    bool saw_first_rect = false;
    for (const std::string &s : nd.calls)
    {
        int verts = 0;
        std::sscanf(s.c_str(), "draw tex=%*d verts=%d", &verts);
        CHECK(verts > 0);
        total_verts += verts;
        if (s.find("v0=(10,10,255,30,34,42)") != std::string::npos) saw_first_rect = true;
    }
    CHECK(total_verts >= 8); // the rect + the card's fill/border at minimum
    CHECK(saw_first_rect);   // the exact first quad, as text

    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

void test_culling_and_visibility()
{
    // Draw-list-level culling: a batch that cannot intersect the active clip
    // never reaches the renderer (a scroll view's scrolled-out rows are the
    // hot case), and `is_visible` gives callers the same test.
    snapshot_device nd;
    context *c = create_context(&nd, nd.create_surface());
    set_violation_handler(c, capture_violation, nullptr);
    g_violation_events = 0;

    const rect view = rect::make(0, 0, 100, 50);

    // The clip is the viewport — `scroll_to` moves the content, the scissors
    // stay put. Device counters advance at flush, so each frame asserts once.
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 300, 200));
    {
        ui u(c);
        scroll_view sv = u.scroll(view, "cull_view"_id);
        sv.set_content_height(40.0f);                           // fits: no bar, no overflow paths
        CHECK(u.is_visible(rect::make(0, 10, 80, 20)));         // inside the viewport
        CHECK(!u.is_visible(rect::make(0, 70, 80, 20)));        // below the viewport
        CHECK(!u.is_visible(rect::make(0, -30, 80, 20)));       // above the viewport
        u.draw_rect(rect::make(0, 70, 80, 20), color::white()); // below the viewport
    }
    end_frame(c);
    CHECK(nd.vertices == 0); // the culled quad never reached the device

    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 300, 200));
    {
        ui u(c);
        scroll_view sv = u.scroll(view, "cull_view"_id);
        sv.set_content_height(40.0f);
        u.draw_rect(rect::make(0, 10, 80, 20), color::white()); // in the viewport
    }
    end_frame(c);
    CHECK(nd.vertices == 4); // the in-viewport quad submitted

    // No clip active: everything is "visible" (the old behavior).
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 300, 200));
    {
        ui u(c);
        CHECK(u.is_visible(rect::make(-1000.0f, -1000.0f, 10.0f, 10.0f)));
    }
    end_frame(c);

    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

void test_identity_scope()
{
    // `ui::scope(key)` — pure identity sugar: widgets inside derive with
    // `local("part")`, two instances of the same component shape never
    // collide, nesting composes, and duplicate keys report like regions.
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    set_violation_handler(c, capture_violation, nullptr);
    g_violation_events = 0;

    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 300, 200));
    {
        ui u(c);
        uiid first_item = 0, second_item = 0;
        {
            id_scope s = u.scope("list"_id);
            first_item = u.local("item");
        }
        {
            id_scope s = u.scope("list2"_id); // a second instance
            second_item = u.local("item");
        }
        CHECK(first_item != second_item);        // same component shape, different ids
        CHECK(u.local("outside") != first_item); // unscooped derivation differs

        // nesting composes: an inner scope derives from the outer one, and
        // leaving it restores the outer derivation
        {
            id_scope outer = u.scope("panel"_id);
            const uiid in_panel = u.local("header");
            {
                id_scope inner = u.scope("tools"_id);
                CHECK(u.local("btn") != in_panel);
            }
            CHECK(u.local("header") == in_panel);
        }
    }
    end_frame(c);
    CHECK(g_violation_events == 0);

    // duplicate scope keys report (same set as regions) — sequential scopes
    // at the same level share the key
    g_violation_events = 0;
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 300, 200));
    {
        ui u(c);
        {
            id_scope a = u.scope("dup"_id);
        }
        {
            id_scope b = u.scope("dup"_id);
        } // same key, same level, again
    }
    end_frame(c);
    CHECK(g_violation_events == 1);
    CHECK(violation_was("duplicate region id among siblings"));
    const i32 violations_here = violation_count(c);

    destroy_context(c);
}

void test_dup_widget_id_and_sizes()
{
    // Debug duplicate-widget detection: the same id interacted at two
    // different rects in one frame reports (the old failure was silent
    // shared state); identical rects stay tolerated. Also: the natural-size
    // helpers (`button_size` / `text_size`) and the options-struct forms.
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    set_violation_handler(c, capture_violation, nullptr);
    g_violation_events = 0;

    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 300, 200));
    {
        ui u(c);
        (void)u.interact("twice"_id, rect::make(0, 0, 40, 20));
        (void)u.interact("twice"_id, rect::make(100, 0, 40, 20)); // different rect: the bug
    }
    end_frame(c);
#if !defined(NDEBUG)
    CHECK(g_violation_events == 1);
    CHECK(violation_was(
        "duplicate widget id among siblings (the same id interacted at a different rect)"));
#else
    // Release builds skip the check (the detection is debug-only by design);
    // the same frame must simply not report anything.
    CHECK(g_violation_events == 0);
#endif

    // identical rects are tolerated (a widget re-submitting in place)
    g_violation_events = 0;
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 300, 200));
    {
        ui u(c);
        (void)u.interact("inplace"_id, rect::make(0, 0, 40, 20));
        (void)u.interact("inplace"_id, rect::make(0, 0, 40, 20));
    }
    end_frame(c);
    CHECK(g_violation_events == 0);

    // natural sizes + options-struct forms (a delta check: the frames above
    // reported one duplicate)
    const i32 violations_before_sizes = violation_count(c);
    begin_frame(c, 0.032, 0.016, rect::make(0, 0, 300, 200));
    {
        ui u(c);
        const vec2 bs = u.button_size("OK");
        CHECK(bs.y == u.control_h());
        CHECK(bs.x > u.text_size("OK").x); // label + padding
        const vec2 ts = u.text_size("hello world");
        CHECK(ts.x == u.text_width("hello world"));
        CHECK(ts.y == u.line_height());

        bool clicked = u.button(rect::make(0, 40, static_cast<f32>(bs.x), bs.y), "OK", "opt_btn"_id,
                                button_opts{.role = "primary"_id});
        (void)clicked;
        CHECK(violation_count(c) == violations_before_sizes); // the opts form adds none
    }
    end_frame(c);

    destroy_context(c);
}

void test_needs_redraw()
{
    // The idle-sleep gate is conservative: an unsettled animation or a
    // focused text field (the caret blinks) each demand a redraw; a quiet
    // context does not.
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    set_violation_handler(c, capture_violation, nullptr);
    g_violation_events = 0;

    // quiet: no redraw needed
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 300, 200));
    end_frame(c);
    CHECK(!needs_redraw(c));

    // an unsettled animation asks for a redraw
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 300, 200));
    {
        ui u(c);
        (void)u.animate("idle_anim"_id, 1.0f, tween{0.5f});
    }
    end_frame(c);
    CHECK(needs_redraw(c)); // mid-tween

    // settle it (run past the duration)
    for (i32 i = 0; i < 40; ++i)
    {
        begin_frame(c, 0.032 + 0.016 * i, 0.016, rect::make(0, 0, 300, 200));
        {
            ui u(c);
            (void)u.animate("idle_anim"_id, 1.0f, tween{0.5f});
        }
        end_frame(c);
    }
    CHECK(!needs_redraw(c)); // settled

    // a focused field blinks (needs redraws)
    std::string v = "x";
    mouse_move(c, 50.0f, 12.0f);
    mouse_button(c, true);
    begin_frame(c, 1.0, 0.016, rect::make(0, 0, 300, 200));
    {
        ui u(c);
        (void)u.text_field(rect::make(0, 0, 100, 24), v, "idle_field"_id);
    }
    end_frame(c);
    mouse_button(c, false);
    CHECK(needs_redraw(c)); // the caret blinks while focused

    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

void test_draw_batching()
{
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 100, 100));
    ui u(c);
    u.draw_rect({0, 0, 10, 10}, color::white());
    u.draw_line(0, 0, 10, 0, color::white());
    // horizontal -> one more quad
    CHECK(nd.draw_calls == 0);
    // still batched, not flushed
    end_frame(c);
    CHECK(nd.draw_calls == 1);
    // both quads share one batch
    CHECK(nd.vertices == 8);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

void test_theme()
{
    context *c = create_context(nullptr);
    theme t = default_dark();
    t.accent = {255, 0, 128, 255};
    set_theme(c, t);
    CHECK(c->active_theme.accent.r == 255 && c->active_theme.accent.g == 0);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

void test_rounded_rect()
{
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 100, 100));
    ui u(c);
    u.draw_rounded_rect({10, 10, 80, 40}, color::white(), 8.0f);
    end_frame(c);
    CHECK(nd.draw_calls == 1);
    CHECK(nd.vertices > 4);
    // center fan + two rings
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

void test_draw_flush()
{
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 200, 200));

    {
        ui u(c);
        u.draw_rect({0, 0, 10, 10}, color::white());

        {
            region clip = u.region({0, 0, 50, 50}, "clip"_id);
            u.draw_rect({0, 0, 10, 10}, color::white());
        }
        CHECK(nd.draw_calls == 1);
        // clip change flushed the first batch
        u.draw_rect({0, 0, 10, 10}, color::white());
    }
    end_frame(c);
    CHECK(nd.draw_calls == 3);
    // + clip-restore flush + final flush      // a texture change
    // flushes the batch too
    texture_handle t1 = nd.create_texture(4, 4, nullptr);
    texture_handle t2 = nd.create_texture(4, 4, nullptr);
    CHECK(t1 != t2);
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 200, 200));

    {
        ui u(c);
        vertex v[4] = {{0, 0, 0, 0, color::white()},
                       {4, 0, 1, 0, color::white()},
                       {4, 4, 1, 1, color::white()},
                       {0, 4, 0, 1, color::white()}};
        i32 idx[6] = {0, 1, 2, 0, 2, 3};
        u.draw_triangles(t1, v, 4, idx, 6);
        u.draw_triangles(t2, v, 4, idx, 6);
        CHECK(nd.draw_calls == 1);
    }
    end_frame(c);
    CHECK(nd.draw_calls == 2);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

void test_blur_fallback()
{
    // A backend without RENDER_TARGETS must fall back to a solid tint.
    struct flat_device : null_device
    {
        backend_caps caps() const override
        {
            return backend_caps::SCISSOR | backend_caps::STREAMING_TEXTURES;
        }
    };
    flat_device nd;
    context *c = create_context(&nd, nd.create_surface());
    nd.my_surface.w = 200;
    nd.my_surface.h = 100;
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        u.blur({10, 10, 80, 40}, 12.0f, 4.0f, 0.8f);
    }
    end_frame(c);
    CHECK(nd.targets == 0);
    // no scratch targets on this backend
    CHECK(nd.draw_calls >= 1);
    // the tint was drawn
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

void test_nine_slice()
{
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    texture_handle tex = nd.create_texture(64, 64, nullptr);
    const skin_image img = make_skin_image(tex, 64, 64, rect::make(0, 0, 48, 48), 8.0f, 8.0f, 8.0f,
                                           8.0f, SKIN_CENTER_STRETCH);
    CHECK(img.valid());
    CHECK(img.uv.x == 0.0f && img.uv.w == 48.0f / 64.0f);
    CHECK(img.src_w == 48.0f && img.src_h == 48.0f);
    CHECK(!make_skin_image(nullptr, 0, 0, rect::make(0, 0, 0, 0)).valid());
    // stretch: exactly nine quads, one batch
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 200, 200));

    {
        ui u(c);
        u.draw_nine_slice(img, rect::make(0, 0, 100, 60));
        CHECK(nd.draw_calls == 0);
    }
    end_frame(c);
    CHECK(nd.draw_calls == 1);
    CHECK(nd.vertices == 9 * 4);
    CHECK(nd.last_texture == tex);
    // tile: repeats the edges/center, so more quads
    skin_image tiled = img;
    tiled.center_mode = SKIN_CENTER_TILE;
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 200, 200));

    {
        ui u(c);
        u.draw_nine_slice(tiled, rect::make(0, 0, 200, 60));
    }
    end_frame(c);
    CHECK(nd.vertices > 9 * 4);
    CHECK(nd.vertices % 4 == 0);
    // none: nothing is scaled past the source slice
    skin_image fixed = img;
    fixed.center_mode = SKIN_CENTER_NONE;
    begin_frame(c, 0.032, 0.016, rect::make(0, 0, 200, 200));

    {
        ui u(c);
        u.draw_nine_slice(fixed, rect::make(0, 0, 200, 60));
    }
    end_frame(c);
    CHECK(nd.vertices >= 4 * 4);
    CHECK(nd.vertices < 9 * 4 * 2);
    // degenerate inputs are no-ops; draw_image emits a single quad
    begin_frame(c, 0.048, 0.016, rect::make(0, 0, 200, 200));

    {
        ui u(c);
        u.draw_nine_slice(skin_image{}, rect::make(0, 0, 100, 100));
        u.draw_nine_slice(img, rect::make(0, 0, 0, 0));
        u.draw_image(img, rect::make(0, 0, 40, 40));
    }
    end_frame(c);
    CHECK(nd.vertices == 4);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

void test_animation_tween()
{
    context *c = create_context(nullptr);
    const rect frame = rect::make(0, 0, 200, 100);
    const tween tw{.duration = 0.2f, .curve = easing::LINEAR};
    f32 v = -1.0f;
    begin_frame(c, 0.0, 0.016, frame);

    {
        ui u(c);
        v = u.animate("h"_id, 100.0f, tw);
        CHECK(v == 0.0f);
        CHECK(u.animations_active());
    }
    end_frame(c);
    begin_frame(c, 0.1, 0.016, frame);

    {
        ui u(c);
        v = u.animate("h"_id, 100.0f, tw);
        CHECK(std::fabs(v - 50.0f) < 0.01f);
    }
    end_frame(c);
    begin_frame(c, 0.2, 0.016, frame);

    {
        ui u(c);
        v = u.animate("h"_id, 100.0f, tw);
        CHECK(v == 100.0f);
        CHECK(!u.animations_active());
    }
    end_frame(c);
    // retarget mid-flight: starts from the current value, no snap
    begin_frame(c, 0.3, 0.016, frame);

    {
        ui u(c);
        v = u.animate("h"_id, 0.0f, tw);
        CHECK(v == 100.0f);
    }
    end_frame(c);
    begin_frame(c, 0.4, 0.016, frame);

    {
        ui u(c);
        v = u.animate("h"_id, 0.0f, tw);
        CHECK(std::fabs(v - 50.0f) < 0.01f);
    }
    end_frame(c);
    // easing shapes the curve: EASE_OUT is past halfway at t = 0.5
    begin_frame(c, 1.0, 0.016, frame);

    {
        ui u(c);
        v = u.animate("e"_id, 100.0f, tween{.duration = 1.0f, .curve = easing::EASE_OUT});
        CHECK(v == 0.0f);
    }
    end_frame(c);
    begin_frame(c, 1.5, 0.016, frame);

    {
        ui u(c);
        v = u.animate("e"_id, 100.0f, tween{.duration = 1.0f, .curve = easing::EASE_OUT});
        CHECK(v > 50.0f && v < 100.0f);
    }
    end_frame(c);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

void test_animation_spring_smooth()
{
    context *c = create_context(nullptr);
    const rect frame = rect::make(0, 0, 200, 100);
    const spring sp{.stiffness = 300.0f, .damping_ratio = 1.0f};
    // settles from 0 to 100 within ~1.5 s at 60 fps
    f64 t = 0.0;
    // 0.0;
    f32 v = 0.0f;
    for (i32 i = 0; i < 90; ++i)
    {
        begin_frame(c, t, 1.0 / 60.0, frame);

        {
            ui u(c);
            v = u.animate("s"_id, 100.0f, sp);
        }
        end_frame(c);
        t += 1.0 / 60.0;
    }
    CHECK(std::fabs(v - 100.0f) < 0.5f);
    begin_frame(c, t, 1.0 / 60.0, frame);

    {
        ui u(c);
        (void)u.animate("s"_id, 100.0f, sp);
        CHECK(!u.animations_active());
    }
    end_frame(c);
    // retargeting mid-flight keeps the value continuous (velocity
    // preserved)
    begin_frame(c, 100.0, 1.0 / 60.0, frame);

    {
        ui u(c);
        v = u.animate("s2"_id, 100.0f, sp);
        CHECK(v >= 0.0f && v < 100.0f);
    }
    end_frame(c);
    begin_frame(c, 100.016, 1.0 / 60.0, frame);

    {
        ui u(c);
        v = u.animate("s2"_id, 100.0f, sp);
        CHECK(v > 0.0f && v < 100.0f);
    }
    end_frame(c);
    const f32 mid = v;
    begin_frame(c, 100.032, 1.0 / 60.0, frame);

    {
        ui u(c);
        const f32 after = u.animate("s2"_id, 0.0f, sp);
        CHECK(std::fabs(after - mid) < 10.0f);
        // no snap on retarget     }
        end_frame(c);
        // smooth: frame-rate-independent approach to a moving
        // target
        begin_frame(c, 200.0, 0.1, frame);

        {
            ui u(c);
            v = u.smooth("sm"_id, 100.0f, 0.1f);
            CHECK(v == 100.0f);
        }
        // starts at target
        end_frame(c);
        begin_frame(c, 200.1, 0.1, frame);

        {
            ui u(c);
            v = u.smooth("sm"_id, 200.0f, 0.1f);
            CHECK(std::fabs(v - 150.0f) < 0.01f);
        }
        end_frame(c);
        // reduced motion snaps everything to the target
        begin_frame(c, 300.0, 1.0 / 60.0, frame);

        {
            ui u(c);
            u.set_reduced_motion(true);
            CHECK(u.animate("r"_id, 100.0f, tween{1.0f, easing::LINEAR}) == 100.0f);
            CHECK(u.animate("r2"_id, 100.0f, sp) == 100.0f);
            CHECK(u.smooth("r3"_id, 50.0f, 0.1f) == 50.0f);
            (void)u.smooth("sm"_id, 150.0f, 0.1f);
            // settle the earlier entry
            CHECK(!u.animations_active());
        }
        end_frame(c);
        CHECK(violation_count(c) == 0);
        destroy_context(c);
    }
}

void test_animation_scope_color_appear()
{
    context *c = create_context(nullptr);
    const rect frame = rect::make(0, 0, 200, 100);
    const tween tw{.duration = 0.2f, .curve = easing::LINEAR};
    // region scoping: the same key in two regions animates independently
    f32 a = -1.0f, b = -1.0f;
    begin_frame(c, 0.0, 0.016, frame);

    {
        ui u(c);

        {
            region ra = u.region({0, 0, 100, 100}, "ra"_id);
            a = u.animate("v"_id, 100.0f, tw);
        }

        {
            region rb = u.region({100, 0, 100, 100}, "rb"_id);
            b = u.animate("v"_id, 200.0f, tw);
        }
        CHECK(a == 0.0f && b == 0.0f);
    }
    end_frame(c);
    begin_frame(c, 0.1, 0.016, frame);

    {
        ui u(c);

        {
            region ra = u.region({0, 0, 100, 100}, "ra"_id);
            a = u.animate("v"_id, 100.0f, tw);
        }

        {
            region rb = u.region({100, 0, 100, 100}, "rb"_id);
            b = u.animate("v"_id, 200.0f, tw);
        }
        CHECK(std::fabs(a - 50.0f) < 0.01f);
        CHECK(std::fabs(b - 100.0f) < 0.01f);
    }
    end_frame(c);
    // color tweens interpolate every channel
    begin_frame(c, 1.0, 0.016, frame);

    {
        ui u(c);
        const color col = u.animate_color("col"_id, color{200, 100, 50, 255}, tw);
        CHECK(col.r == 0 && col.g == 0 && col.a == 0);
    }
    end_frame(c);
    begin_frame(c, 1.1, 0.016, frame);

    {
        ui u(c);
        const color col = u.animate_color("col"_id, color{200, 100, 50, 255}, tw);
        CHECK(std::fabs(static_cast<f32>(col.r) - 100.0f) <= 1.0f);
        CHECK(std::fabs(static_cast<f32>(col.g) - 50.0f) <= 1.0f);
        CHECK(col.a == 128);
    }
    end_frame(c);
    // appear: 0 on the first frame the key is seen, 1 after the
    // duration
    f32 ap = -1.0f;
    begin_frame(c, 2.0, 0.016, frame);

    {
        ui u(c);
        ap = u.appear("ap"_id, 0.2f, easing::LINEAR);
        CHECK(ap == 0.0f);
    }
    end_frame(c);
    begin_frame(c, 2.2, 0.016, frame);

    {
        ui u(c);
        ap = u.appear("ap"_id, 0.2f, easing::LINEAR);
        CHECK(ap == 1.0f);
    }
    end_frame(c);
    // unused keys are collected after 5 s: re-issuing restarts
    // from 0
    begin_frame(c, 10.0, 0.016, frame);

    {
        ui u(c);
        (void)u.animate("gone"_id, 100.0f, tw);
    }
    end_frame(c);
    begin_frame(c, 10.1, 0.016, frame);

    {
        ui u(c);
        const f32 mid = u.animate("gone"_id, 100.0f, tw);
        CHECK(std::fabs(mid - 50.0f) < 0.01f);
    }
    end_frame(c);
    begin_frame(c, 20.0, 0.016, frame);
    // 9.9 s idle: collected at this boundary
    {
        ui u(c);
        const f32 restarted = u.animate("gone"_id, 100.0f, tw);
        CHECK(restarted == 0.0f);
    }
    end_frame(c);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

void test_violation_overlay()
{
    // violation_last reports the exact guard message (stable string literals
    // the overlay and tests may rely on), and the overlay draws only when the
    // context has violations - release frames stay clean. Expected violations
    // are captured, not printed.
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    set_violation_handler(c, capture_violation, nullptr);
    g_violation_events = 0;
    CHECK(std::strcmp(violation_last(c), "") == 0);

    // no violations: nothing is drawn, nothing is reported
    i32 draws_before = nd.draw_calls;
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 200, 200));
    {
        ui u(c);
        draw_violation_overlay(u, rect::make(8, 150, 300, 40));
    }
    end_frame(c);
    CHECK(nd.draw_calls == draws_before);
    CHECK(std::strcmp(violation_last(c), "") == 0);

    // one violation: the overlay draws, the message is exact, and the code is
    // the stable identity
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 200, 200));
    {
        ui u(c);
        {
            region a = u.region(rect::make(0, 0, 50, 50), "dup"_id);
        }
        {
            region b = u.region(rect::make(60, 0, 50, 50), "dup"_id);
        }
    }
    end_frame(c);
    CHECK(violation_count(c) == 1);
    CHECK(std::strcmp(violation_last(c), "duplicate region id among siblings") == 0);
    CHECK(violation_last_code(c) == VIOL_DUP_REGION_ID);
    CHECK(std::strcmp(violation_code_name(VIOL_DUP_REGION_ID), "dup_region") == 0);
    CHECK(std::strcmp(violation_code_name(VIOL_NONE), "none") == 0);

    draws_before = nd.draw_calls;
    begin_frame(c, 0.032, 0.016, rect::make(0, 0, 200, 200));
    {
        ui u(c);
        draw_violation_overlay(u, rect::make(8, 150, 300, 40));
    }
    end_frame(c);
    CHECK(nd.draw_calls > draws_before); // the strip + the border + the text

    // the window's close path keeps reporting too (same message)
    destroy_context(c);
}

void test_draw_shapes_coverage()
{
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    // draw_arc: a positive span emits
    // vertices (ring of quads). One shape
    // per     // frame: the device's
    // counters only advance at flush (end
    // of frame).
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 400, 120));

    {
        ui u(c);
        u.draw_arc({100, 60}, 40.0f, 6.0f, 0.0f, 1.5f, color::white());
    }
    end_frame(c);
    CHECK(nd.vertices > 8);
    // A zero-span arc draws nothing (the
    // sector guard rejects degenerate
    // spans).
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 400, 120));

    {
        ui u(c);
        u.draw_arc({100, 60}, 40.0f, 6.0f, 0.0f, 0.0f, color::white());
    }
    end_frame(c);
    CHECK(nd.vertices == 0);
    // draw_line: an axis-aligned line is a
    // flat quad; a diagonal line is a real
    // // drawn segment (the half-plane
    // split), and a zero-length line is a
    // dot.
    begin_frame(c, 0.032, 0.016, rect::make(0, 0, 400, 120));

    {
        ui u(c);
        u.draw_line(10, 10, 60, 10, color::white());
    }
    end_frame(c);
    CHECK(nd.vertices == 4);
    begin_frame(c, 0.048, 0.016, rect::make(0, 0, 400, 120));

    {
        ui u(c);
        u.draw_line(10, 10, 80, 60, color::white());
        // diagonal: was silent before     }
        end_frame(c);
        CHECK(nd.vertices >= 4);
        begin_frame(c, 0.064, 0.016, rect::make(0, 0, 400, 120));

        {
            ui u(c);
            u.draw_line(10, 10, 10, 10, color::white());
            // dot     }
            end_frame(c);
            CHECK(nd.vertices >= 4);
            CHECK(violation_count(c) == 0);
            destroy_context(c);
        }
    }
}

void test_shapes()
{
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 100, 100));

    {
        ui u(c);
        u.draw_sector({50, 50}, 10.0f, 30.0f, 0.0f, PI * 0.5f, color::white());
        const std::vector<vec2> quad = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
        u.draw_polygon(quad, color::white());
    }
    end_frame(c);
    CHECK(nd.draw_calls >= 1);
    CHECK(nd.vertices > 4);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

void test_device_contract()
{
    null_device nd;
    CHECK(has_cap(nd.caps(), backend_caps::RENDER_TARGETS));
    CHECK(has_cap(nd.caps(), backend_caps::SCISSOR));
    CHECK(has_cap(nd.caps(), backend_caps::SHARED_DEVICE));
    CHECK(nd.create_surface() != nullptr);
    CHECK(nd.create_surface() != nullptr);
    // shared: a second surface is fine
    CHECK(nd.surfaces_created == 2);
    // A single-window backend refuses the second surface instead of
    // degrading.
    null_device single;
    single.shared = false;
    CHECK(!has_cap(single.caps(), backend_caps::SHARED_DEVICE));
    CHECK(single.create_surface() != nullptr);
    CHECK(single.create_surface() == nullptr);
    // vsync is device-level and mutable
    CHECK(nd.vsync);
    nd.set_vsync(false);
    CHECK(!nd.vsync);
    nd.set_vsync(true);
    CHECK(nd.vsync);
    render_surface *s = nd.create_surface();
    CHECK(s != nullptr);
    context *c = create_context(&nd, s);
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 100, 100));

    {
        ui u(c);
        u.draw_rect({0, 0, 10, 10}, color::white());
    }
    end_frame(c);
    CHECK(nd.draw_calls == 1);
    CHECK(nd.vertices == 4);
    texture_handle target = nd.create_target(64, 64);
    CHECK(target != nullptr);
    CHECK(nd.targets == 1);
    nd.destroy_target(target);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

void test_blur()
{
    null_device nd;
    nd.my_surface.w = 200;
    nd.my_surface.h = 100;
    context *c = create_context(&nd, nd.create_surface());
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        u.draw_rect({0, 0, 200, 100}, color::white());
        u.blur({20, 20, 80, 40}, 12.0f, 4.0f, 0.8f); // frosted panel
    }
    end_frame(c);
    CHECK(nd.targets >= 3);
    // half + quarter (+ eighth)
    CHECK(nd.draw_calls >= 1);
    // background + composite quad
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}
