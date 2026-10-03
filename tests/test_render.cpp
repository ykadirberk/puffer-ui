// render: batching, flush, shapes, blur, device contract (r88 split).
#include "test_util.h"

PUI_TEST(test_draw_list_snapshot)
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

PUI_TEST(test_culling_and_visibility)
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

PUI_TEST(test_identity_scope)
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

PUI_TEST(test_dup_widget_id_and_sizes)
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

PUI_TEST(test_needs_redraw)
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

PUI_TEST(test_draw_batching)
{
    tf_env env;
    context *c = env.c;
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 100, 100));
    ui u(c);
    u.draw_rect({0, 0, 10, 10}, color::white());
    u.draw_line(0, 0, 10, 0, color::white());
    // horizontal -> one more quad
    CHECK(env.nd.draw_calls == 0);
    // still batched, not flushed
    end_frame(c);
    CHECK(env.nd.draw_calls == 1);
    // both quads share one batch
    CHECK(env.nd.vertices == 8);
}

PUI_TEST(test_theme)
{
    context *c = create_context(nullptr);
    theme t = default_dark();
    t.accent = {255, 0, 128, 255};
    set_theme(c, t);
    CHECK(c->active_theme.accent.r == 255 && c->active_theme.accent.g == 0);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

PUI_TEST(test_rounded_rect)
{
    tf_env env;
    context *c = env.c;
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 100, 100));
    ui u(c);
    u.draw_rounded_rect({10, 10, 80, 40}, color::white(), 8.0f);
    end_frame(c);
    CHECK(env.nd.draw_calls == 1);
    CHECK(env.nd.vertices > 4);
    // center fan + two rings
}

PUI_TEST(test_draw_flush)
{
    tf_env env;
    context *c = env.c;
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 200, 200));

    {
        ui u(c);
        u.draw_rect({0, 0, 10, 10}, color::white());

        {
            region clip = u.region({0, 0, 50, 50}, "clip"_id);
            u.draw_rect({0, 0, 10, 10}, color::white());
        }
        CHECK(env.nd.draw_calls == 1);
        // clip change flushed the first batch
        u.draw_rect({0, 0, 10, 10}, color::white());
    }
    end_frame(c);
    CHECK(env.nd.draw_calls == 3);
    // + clip-restore flush + final flush      // a texture change
    // flushes the batch too
    texture_handle t1 = env.nd.create_texture(4, 4, nullptr);
    texture_handle t2 = env.nd.create_texture(4, 4, nullptr);
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
        CHECK(env.nd.draw_calls == 1);
    }
    end_frame(c);
    CHECK(env.nd.draw_calls == 2);
}

PUI_TEST(test_blur_fallback)
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

PUI_TEST(test_nine_slice)
{
    tf_env env;
    context *c = env.c;
    texture_handle tex = env.nd.create_texture(64, 64, nullptr);
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
        CHECK(env.nd.draw_calls == 0);
    }
    end_frame(c);
    CHECK(env.nd.draw_calls == 1);
    CHECK(env.nd.vertices == 9 * 4);
    CHECK(env.nd.last_texture == tex);
    // tile: repeats the edges/center, so more quads
    skin_image tiled = img;
    tiled.center_mode = SKIN_CENTER_TILE;
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 200, 200));

    {
        ui u(c);
        u.draw_nine_slice(tiled, rect::make(0, 0, 200, 60));
    }
    end_frame(c);
    CHECK(env.nd.vertices > 9 * 4);
    CHECK(env.nd.vertices % 4 == 0);
    // none: nothing is scaled past the source slice
    skin_image fixed = img;
    fixed.center_mode = SKIN_CENTER_NONE;
    begin_frame(c, 0.032, 0.016, rect::make(0, 0, 200, 200));

    {
        ui u(c);
        u.draw_nine_slice(fixed, rect::make(0, 0, 200, 60));
    }
    end_frame(c);
    CHECK(env.nd.vertices >= 4 * 4);
    CHECK(env.nd.vertices < 9 * 4 * 2);
    // degenerate inputs are no-ops; draw_image emits a single quad
    begin_frame(c, 0.048, 0.016, rect::make(0, 0, 200, 200));

    {
        ui u(c);
        u.draw_nine_slice(skin_image{}, rect::make(0, 0, 100, 100));
        u.draw_nine_slice(img, rect::make(0, 0, 0, 0));
        u.draw_image(img, rect::make(0, 0, 40, 40));
    }
    end_frame(c);
    CHECK(env.nd.vertices == 4);
}

PUI_TEST(test_animation_tween)
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

PUI_TEST(test_animation_spring_smooth)
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

PUI_TEST(test_animation_scope_color_appear)
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

PUI_TEST(test_violation_overlay)
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

PUI_TEST(test_draw_shapes_coverage)
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

PUI_TEST(test_shapes)
{
    tf_env env;
    context *c = env.c;
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 100, 100));

    {
        ui u(c);
        u.draw_sector({50, 50}, 10.0f, 30.0f, 0.0f, PI * 0.5f, color::white());
        const std::vector<vec2> quad = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
        u.draw_polygon(quad, color::white());
    }
    end_frame(c);
    CHECK(env.nd.draw_calls >= 1);
    CHECK(env.nd.vertices > 4);
}

PUI_TEST(test_device_contract)
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

PUI_TEST(test_blur)
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

PUI_TEST(test_spring_survives_long_frames)
{
    // A frame can be far longer than the spring's natural period (a window drag,
    // a debugger stop): the integrator must stay stable and still arrive.
    tf_env env;
    const spring sp{.stiffness = 380.0f, .damping_ratio = 0.55f};
    for (const f64 dt : {0.05, 0.25, 0.9})
    {
        f32 worst = 0.0f, last = 0.0f;
        const uiid key = id_child("spring_dt"_id, static_cast<uiid>(dt * 1000.0));
        f64 now = 0.0;
        for (i32 frame = 0; frame < 40; ++frame)
        {
            now += dt;
            begin_frame(env.c, now, dt, rect::make(0, 0, 300, 200));
            {
                ui u(env.c);
                last = u.animate_global(key, 1.0f, sp);
            }
            end_frame(env.c);
            worst = max2(worst, last < 0.0f ? -last : last);
        }
        CHECK(worst < 2.0f);                 // bounded: no blow-up
        CHECK(last > 0.99f && last < 1.01f); // and it arrived at the target
    }
}

PUI_TEST(test_request_redraw)
{
    // Ambient motion that is not a keyed animation keeps frames coming by asking
    // every frame; the request lapses with the first frame that does not repeat it.
    tf_env env;
    env.frame(0.0, [](ui &) {});
    CHECK(!needs_redraw(env.c));

    env.frame(0.016, [](ui &u) { u.request_redraw(); });
    CHECK(needs_redraw(env.c));
    env.frame(0.032, [](ui &u) { u.request_redraw(); });
    CHECK(needs_redraw(env.c));

    env.frame(0.048, [](ui &) {});
    CHECK(!needs_redraw(env.c)); // not repeated: the app may sleep again
}

namespace
{
std::vector<vertex> rounded_fill(vertex_log_device &dev, context *c, rect r,
                                 const corner_radii &radii)
{
    dev.log.clear();
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 300, 200));
    {
        ui u(c);
        u.draw_rounded_rect(r, color{255, 255, 255, 255}, radii);
    }
    end_frame(c);
    return dev.log;
}
} // namespace

PUI_TEST(test_rounded_rect_corner_radii)
{
    // A corner with radius 0 is square: the fill reaches the exact corner point.
    // A rounded corner stays well away from it.
    vertex_log_device dev;
    context *c = create_context(&dev, dev.create_surface());
    set_violation_handler(c, capture_violation, nullptr);
    g_violation_events = 0;
    const rect r = rect::make(20, 20, 100, 60);
    const f32 right = r.right(), bottom = r.bottom();

    std::vector<vertex> top = rounded_fill(dev, c, r, corner_radii::top(16.0f));
    CHECK(!vertex_near(top, r.x, r.y, 3.0f, 255));      // top-left rounded
    CHECK(!vertex_near(top, right, r.y, 3.0f, 255));    // top-right rounded
    CHECK(vertex_near(top, right, bottom, 0.01f, 255)); // bottom-right square
    CHECK(vertex_near(top, r.x, bottom, 0.01f, 255));   // bottom-left square

    std::vector<vertex> bottom_only = rounded_fill(dev, c, r, corner_radii::bottom(16.0f));
    CHECK(vertex_near(bottom_only, r.x, r.y, 0.01f, 255));
    CHECK(vertex_near(bottom_only, right, r.y, 0.01f, 255));
    CHECK(!vertex_near(bottom_only, right, bottom, 3.0f, 255));
    CHECK(!vertex_near(bottom_only, r.x, bottom, 3.0f, 255));

    // radii may differ per corner (a "leaf": opposite corners round)
    std::vector<vertex> leaf = rounded_fill(dev, c, r, {.tl = 24.0f, .br = 24.0f});
    CHECK(!vertex_near(leaf, r.x, r.y, 3.0f, 255));
    CHECK(vertex_near(leaf, right, r.y, 0.01f, 255));
    CHECK(!vertex_near(leaf, right, bottom, 3.0f, 255));
    CHECK(vertex_near(leaf, r.x, bottom, 0.01f, 255));

    // all corners through the struct == the plain radius overload, vertex for vertex
    std::vector<vertex> a = rounded_fill(dev, c, r, corner_radii::all(12.0f));
    dev.log.clear();
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 300, 200));
    {
        ui u(c);
        u.draw_rounded_rect(r, color{255, 255, 255, 255}, 12.0f);
    }
    end_frame(c);
    CHECK(a.size() == dev.log.size());
    bool same = a.size() == dev.log.size();
    for (size_t i = 0; same && i < a.size(); ++i)
        same = a[i].x == dev.log[i].x && a[i].y == dev.log[i].y;
    CHECK(same);

    // all radii zero is a plain rect; oversized radii are clamped to half the short side
    CHECK(rounded_fill(dev, c, r, {}).size() == 4);
    std::vector<vertex> huge = rounded_fill(dev, c, r, corner_radii::all(1000.0f));
    CHECK(!huge.empty() && !vertex_near(huge, r.x, r.y, 3.0f, 255));

    CHECK(violation_count(c) == 0);
    CHECK(g_violation_events == 0);
    destroy_context(c);
}

PUI_TEST(test_blur_corner_radii)
{
    // The blur composite follows the same per-corner shape, so a blurred panel can
    // have square edges where it docks to something.
    vertex_log_device dev;
    dev.my_surface.w = 300;
    dev.my_surface.h = 200;
    context *c = create_context(&dev, dev.create_surface());
    set_violation_handler(c, capture_violation, nullptr);
    g_violation_events = 0;
    const rect r = rect::make(40, 40, 120, 80);

    auto composite = [&](const corner_radii &radii)
    {
        dev.log.clear();
        begin_frame(c, 0.0, 0.016, rect::make(0, 0, 300, 200));
        {
            ui u(c);
            u.draw_rect(rect::make(0, 0, 300, 200), color{40, 80, 160, 255}); // something to blur
            u.blur(r, 12.0f, radii, 1.0f);
        }
        end_frame(c);
        return dev.log;
    };

    std::vector<vertex> bottom = composite(corner_radii::bottom(18.0f));
    CHECK(vertex_near(bottom, r.x, r.y, 0.01f, 255));
    CHECK(vertex_near(bottom, r.right(), r.y, 0.01f, 255));
    CHECK(!vertex_near(bottom, r.x, r.bottom(), 3.0f, 255));
    CHECK(!vertex_near(bottom, r.right(), r.bottom(), 3.0f, 255));

    std::vector<vertex> all = composite(corner_radii::all(18.0f));
    CHECK(!vertex_near(all, r.x, r.y, 3.0f, 255));

    // the plain overload still means "all corners"
    dev.log.clear();
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 300, 200));
    {
        ui u(c);
        u.draw_rect(rect::make(0, 0, 300, 200), color{40, 80, 160, 255});
        u.blur(r, 12.0f, 18.0f, 1.0f);
    }
    end_frame(c);
    CHECK(dev.log.size() == all.size());

    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

PUI_TEST(test_widget_corner_radii)
{
    // A button and a text field can round only some corners (a field flush against
    // its button): the fill and the outline both follow, and the default stays
    // all-corners. Checked on geometry: a square corner has a vertex exactly on
    // the corner point, a rounded one stays away from it.
    vertex_log_device dev;
    context *c = create_context(&dev, dev.create_surface());
    set_violation_handler(c, capture_violation, nullptr);
    g_violation_events = 0;
    const rect r = rect::make(20, 20, 120, 40);
    const f32 right = r.right(), bottom = r.bottom();
    const color border{1, 2, 3, 255};

    auto frame = [&](auto &&draw)
    {
        dev.log.clear();
        begin_frame(c, 0.0, 0.016, rect::make(0, 0, 300, 200));
        {
            ui u(c);
            draw(u);
        }
        end_frame(c);
        return dev.log;
    };

    // --- button: opaque fill (classic two-rect path)
    theme t = default_dark();
    t.button.bg = {40, 80, 160, 255};
    t.button.border_thickness = 0.0f;
    set_theme(c, t);
    std::vector<vertex> b = frame(
        [&](ui &u)
        {
            (void)u.button(r, "", "b"_id, 0,
                           button_override{.radii = some(corner_radii::right(14.0f))});
        });
    CHECK(vertex_near(b, r.x, r.y, 0.01f, 255)); // left corners square
    CHECK(vertex_near(b, r.x, bottom, 0.01f, 255));
    CHECK(!vertex_near(b, right, r.y, 3.0f, 255)); // right corners rounded
    CHECK(!vertex_near(b, right, bottom, 3.0f, 255));

    // --- button: translucent fill + outline ring in a unique color
    t.button.bg = {255, 255, 255, 30};
    t.button.border = border;
    t.button.border_thickness = 2.0f;
    set_theme(c, t);
    auto has_border = [&](const std::vector<vertex> &log, f32 x, f32 y)
    {
        for (const vertex &v : log)
            if (v.c.r == border.r && v.c.g == border.g && v.c.b == border.b && v.c.a == 255 &&
                std::fabs(v.x - x) <= 0.01f && std::fabs(v.y - y) <= 0.01f)
                return true;
        return false;
    };
    std::vector<vertex> ring = frame(
        [&](ui &u)
        {
            (void)u.button(r, "", "b"_id, 0,
                           button_override{.radii = some(corner_radii::right(14.0f))});
        });
    CHECK(has_border(ring, r.x, r.y)); // the outline reaches the square corner ...
    CHECK(has_border(ring, r.x, bottom));
    CHECK(!has_border(ring, right, r.y)); // ... and starts after the arc on a round one
    CHECK(!has_border(ring, right, bottom));

    // the default is every corner rounded (no behavior change without the option)
    std::vector<vertex> all = frame([&](ui &u) { (void)u.button(r, "", "b"_id); });
    CHECK(!has_border(all, r.x, r.y) && !has_border(all, right, bottom));

    // --- text field
    t = default_dark();
    set_theme(c, t);
    std::string value;
    std::vector<vertex> f = frame(
        [&](ui &u)
        {
            (void)u.text_field(r, value, "f"_id,
                               field_opts{.radii = some(corner_radii::left(14.0f))});
        });
    CHECK(!vertex_near(f, r.x, r.y, 3.0f, 255)); // left rounded
    CHECK(!vertex_near(f, r.x, bottom, 3.0f, 255));
    CHECK(vertex_near(f, right, r.y, 0.01f, 255)); // right square
    CHECK(vertex_near(f, right, bottom, 0.01f, 255));

    CHECK(violation_count(c) == 0);
    CHECK(g_violation_events == 0);
    destroy_context(c);
}

// Regression: dl_prepare read a zero-size clip as "no scissor", so everything
// drawn inside a collapsed region painted across the whole window.
PUI_TEST(test_empty_clip_paints_nothing)
{
    tf_env env;
    const i32 base = env.nd.vertices;
    env.frame(0.0,
              [&](ui &u)
              {
                  region r = u.region(rect::make(10, 10, 50, 0), "collapsed"_id);
                  u.draw_rect(rect::make(0, 0, 100, 100), color::white());
                  u.draw_rounded_rect(rect::make(0, 0, 100, 100), color::white(), 8.0f);
              });
    CHECK(env.nd.vertices == base); // nothing reached the device

    env.frame(0.016,
              [&](ui &u)
              {
                  region r = u.region(rect::make(10, 10, 50, 50), "open"_id);
                  u.draw_rect(rect::make(0, 0, 100, 100), color::white());
              });
    CHECK(env.nd.vertices > base); // control: a real clip still draws
}

// Regression: free_blur_store forgot the sixteenth-size target (radius > 20).
struct target_counting_device : null_device
{
    i32 destroyed = 0;
    void destroy_target(texture_handle t) override
    {
        ++destroyed;
        null_device::destroy_target(t);
    }
};

PUI_TEST(test_blur_frees_every_target)
{
    target_counting_device nd;
    nd.my_surface.w = 200;
    nd.my_surface.h = 100;
    context *c = create_context(&nd, nd.create_surface());
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 200, 100));
    {
        ui u(c);
        u.draw_rect({0, 0, 200, 100}, color::white());
        u.blur({20, 20, 80, 40}, 30.0f, 4.0f, 0.8f); // radius > 20: all four levels
    }
    end_frame(c);
    CHECK(nd.targets == 4);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
    CHECK(nd.destroyed == nd.targets); // every target the blur made is released
}
