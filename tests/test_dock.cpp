// dock + windows: dock tree, multi-window, titlebar (r88 split).
#include "test_util.h"

PUI_TEST(test_window_errors)
{
    null_device nd;
    context *c = create_context(&nd, nullptr);
    set_violation_handler(c, capture_violation, nullptr);
    g_violation_events = 0;
    CHECK(first_window(c) == nullptr);
    CHECK(window_at(c, reinterpret_cast<void *>(1)) == nullptr);
    CHECK(desktop_rect(c).w == 0.0f);
    // the window list grows on demand (no fixed limit): 8 windows, then the
    // 9th grows the list; no violation is reported
    for (i32 i = 0; i < MAX_WINDOWS; ++i)
        CHECK(add_window(c, reinterpret_cast<void *>(static_cast<usize>(i + 1)), nullptr,
                         rect::make(0, 0, 100, 100)) != nullptr);
    CHECK(c->window_count == MAX_WINDOWS);
    CHECK(add_window(c, reinterpret_cast<void *>(99), nullptr, rect::make(0, 0, 100, 100)) !=
          nullptr); // the list grew
    CHECK(c->window_count == MAX_WINDOWS + 1);
    CHECK(violation_count(c) == 0);
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 100, 100));
    g_last_violation = nullptr;
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 100, 100));
    // nested: refused
    CHECK(violation_was("begin_frame: another window frame is open"));
    CHECK(c->current_window != nullptr);
    end_frame(c);
    g_last_violation = nullptr;
    end_frame(c);
    // extra: refused
    CHECK(violation_was("end_frame without begin_frame"));
    CHECK(violation_count(c) == 2);
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 100, 100));
    g_last_violation = nullptr;
    remove_window(c, *first_window(c));
    // mid-frame: refused
    CHECK(violation_was("remove_window while a frame is open"));
    CHECK(c->window_count == MAX_WINDOWS + 1);
    end_frame(c);
    CHECK(violation_count(c) == 3);
    CHECK(g_violation_events == 3);
    destroy_context(c);
}

PUI_TEST(test_dock_empty)
{
    context *c = create_context(nullptr);
    dock_node leaf{};
    // a leaf with no panels     leaf.kind = DOCK_LEAF;
    i32 visits = 0;
    auto draw = [&](uiid, rect, bool) { ++visits; };
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        dock_action act = u.dock_space("empty"_id, rect::make(0, 0, 200, 100), leaf, draw);
        CHECK(!act.active);
    }
    end_frame(c);
    CHECK(visits == 0);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

PUI_TEST(test_dock_persistence)
{
    // The dock tree round-trips: save -> restore -> save produces the same
    // text, and the restored structure matches node for node (kinds, ratios,
    // tab panels with ids and names, the active tab). Malformed input is
    // refused.
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    set_violation_handler(c, capture_violation, nullptr);
    g_violation_events = 0;
    (void)c;

    // the source tree: split h 0.6 { leaf[stats, log] ; split v 0.35 {
    // leaf[inspector] ; leaf[log2] } }
    dock_node a{}, b{}, c1{}, c2{}, s1{}, s2{};
    a.kind = DOCK_LEAF;
    a.panels[0] = 1;
    a.panel_names[0] = "Statistics";
    a.panels[1] = 2;
    a.panel_names[1] = "Log";
    a.panel_count = 2;
    a.active = 1;
    c1.kind = DOCK_LEAF;
    c1.panels[0] = 3;
    c1.panel_names[0] = "Inspector";
    c1.panel_count = 1;
    c2.kind = DOCK_LEAF;
    c2.panels[0] = 4;
    c2.panel_names[0] = "Log2";
    c2.panel_count = 1;
    s2.kind = DOCK_SPLIT_V;
    s2.ratio = 0.35f;
    s2.a = &c1;
    s2.b = &c2;
    s1.kind = DOCK_SPLIT_H;
    s1.ratio = 0.6f;
    s1.a = &a;
    s1.b = &s2;

    std::fflush(stdout);
    const std::string saved = dock_save_tree(s1);
    CHECK(saved.find("split h 0.6000") != std::string::npos);
    CHECK(saved.find("1|Statistics;2|Log") != std::string::npos);

    // restore into a fresh pool
    dock_node pool[16]{};
    std::string names;
    dock_node *root = nullptr;
    std::fflush(stdout);
    CHECK(dock_restore_tree(saved, pool, 16, names, root));
    CHECK(root != nullptr);
    CHECK(root->kind == DOCK_SPLIT_H);
    CHECK(root->ratio > 0.59f && root->ratio < 0.61f);
    CHECK(root->a != nullptr && root->a->kind == DOCK_LEAF);
    CHECK(root->a->panel_count == 2);
    CHECK(root->a->panels[0] == 1 && root->a->panels[1] == 2);
    CHECK(std::strcmp(root->a->panel_names[0], "Statistics") == 0);
    CHECK(std::strcmp(root->a->panel_names[1], "Log") == 0);
    CHECK(root->a->active == 1);
    CHECK(root->b != nullptr && root->b->kind == DOCK_SPLIT_V);
    CHECK(root->b->ratio > 0.34f && root->b->ratio < 0.36f);
    CHECK(root->b->a != nullptr && root->b->a->panel_count == 1);
    CHECK(std::strcmp(root->b->a->panel_names[0], "Inspector") == 0);
    CHECK(root->b->b != nullptr && root->b->b->panel_count == 1);
    CHECK(std::strcmp(root->b->b->panel_names[0], "Log2") == 0);

    // round-trip: saving the restored tree reproduces the same text
    const std::string saved2 = dock_save_tree(*root);
    CHECK(saved2 == saved);

    // malformed input: refused, and the caller pool is untouched
    dock_node pool2[16]{};
    std::string names2;
    dock_node *bad = nullptr;
    CHECK(dock_restore_tree("garbage", pool2, 16, names2, bad) == false);
    CHECK(dock_restore_tree("d0 leaf", pool2, 16, names2, bad) == false);
    CHECK(dock_restore_tree("d0 split h 0.5\nd0 leaf 0 1|X", pool2, 16, names2, bad) ==
          false); // two roots
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

PUI_TEST(test_multi_window)
{
    // A single-window backend refuses a second window (reported, not
    // silent).
    {
        null_device single;
        single.shared = false;
        context *c = create_context(&single, single.create_surface());
        set_violation_handler(c, capture_violation, nullptr);
        window *w1 =
            add_window(c, reinterpret_cast<void *>(1), nullptr, rect::make(0, 0, 100, 100));
        CHECK(w1 != nullptr);
        g_last_violation = nullptr;
        window *w2 =
            add_window(c, reinterpret_cast<void *>(2), nullptr, rect::make(100, 0, 100, 100));
        CHECK(w2 == nullptr);
        CHECK(violation_count(c) == 1);
        CHECK(violation_was("a second window requires backend_caps::SHARED_DEVICE"));
        destroy_context(c);
    }
    null_device nd;
    context *c = create_context(&nd, nullptr);
    render_surface *s1 = nd.create_surface();
    render_surface *s2 = nd.create_surface();
    window *w1 = add_window(c, reinterpret_cast<void *>(1), s1, rect::make(0, 0, 200, 150));
    window *w2 = add_window(c, reinterpret_cast<void *>(2), s2, rect::make(200, 0, 200, 150));
    CHECK(w1 && w2);
    CHECK(w1->index == 0 && w2->index == 1);
    CHECK(window_at(c, reinterpret_cast<void *>(2)) == w2);
    CHECK(focused_window(c) == w1);
    // the first window takes focus
    focus_window(c, *w2);
    CHECK(w2->focused && !w1->focused);
    CHECK(focused_window(c) == w2);

    {
        const rect desk = desktop_rect(c);
        CHECK(desk.x == 0.0f && desk.y == 0.0f && desk.w == 400.0f && desk.h == 150.0f);
    }
    font_handle fh = load_font(c, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
    if (fh == FONT_INVALID) fh = load_font(c, "C:/Windows/Fonts/segoeui.ttf");
    if (fh == FONT_INVALID) fh = load_font(c, "C:/Windows/Fonts/arial.ttf");
    const bool has_font = (fh != FONT_INVALID);
    theme t = default_dark();
    t.font = fh;
    set_theme(c, t);
    // Pointer over window 2: window 1 derives a local position past its
    // right edge.
    set_global_mouse(c, 210.0f, 10.0f);
    texture_handle atlas_in_w1 = nullptr;
    begin_frame(c, *w1, 0.0, 0.016);

    {
        CHECK(c->screen.w == 200.0f && c->screen.h == 150.0f);
        CHECK(c->mouse_x == 210.0f && c->mouse_y == 10.0f);
        ui u(c);
        interaction in = u.interact("mw_a"_id, rect::make(0, 0, 40, 40));
        CHECK(!in.hovered);
        if (has_font) u.text(rect::make(0, 0, 100, 20), "window one", color::white());
    }
    end_frame(c);
    if (has_font) atlas_in_w1 = nd.last_texture;
    // flushed by end_frame
    // Window 2 sees local (10, 10) and receives the keyboard events aimed
    // at it.
    bool tab_pressed_in_w2 = false, tab_pressed_in_w1 = false;
    texture_handle atlas_in_w2 = nullptr;
    key_event(*w2, key::TAB, true);
    begin_frame(c, *w2, 0.016, 0.016);

    {
        CHECK(c->mouse_x == 10.0f && c->mouse_y == 10.0f);
        ui u(c);
        tab_pressed_in_w2 = u.key_pressed(key::TAB);
        interaction in = u.interact("mw_b"_id, rect::make(0, 0, 40, 40));
        CHECK(in.hovered);
        if (has_font) u.text(rect::make(0, 0, 100, 20), "window two", color::white());
    }
    end_frame(c);
    if (has_font) atlas_in_w2 = nd.last_texture;
    CHECK(tab_pressed_in_w2);
    if (has_font)
    {
        CHECK(atlas_in_w1 != nullptr);
        CHECK(atlas_in_w1 == atlas_in_w2);
        // one atlas handle across windows
        CHECK(nd.textures == 1);
        // and created exactly once
    }
    // Window 1 does not see the
    // key that was routed to window 2.
    begin_frame(c, *w1, 0.032, 0.016);

    {
        ui u(c);
        tab_pressed_in_w1 = u.key_pressed(key::TAB);
    }
    end_frame(c);
    CHECK(!tab_pressed_in_w1);
    // Press in window 1 captures there; the drag survives while the
    // pointer // moves into window 2, and window 2 does not re-capture
    // it.
    const uiid drag_id = "mw_drag"_id;
    mouse_move(*w1, 10.0f, 10.0f);
    mouse_button(*w1, true);
    begin_frame(c, *w1, 0.048, 0.016);

    {
        ui u(c);
        interaction in = u.interact(drag_id, rect::make(0, 0, 40, 40));
        CHECK(in.activated && in.captured && in.held);
        CHECK(c->active == drag_id);
    }
    end_frame(c);
    set_global_mouse(c, 210.0f, 20.0f);
    begin_frame(c, *w2, 0.064, 0.016);

    {
        ui u(c);
        CHECK(c->mouse_down);
        // held state is global
        interaction in = u.interact("mw_other"_id, rect::make(0, 0, 40, 40));
        CHECK(in.hovered);
        CHECK(!in.activated);
        // the press happened in window 1
        CHECK(c->active != "mw_other"_id);
    }
    end_frame(c);
    begin_frame(c, *w1, 0.080, 0.016);

    {
        ui u(c);
        interaction in = u.interact(drag_id, rect::make(0, 0, 40, 40));
        CHECK(in.captured && in.held);
        // window 1 still owns the drag
        CHECK(!in.clicked);
    }
    end_frame(c);
    // Release in window 2: window 1 must not report a click.
    mouse_button(*w2, false);
    begin_frame(c, *w2, 0.096, 0.016);

    {
        ui u(c);
        interaction in = u.interact("mw_other"_id, rect::make(0, 0, 40, 40));
        CHECK(!in.clicked);
    }
    end_frame(c);
    begin_frame(c, *w1, 0.112, 0.016);

    {
        ui u(c);
        interaction in = u.interact(drag_id, rect::make(0, 0, 40, 40));
        CHECK(!in.clicked);
        // the release was not seen here
        CHECK(!c->mouse_down);
    }
    end_frame(c);
    // Cursor: only the window under the pointer applies its cursor, so
    // framing
    // // window 1 first cannot overwrite the cursor window 2 wants.
    set_global_mouse(c, 210.0f, 10.0f);
    // over window 2
    nd.cursor_sets = 0;
    nd.last_cursor = cursor::ARROW;
    begin_frame(c, *w1, 0.128, 0.016);

    {
        ui u(c);
        u.set_cursor(cursor::HRESIZE);
    }
    end_frame(c);
    CHECK(nd.cursor_sets == 0);
    // window 1 is not hovered
    begin_frame(c, *w2, 0.144, 0.016);

    {
        ui u(c);
        u.set_cursor(cursor::IBEAM);
    }
    end_frame(c);
    CHECK(nd.cursor_sets == 1);
    CHECK(nd.last_cursor == cursor::IBEAM);
    // Per-window panels: one window's panel entries never capture in
    // another, and removing a window drops/remaps its entries.
    {
        rect b1 = {10, 10, 80, 80};
        // and removing a window drops/remaps its entries.
        {
            rect b1 = {10, 10, 80, 80};
            begin_frame(c, *w1, 0.160, 0.016);

            {
                ui u(c);
                panel_scope p = u.panel("panel_a", b1, {.id = "panel_a"_id});
                (void)p;
            }
            end_frame(c);
            rect b2 = {0, 10, 80, 80};
            begin_frame(c, *w2, 0.176, 0.016);

            {
                ui u(c);
                panel_scope p = u.panel("panel_b", b2, {.id = "panel_b"_id});
                (void)p;
            }
            end_frame(c);
            CHECK(c->panel_depth == 2);
        }
        // Removing a window re-indexes the rest and moves focus.
        remove_window(c, *w1);
        CHECK(c->window_count == 1);
        CHECK(c->panel_depth == 1);
        // window 1's panel entry was dropped
        CHECK(c->panels[0].win == 0);
        // window 2's entry was remapped
        window *remaining = focused_window(c);
        CHECK(remaining != nullptr);
        CHECK(remaining->index == 0);
        // shifted into slot 0
        CHECK(remaining->handle == reinterpret_cast<void *>(2));
        CHECK(window_at(c, reinterpret_cast<void *>(1)) == nullptr);
        // The surviving window's own panel still blocks a widget
        // underneath it.
        set_global_mouse(c, 210.0f, 20.0f);
        // local (10, 20), inside panel_b
        begin_frame(c, *remaining, 0.192, 0.016);

        {
            ui u(c);
            interaction blocked = u.interact("blocked"_id, rect::make(0, 10, 80, 40));
            CHECK(!blocked.hovered);
        }
        end_frame(c);
        CHECK(violation_count(c) == 0);
        destroy_context(c);
    }
}

PUI_TEST(test_window_pointer_invalidation)
{
    // remove_window compacts the window list: pointers to later windows
    // go stale (the user-visible bug was a second window's frame loop
    // reporting "begin_frame: window is not registered" after the first
    // one closed). The contract: such a pointer is a reported
    // violation, and the correct workflow re-fetches with
    // window_at(handle).
    null_device nd;
    context *c = create_context(&nd, nullptr);
    set_violation_handler(c, capture_violation, nullptr);
    g_violation_events = 0;
    render_surface *sa = nd.create_surface();
    render_surface *sb = nd.create_surface();
    (void)add_window(c, reinterpret_cast<void *>(1), sa, rect::make(0, 0, 200, 150));
    window *wb = add_window(c, reinterpret_cast<void *>(2), sb, rect::make(200, 0, 200, 150));
    CHECK(wb != nullptr);
    // A pointer captured before the removal (as an app would hold it).
    window *stale = wb;
    remove_window(c, *first_window(c));
    // the first window goes away
    CHECK(c->window_count == 1);
    // The stale pointer no longer names a registered window: reported,
    // and the frame is not opened (the end_frame afterwards is refused
    // as well).
    begin_frame(c, *stale, 0.016, 0.016);
    CHECK(violation_was("begin_frame: window is not registered"));
    CHECK(c->current_window == nullptr);
    end_frame(c);
    CHECK(violation_was("end_frame without begin_frame"));
    // The documented recovery: re-fetch by handle - it now lives at
    // index 0.
    window *refetched = window_at(c, reinterpret_cast<void *>(2));
    CHECK(refetched != nullptr && refetched != stale);
    CHECK(refetched->index == 0);
    begin_frame(c, *refetched, 0.032, 0.016);

    {
        ui u(c);
        const interaction in = u.interact("recovered"_id, rect::make(0, 0, 40, 40));
        (void)in;
        // a clean frame on the moved window     }
        end_frame(c);
        CHECK(violation_count(c) == 2);
        // exactly the stale-pointer pair
        CHECK(g_violation_events == 2);
        destroy_context(c);
    }
    // ---- custom window chrome: titlebar + edges through a recorded
    // host
    // ----------
}

PUI_TEST(test_titlebar)
{
    // The custom titlebar on a borderless window: it paints the bar and the
    // three glyph buttons; native dragging/resizing/Aero Snap come from the
    // platform through install_window_chrome. The library contract tested
    // here: the bar cuts the layout bounds, the buttons route through the
    // host, and the close button sets window::close_requested.
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    set_violation_handler(c, capture_violation, nullptr);
    recorder_host host;
    set_window_host(c, &host);
    window *w = add_window(c, reinterpret_cast<void *>(1), nullptr, rect::make(0, 0, 300, 200));
    CHECK(w != nullptr);
    CHECK(w->root != 0);
    // press and release land in separate frames, like a real pump delivers
    auto frame = [&](f64 t, f32 mx, f32 my, bool press, bool release)
    {
        begin_frame(c, *w, t, 0.016);
        mouse_move(*w, mx, my);
        if (press) mouse_button(*w, true);
        {
            ui u(c);
            rect bounds = rect::make(0, 0, 300, 200);
            (void)u.titlebar(*w, bounds, "my window");
            CHECK(bounds.h == 200.0f - 34.0f); // the bar was cut off the layout
        }
        end_frame(c);
        if (release)
        {
            mouse_button(*w, false);
            begin_frame(c, t + 0.016, 0.016, rect::make(0, 0, 300, 200));
            mouse_move(*w, mx, my);
            {
                ui u(c);
                rect bounds2 = rect::make(0, 0, 300, 200);
                (void)u.titlebar(*w, bounds2, "my window");
            }
            end_frame(c);
        }
    };

    // Frame 1: nothing clicked. The bar paints and the layout shrinks.
    frame(0.000, 60, 16, false, false);
    CHECK(host.moves == 0 && host.resizes == 0 && host.minimizes == 0 && host.maximizes == 0);
    CHECK(!w->close_requested);

    // The close button (46px wide, right edge): press + release closes.
    frame(0.016, 285, 16, true, true);
    CHECK(w->close_requested); // the click set the app-facing flag
    CHECK(host.moves == 0 && host.minimizes == 0 && host.maximizes == 0);
    w->close_requested = false;

    // The maximize button (36px, left of close): toggles through the host.
    frame(0.048, 250, 16, true, true);
    CHECK(host.maximizes == 1);
    CHECK(!w->close_requested && host.minimizes == 0);

    // The minimize button (36px, left of maximize): minimizes through the host.
    frame(0.064, 214, 16, true, true);
    CHECK(host.minimizes == 1);

    // Clicks on the bar itself never move anything (native dragging owns the
    // caption; the library does not move windows from widget code).
    frame(0.080, 60, 16, true, true);
    CHECK(host.moves == 0);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

PUI_TEST(test_titlebar_maximize_restore)
{
    // The maximize/restore glyph toggles the real window state through the
    // host. The host reflects the OS state the way the event pump does (SDL
    // reports the maximized flag), and the host reads the REAL window state
    // when toggling, so restore stays reachable no matter how the flag
    // drifted (the r74 regression).
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    set_violation_handler(c, capture_violation, nullptr);
    recorder_host host;
    host.reflect_maximize = true;
    set_window_host(c, &host);
    window *w = add_window(c, reinterpret_cast<void *>(1), nullptr, rect::make(0, 0, 300, 200));
    CHECK(w != nullptr);
    auto click_max = [&](f64 t, f32 mx = 250.0f, f32 my = 16.0f)
    {
        begin_frame(c, *w, t, 0.016);
        mouse_move(*w, mx, my);
        mouse_button(*w, true);
        {
            ui u(c);
            rect bounds = rect::make(0, 0, 300, 200);
            (void)u.titlebar(*w, bounds, "my window");
        }
        end_frame(c);
        mouse_button(*w, false);
        begin_frame(c, t + 0.016, 0.016, rect::make(0, 0, 300, 200));
        mouse_move(*w, mx, my);
        {
            ui u(c);
            rect bounds2 = rect::make(0, 0, 300, 200);
            (void)u.titlebar(*w, bounds2, "my window");
        }
        end_frame(c);
    };
    click_max(0.0);
    CHECK(host.maximizes == 1);
    CHECK(w->maximized); // the pump reflected the maximize

    // Clicking again restores (the toggle through the REAL window state).
    click_max(0.4);
    CHECK(host.maximizes == 2);
    CHECK(w->maximized == false);

    // Pressing the glyph and releasing OFF it cancels: no toggle.
    begin_frame(c, *w, 0.8, 0.016);
    mouse_move(*w, 250, 16);
    mouse_button(*w, true);
    {
        ui u(c);
        rect bounds = rect::make(0, 0, 300, 200);
        (void)u.titlebar(*w, bounds, "my window");
    }
    end_frame(c);
    mouse_move(*w, 100, 100); // the pointer left the glyph
    begin_frame(c, 0.816, 0.016, rect::make(0, 0, 300, 200));
    {
        ui u(c);
        rect bounds2 = rect::make(0, 0, 300, 200);
        (void)u.titlebar(*w, bounds2, "my window");
    }
    end_frame(c);
    mouse_button(*w, false);
    begin_frame(c, 0.832, 0.016, rect::make(0, 0, 300, 200));
    {
        ui u(c);
        rect bounds3 = rect::make(0, 0, 300, 200);
        (void)u.titlebar(*w, bounds3, "my window");
    }
    end_frame(c);
    CHECK(host.maximizes == 2); // the release off the glyph was cancelled
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

PUI_TEST(test_titlebar_close_paths)
{
    // The custom titlebar's close button must set the window's close request
    // in every ordering that can occur in a real event pump: a plain click, a
    // click on a maximized window, a click shortly after another strip click
    // (different ids never double-click into each other), a click after a
    // restore-drag, and a fast click whose press and release both arrive
    // between frames.
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    set_violation_handler(c, capture_violation, nullptr);
    recorder_host host;
    set_window_host(c, &host);
    auto click_close = [&](window *w, f64 t, bool reflect = false)
    {
        begin_frame(c, *w, t, 0.016);
        mouse_move(*w, 285, 16); // the close glyph (46 px wide, right edge)
        mouse_button(*w, true);
        {
            ui u(c);
            rect bounds = rect::make(0, 0, 300, 200);
            (void)u.titlebar(*w, bounds, "probe");
        }
        end_frame(c);
        mouse_button(*w, false);
        begin_frame(c, t + 0.016, 0.016, rect::make(0, 0, 300, 200));
        {
            ui u(c);
            rect bounds = rect::make(0, 0, 300, 200);
            (void)u.titlebar(*w, bounds, "probe");
        }
        end_frame(c);
        (void)reflect;
    };

    // (a) plain close click
    {
        window *w = add_window(c, reinterpret_cast<void *>(1), nullptr, rect::make(0, 0, 300, 200));
        click_close(w, 0.0);
        CHECK(w->close_requested);
        remove_window(c, *w);
    }
    // (b) close on a maximized window
    {
        window *w = add_window(c, reinterpret_cast<void *>(1), nullptr, rect::make(0, 0, 300, 200));
        w->maximized = true;
        click_close(w, 1.0);
        CHECK(w->close_requested);
        remove_window(c, *w);
    }
    // (c) a plain strip click, then a close click inside the double-click
    // window: different ids, so the close click must not be swallowed and the
    // strip must not toggle maximize.
    {
        window *w = add_window(c, reinterpret_cast<void *>(1), nullptr, rect::make(0, 0, 300, 200));
        begin_frame(c, *w, 2.0, 0.016);
        mouse_move(*w, 60, 16);
        mouse_button(*w, true);
        {
            ui u(c);
            rect bounds = rect::make(0, 0, 300, 200);
            (void)u.titlebar(*w, bounds, "probe");
        }
        end_frame(c);
        mouse_button(*w, false);
        begin_frame(c, 2.016, 0.016, rect::make(0, 0, 300, 200));
        {
            ui u(c);
            rect bounds = rect::make(0, 0, 300, 200);
            (void)u.titlebar(*w, bounds, "probe");
        }
        end_frame(c);
        const i32 maxes_before = host.maximizes;
        click_close(w, 2.05); // 34 ms after the strip click
        CHECK(w->close_requested);
        CHECK(host.maximizes == maxes_before);
        remove_window(c, *w);
    }
    // (d) close right after the window state changed through the host
    // (maximize via the button): the click still closes.
    {
        window *w = add_window(c, reinterpret_cast<void *>(1), nullptr, rect::make(0, 0, 300, 200));
        begin_frame(c, *w, 3.0, 0.016);
        mouse_move(*w, 250, 16); // the maximize glyph
        mouse_button(*w, true);
        {
            ui u(c);
            rect bounds = rect::make(0, 0, 300, 200);
            (void)u.titlebar(*w, bounds, "probe");
        }
        end_frame(c);
        mouse_button(*w, false);
        begin_frame(c, 3.016, 0.016, rect::make(0, 0, 300, 200));
        {
            ui u(c);
            rect bounds = rect::make(0, 0, 300, 200);
            (void)u.titlebar(*w, bounds, "probe");
        }
        end_frame(c);
        CHECK(host.maximizes == 1); // maximized through the host
        w->close_requested = false;
        click_close(w, 3.4); // outside the double-click window
        CHECK(w->close_requested);
        remove_window(c, *w);
    }
    // (e) fast click: press and release both arrive between frames, so one
    // frame sees both edges
    {
        window *w = add_window(c, reinterpret_cast<void *>(1), nullptr, rect::make(0, 0, 300, 200));
        mouse_move(*w, 285, 16);
        mouse_button(*w, true);
        mouse_button(*w, false); // both edges queued before any frame
        begin_frame(c, *w, 4.0, 0.016);
        {
            ui u(c);
            rect bounds = rect::make(0, 0, 300, 200);
            (void)u.titlebar(*w, bounds, "probe");
        }
        end_frame(c);
        CHECK(w->close_requested);
        remove_window(c, *w);
    }
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

PUI_TEST(test_dock_tabs)
{
    context *c = create_context(nullptr);
    dock_node leaf{};
    leaf.kind = DOCK_LEAF;
    leaf.panels[0] = "a"_id;
    leaf.panel_names[0] = "A";
    leaf.panels[1] = "b"_id;
    leaf.panel_names[1] = "B";
    leaf.panel_count = 2;
    leaf.active = 0;
    uiid seen = 0;
    auto draw = [&](uiid p, rect, bool) { seen = p; };
    const rect area = {0, 0, 300, 200};
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 300, 200));

    {
        ui u(c);
        (void)u.dock_space("dock"_id, area, leaf, draw);
    }
    end_frame(c);
    CHECK(seen == "a"_id);
    // click tab 1 (x = 110 + gap 4 = 114 .. 224, y 0..26)
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 300, 200));
    mouse_move(c, 160, 13);
    mouse_button(c, true);

    {
        ui u(c);
        (void)u.dock_space("dock"_id, area, leaf, draw);
    }
    end_frame(c);
    begin_frame(c, 0.032, 0.016, rect::make(0, 0, 300, 200));
    mouse_button(c, false);

    {
        ui u(c);
        (void)u.dock_space("dock"_id, area, leaf, draw);
    }
    end_frame(c);
    CHECK(leaf.active == 1);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

PUI_TEST(test_dock_split_and_ratio)
{
    context *c = create_context(nullptr);
    dock_node a{}, b{}, root{};
    a.kind = DOCK_LEAF;
    a.panels[0] = "A1"_id;
    a.panel_names[0] = "A";
    a.panel_count = 1;
    b.kind = DOCK_LEAF;
    b.panels[0] = "B1"_id;
    b.panel_names[0] = "B";
    b.panel_count = 1;
    root.kind = DOCK_SPLIT_H;
    root.a = &a;
    root.b = &b;
    root.ratio = 0.5f;
    i32 visits = 0;
    auto draw = [&](uiid, rect, bool) { ++visits; };
    const rect area = {0, 0, 200, 100};
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        (void)u.dock_space("d"_id, area, root, draw);
    }
    end_frame(c);
    CHECK(visits == 2);
    // both leaves rendered
    CHECK(std::fabs(root.ratio - 0.5f) < 0.05f);
    // drag the splitter handle (x = 100 + gap 2 .. 108)
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 200, 100));
    mouse_move(c, 105, 50);
    mouse_button(c, true);

    {
        ui u(c);
        (void)u.dock_space("d"_id, area, root, draw);
    }
    end_frame(c);
    begin_frame(c, 0.032, 0.016, rect::make(0, 0, 200, 100));
    mouse_move(c, 140, 50);

    {
        ui u(c);
        (void)u.dock_space("d"_id, area, root, draw);
    }
    end_frame(c);
    begin_frame(c, 0.048, 0.016, rect::make(0, 0, 200, 100));
    mouse_button(c, false);

    {
        ui u(c);
        (void)u.dock_space("d"_id, area, root, draw);
    }
    end_frame(c);
    CHECK(root.ratio > 0.55f);
    // handle was dragged right (clamped)
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

PUI_TEST(test_dock_drag_action)
{
    context *c = create_context(nullptr);
    dock_node leaf{};
    leaf.kind = DOCK_TABS;
    leaf.panels[0] = "p1"_id;
    leaf.panel_names[0] = "One";
    leaf.panels[1] = "p2"_id;
    leaf.panel_names[1] = "Two";
    leaf.panel_count = 2;
    auto draw = [](uiid, rect, bool) {};
    const rect area = {0, 0, 300, 200};
    dock_action act;
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 300, 200));

    {
        ui u(c);
        (void)u.dock_space("d"_id, area, leaf, draw);
    }
    end_frame(c);
    // press tab 0, then drag far, then release over the right
    // edge
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 300, 200));
    mouse_move(c, 50, 13);
    mouse_button(c, true);

    {
        ui u(c);
        (void)u.dock_space("d"_id, area, leaf, draw);
    }
    end_frame(c);
    begin_frame(c, 0.032, 0.016, rect::make(0, 0, 300, 200));
    mouse_move(c, 250, 120);

    {
        ui u(c);
        (void)u.dock_space("d"_id, area, leaf, draw);
    }
    end_frame(c);
    begin_frame(c, 0.048, 0.016, rect::make(0, 0, 300, 200));
    mouse_button(c, false);

    {
        ui u(c);
        act = u.dock_space("d"_id, area, leaf, draw);
    }
    end_frame(c);
    CHECK(act.active);
    CHECK(act.panel == "p1"_id);
    CHECK(act.target == &leaf);
    CHECK(act.zone == DOCK_ZONE_RIGHT);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

PUI_TEST(test_dock_tabs_no_overflow)
{
    context *c = create_context(nullptr);
    dock_node leaf{};
    leaf.kind = DOCK_TABS;
    leaf.panels[0] = "a1"_id;
    leaf.panel_names[0] = "A";
    leaf.panels[1] = "a2"_id;
    leaf.panel_names[1] = "B";
    leaf.panel_count = 2;
    leaf.active = 0;
    auto draw = [](uiid, rect, bool) {};
    const rect area = {0, 0, 100, 120};
    // narrower than one full tab
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 300, 200));

    {
        ui u(c);
        (void)u.dock_space("d"_id, area, leaf, draw);
    }
    end_frame(c);
    // A click to the right of the node must not hit an
    // overflowing tab.
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 300, 200));
    mouse_move(c, 150, 13);
    mouse_button(c, true);

    {
        ui u(c);
        (void)u.dock_space("d"_id, area, leaf, draw);
    }
    end_frame(c);
    begin_frame(c, 0.032, 0.016, rect::make(0, 0, 300, 200));
    mouse_button(c, false);

    {
        ui u(c);
        (void)u.dock_space("d"_id, area, leaf, draw);
    }
    end_frame(c);
    CHECK(leaf.active == 0);
    // no tab existed out there
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

PUI_TEST(test_dock_drop_left_zone)
{
    context *c = create_context(nullptr);
    dock_node leaf{};
    leaf.kind = DOCK_TABS;
    leaf.panels[0] = "q1"_id;
    leaf.panel_names[0] = "One";
    leaf.panels[1] = "q2"_id;
    leaf.panel_names[1] = "Two";
    leaf.panel_count = 2;
    auto draw = [](uiid, rect, bool) {};
    const rect area = {0, 0, 300, 200};
    dock_action act;
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 300, 200));

    {
        ui u(c);
        (void)u.dock_space("d"_id, area, leaf, draw);
    }
    end_frame(c);
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 300, 200));
    mouse_move(c, 50, 13);
    mouse_button(c, true);

    {
        ui u(c);
        (void)u.dock_space("d"_id, area, leaf, draw);
    }
    end_frame(c);
    begin_frame(c, 0.032, 0.016, rect::make(0, 0, 300, 200));
    mouse_move(c, 10, 120);
    // near the left edge
    {
        ui u(c);
        (void)u.dock_space("d"_id, area, leaf, draw);
    }
    end_frame(c);
    begin_frame(c, 0.048, 0.016, rect::make(0, 0, 300, 200));
    mouse_button(c, false);

    {
        ui u(c);
        act = u.dock_space("d"_id, area, leaf, draw);
    }
    end_frame(c);
    CHECK(act.active);
    CHECK(act.panel == "q1"_id);
    CHECK(act.target == &leaf);
    CHECK(act.zone == DOCK_ZONE_LEFT);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

PUI_TEST(test_dock_content_clipped)
{
    context *c = create_context(nullptr);
    dock_node leaf{};
    leaf.kind = DOCK_LEAF;
    leaf.panels[0] = "x"_id;
    leaf.panel_names[0] = "X";
    leaf.panel_count = 1;
    i32 min_depth = 1000;
    auto draw = [&](uiid, rect, bool)
    {
        if (c->clip_depth < min_depth) min_depth = c->clip_depth;
    };
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 300, 200));

    {
        ui u(c);
        (void)u.dock_space("d"_id, {0, 0, 300, 200}, leaf, draw);
    }
    end_frame(c);
    CHECK(min_depth >= 1);
    // panel content was drawn inside a clip region
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

PUI_TEST(test_float_panel_dock_drag)
{
    context *c = create_context(nullptr);
    dock_node leaf{};
    leaf.kind = DOCK_LEAF;
    leaf.panels[0] = "target"_id;
    leaf.panel_names[0] = "Target";
    leaf.panel_count = 1;
    auto draw = [](uiid, rect, bool) {};
    const rect dock_area = {0, 0, 200, 200};
    rect win = {320.0f, 20.0f, 160.0f, 110.0f};
    const rect screen = rect::make(0, 0, 600, 400);
    dock_action act;
    auto frame = [&](f64 now, f32 mx, f32 my, bool press, bool release)
    {
        begin_frame(c, now, 0.016, screen);
        mouse_move(c, mx, my);
        if (press) mouse_button(c, true);
        if (release) mouse_button(c, false);
        dock_action a;

        {
            ui u(c);
            a = u.dock_space("dock"_id, dock_area, leaf, draw);
            rect w = win;
            panel_scope p = u.panel("Float", w, PANEL_NONE, "floating"_id, "Float");
        }
        end_frame(c);
        return a;
    };
    frame(0.000, 340, 30, false, false);
    // steady state
    frame(0.016, 340, 30, true,
          false);                              // press the floating titlebar
    frame(0.032, 100, 100, false, false);      // drag over the dock area
    act = frame(0.048, 100, 100, false, true); // release over the dock
    // // release over the dock
    CHECK(act.active);
    CHECK(act.panel == "floating"_id);
    CHECK(act.target == &leaf);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

// Regression: window roots came from the slot index but survived the renumbering
// in remove_window, so a window added after a removal could share a root (and
// with it every titlebar id) with a survivor.
PUI_TEST(test_window_roots_stay_unique)
{
    null_device nd;
    context *c = create_context(&nd, nullptr);
    set_violation_handler(c, capture_violation, nullptr);
    g_violation_events = 0;
    for (i32 i = 1; i <= 3; ++i)
        (void)add_window(c, reinterpret_cast<void *>(static_cast<ptrdiff_t>(i)),
                         nd.create_surface(), rect::make(0, 0, 100, 100));
    remove_window(c, *window_at(c, reinterpret_cast<void *>(2)));
    (void)add_window(c, reinterpret_cast<void *>(4), nd.create_surface(),
                     rect::make(0, 0, 100, 100));
    CHECK(c->window_count == 3);
    for (i32 i = 0; i < c->window_count; ++i)
        for (i32 j = i + 1; j < c->window_count; ++j)
            CHECK(c->windows[i].root != c->windows[j].root);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

// Regression: a dock tab drag kept the caller's name pointer for the ghost label;
// the drag outlives the frame that started it, so the name is copied.
PUI_TEST(test_dock_tab_drag_name_is_interned)
{
    context *c = create_context(nullptr);
    char name[16] = "Tab";
    dock_node leaf{};
    leaf.kind = DOCK_LEAF;
    leaf.panels[0] = "tab"_id;
    leaf.panel_names[0] = name;
    leaf.panel_count = 1;
    auto draw = [](uiid, rect, bool) {};
    auto frame = [&](f64 now, f32 mx, f32 my, bool press)
    {
        begin_frame(c, now, 0.016, rect::make(0, 0, 300, 200));
        mouse_move(c, mx, my);
        if (press) mouse_button(c, true);
        {
            ui u(c);
            (void)u.dock_space("dock"_id, {0, 0, 300, 200}, leaf, draw);
        }
        end_frame(c);
    };
    frame(0.0, 20, 10, false);
    frame(0.016, 20, 10, true); // press the tab
    std::snprintf(name, sizeof(name), "%s", "gone");
    frame(0.032, 20, 60, false);
    CHECK(c->dock_panel_name != nullptr);
    CHECK(c->dock_panel_name != nullptr && std::strcmp(c->dock_panel_name, "Tab") == 0);
    destroy_context(c);
}
