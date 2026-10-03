// input: clicks, keys, interaction edges, keyboard navigation (r88 split).
#include "test_util.h"

PUI_TEST(test_click)
{
    context *c = create_context(nullptr);
    null_device nd;
    set_device(c, &nd, nd.create_surface());
    const rect btn = {10, 10, 80, 30};
    const uiid id = "btn"_id;
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 100, 100));
    ui u(c);
    mouse_move(c, 50, 25);
    mouse_button(c, true);
    interaction in1 = u.interact(id, btn);
    CHECK(in1.hovered);
    CHECK(in1.pressed);
    CHECK(c->active == id);
    end_frame(c);
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 100, 100));
    mouse_button(c, false);
    interaction in2 = u.interact(id, btn);
    CHECK(in2.clicked);
    CHECK(c->active == 0);
    end_frame(c);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

PUI_TEST(test_release_outside_cancels)
{
    context *c = create_context(nullptr);
    const rect btn = {10, 10, 80, 30};
    const uiid id = "btn"_id;
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 100, 100));
    ui u(c);
    mouse_move(c, 50, 25);
    mouse_button(c, true);
    (void)u.interact(id, btn);
    end_frame(c);
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 100, 100));
    mouse_move(c, 5, 5);
    mouse_button(c, false);
    interaction in = u.interact(id, btn);
    CHECK(!in.clicked);
    end_frame(c);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

PUI_TEST(test_keys_and_escape)
{
    context *c = create_context(nullptr);
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 200, 200));
    key_event(c, key::ESCAPE, true);

    {
        ui u(c);
        CHECK(u.key_pressed(key::ESCAPE));
        popup_scope p = u.popup("m"_id, rect{0, 0, 50, 50}, POPUP_CLOSE_ON_ESCAPE);
        CHECK(p.close_requested);
    }
    end_frame(c);
    CHECK(!c->key_pressed[static_cast<i32>(key::ESCAPE)]);
    // edges cleared
    CHECK(c->key_down[static_cast<i32>(key::ESCAPE)]);
    // still held
    key_event(c, key::ESCAPE, false);
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 200, 200));

    {
        ui u(c);
        CHECK(!u.key_down(key::ESCAPE));
    }
    end_frame(c);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

PUI_TEST(test_interaction_edges)
{
    context *c = create_context(nullptr);
    const rect r = {0, 0, 100, 100};
    // disabled interaction never hovers or captures
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 200, 200));
    mouse_move(c, 50, 50);
    mouse_button(c, true);

    {
        ui u(c);
        interaction dis = u.interact("dis"_id, r, false);
        CHECK(dis.disabled);
        CHECK(!dis.hovered && !dis.pressed);
        CHECK(c->active == 0);
    }
    end_frame(c);
    mouse_button(c, false);
    // hot_id names the hovered widget (tooltip hook)
    begin_frame(c, 0.5, 0.016, rect::make(0, 0, 200, 200));
    mouse_move(c, 50, 50);

    {
        ui u(c);
        interaction in = u.interact("hot"_id, r);
        CHECK(in.hovered);
        CHECK(u.hot_id() == "hot"_id);
    }
    end_frame(c);
    // double click within the threshold
    auto click = [&](f64 t)
    {
        begin_frame(c, t, 0.016, rect::make(0, 0, 200, 200));
        mouse_move(c, 50, 50);
        mouse_button(c, true);

        {
            ui u(c);
            (void)u.interact("dbl"_id, r);
        }
        end_frame(c);
        begin_frame(c, t + 0.016, 0.016, rect::make(0, 0, 200, 200));
        mouse_button(c, false);
        interaction out{};

        {
            ui u(c);
            out = u.interact("dbl"_id, r);
        }
        end_frame(c);
        return out;
    };
    CHECK(!click(1.0).double_clicked);
    CHECK(click(1.05).double_clicked);
    // right-press over the widget is a right click; outside is not
    begin_frame(c, 2.0, 0.016, rect::make(0, 0, 200, 200));
    mouse_move(c, 50, 50);
    mouse_button(c, pointer_button::RIGHT, true);

    {
        ui u(c);
        interaction in = u.interact("rc"_id, r);
        CHECK(in.right_clicked);
        CHECK(!in.clicked);
    }
    end_frame(c);
    mouse_button(c, pointer_button::RIGHT, false);
    begin_frame(c, 2.1, 0.016, rect::make(0, 0, 200, 200));
    mouse_move(c, 150, 150);
    mouse_button(c, pointer_button::RIGHT, true);

    {
        ui u(c);
        interaction in = u.interact("rc2"_id, r);
        CHECK(!in.right_clicked);
    }
    end_frame(c);
    mouse_button(c, pointer_button::RIGHT, false);
    // a text field owns focus; interact() reports it, Escape releases
    // it
    std::string value = "x";
    const rect field = {0, 0, 100, 24};
    begin_frame(c, 3.0, 0.016, rect::make(0, 0, 200, 200));
    mouse_move(c, 10, 10);
    mouse_button(c, true);

    {
        ui u(c);
        (void)u.text_field(field, value, "fld"_id);
    }
    end_frame(c);
    begin_frame(c, 3.016, 0.016, rect::make(0, 0, 200, 200));
    mouse_button(c, false);

    {
        ui u(c);
        (void)u.text_field(field, value, "fld"_id);
    }
    end_frame(c);
    CHECK(c->focus == "fld"_id);
    begin_frame(c, 3.032, 0.016, rect::make(0, 0, 200, 200));

    {
        ui u(c);
        interaction in = u.interact("fld"_id, field);
        CHECK(in.focused);
    }
    end_frame(c);
    key_event(c, key::ESCAPE, true);
    begin_frame(c, 3.048, 0.016, rect::make(0, 0, 200, 200));

    {
        ui u(c);
        (void)u.text_field(field, value, "fld"_id);
    }
    end_frame(c);
    key_event(c, key::ESCAPE, false);
    CHECK(c->focus == 0);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

PUI_TEST(test_interact_topmost_wins)
{
    // Overlapping hit rects are a feature with defined semantics: the
    // TOPMOST (last submitted) enabled rect containing the press position
    // owns it. The lower rect's `activated` report is superseded, and its
    // release lands no click. Disjoint siblings and hover are unchanged.
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    set_violation_handler(c, capture_violation, nullptr);
    g_violation_events = 0;

    // hover: the topmost rect under the pointer owns `hot`
    mouse_move(c, 30, 30); // inside both rects
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 200, 200));
    {
        ui u(c);
        (void)u.interact("a"_id, rect::make(0, 0, 60, 60));
        (void)u.interact("b"_id, rect::make(20, 20, 60, 60)); // overlaps a
    }
    end_frame(c);
    CHECK(c->hot == "b"_id); // the topmost owns the position

    // press: the topmost captures; the lower never acts
    mouse_move(c, 30, 30);
    mouse_button(c, true);
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 200, 200));
    {
        ui u(c);
        interaction lower = u.interact("a"_id, rect::make(0, 0, 60, 60));
        CHECK(lower.activated); // the lower call reported it first
        interaction top = u.interact("b"_id, rect::make(20, 20, 60, 60));
        CHECK(top.activated);       // the topmost stole the press
        CHECK(c->active == "b"_id); // the capture belongs to the topmost
    }
    end_frame(c);

    // release: only the topmost gets the click
    mouse_move(c, 30, 30);
    mouse_button(c, false);
    begin_frame(c, 0.032, 0.016, rect::make(0, 0, 200, 200));
    {
        ui u(c);
        interaction lower = u.interact("a"_id, rect::make(0, 0, 60, 60));
        CHECK(!lower.clicked); // the release was not for the lower rect
        interaction top = u.interact("b"_id, rect::make(20, 20, 60, 60));
        CHECK(top.clicked); // the topmost gets the click
    }
    end_frame(c);

    // no violations: overlapping rects are a feature, not a mistake
    CHECK(violation_count(c) == 0);
    CHECK(g_violation_events == 0);
    destroy_context(c);
}

PUI_TEST(test_keyboard_navigation)
{
    // Tab walks the submission-order ring of every focusable widget — text
    // fields and the point-and-click widgets alike. Nothing focused: Tab
    // takes the first ring entry. Enter/Space activate a focused button or
    // checkbox; Left/Right adjust a focused slider; Shift+Tab goes back. The
    // focused widget draws the theme's focus outline.
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    set_violation_handler(c, capture_violation, nullptr);
    g_violation_events = 0;

    const rect btn_r = rect::make(0, 0, 80, 24);
    const rect field_r = rect::make(0, 30, 80, 24);
    const rect box_r = rect::make(0, 60, 120, 24);
    const rect sld_r = rect::make(0, 90, 80, 24);
    std::string text = "hi";
    bool checked = false;
    f32 slider = 0.5f;
    i32 clicks = 0;
    mouse_move(c, -50.0f, -50.0f); // pointer far away: hover never interferes

    auto frame = [&]()
    {
        ui u(c);
        if (u.button(btn_r, "go", "btn"_id)) ++clicks;
        (void)u.text_field(field_r, text, "field"_id);
        (void)u.checkbox(box_r, "on", checked, "box"_id);
        (void)u.slider_float(sld_r, "v", slider, 0.0f, 1.0f, "sld"_id);
    };

    // Frame 1 submits the ring; nothing is focused until Tab.
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 300, 200));
    frame();
    end_frame(c);
    CHECK(c->focus == 0);

    // Tab with no focus: the first ring entry (the button).
    key_event(c, key::TAB, true);
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 300, 200));
    frame();
    end_frame(c);
    key_event(c, key::TAB, false);
    CHECK(c->focus == "btn"_id);

    // Enter activates the focused button (Space would too); focus stays.
    key_event(c, key::ENTER, true);
    begin_frame(c, 0.032, 0.016, rect::make(0, 0, 300, 200));
    frame();
    end_frame(c);
    key_event(c, key::ENTER, false);
    CHECK(clicks == 1);
    CHECK(c->focus == "btn"_id);

    // Tab moves to the text field. Tab focus selects the value, so the next
    // typed characters replace it.
    key_event(c, key::TAB, true);
    begin_frame(c, 0.048, 0.016, rect::make(0, 0, 300, 200));
    frame();
    end_frame(c);
    key_event(c, key::TAB, false);
    CHECK(c->focus == "field"_id);
    text_input_event(c, "X");
    begin_frame(c, 0.064, 0.016, rect::make(0, 0, 300, 200));
    frame();
    end_frame(c);
    CHECK(text == "X");

    // Two more Tabs: checkbox, then slider.
    key_event(c, key::TAB, true);
    begin_frame(c, 0.080, 0.016, rect::make(0, 0, 300, 200));
    frame();
    end_frame(c);
    key_event(c, key::TAB, false);
    CHECK(c->focus == "box"_id);
    key_event(c, key::TAB, true);
    begin_frame(c, 0.096, 0.016, rect::make(0, 0, 300, 200));
    frame();
    end_frame(c);
    key_event(c, key::TAB, false);
    CHECK(c->focus == "sld"_id);

    // Right adjusts the focused slider by 5% of the range.
    key_event(c, key::RIGHT, true);
    begin_frame(c, 0.112, 0.016, rect::make(0, 0, 300, 200));
    frame();
    end_frame(c);
    key_event(c, key::RIGHT, false);
    CHECK(slider > 0.54f && slider < 0.56f);

    // Shift+Tab goes back (slider -> checkbox).
    mods_event(c, true, false);
    key_event(c, key::TAB, true);
    begin_frame(c, 0.128, 0.016, rect::make(0, 0, 300, 200));
    frame();
    end_frame(c);
    key_event(c, key::TAB, false);
    mods_event(c, false, false);
    CHECK(c->focus == "box"_id);

    // Space toggles the focused checkbox and keeps the focus.
    key_event(c, key::SPACE, true);
    begin_frame(c, 0.144, 0.016, rect::make(0, 0, 300, 200));
    frame();
    end_frame(c);
    key_event(c, key::SPACE, false);
    CHECK(checked);
    CHECK(c->focus == "box"_id);

    // The focused widget draws the theme's focus outline: two identical
    // button frames (pointer away, no hover) whose only difference is focus.
    begin_frame(c, 0.160, 0.016, rect::make(0, 0, 300, 200));
    {
        ui u(c);
        (void)u.button(rect::make(0, 0, 60, 24), "solo", "solo"_id);
    }
    end_frame(c);
    const i32 plain_verts = nd.vertices;

    c->windows[0].focus = "solo"_id; // the same button now owns keyboard focus
    begin_frame(c, 0.176, 0.016, rect::make(0, 0, 300, 200));
    {
        ui u(c);
        (void)u.button(rect::make(0, 0, 60, 24), "solo", "solo"_id);
    }
    end_frame(c);
    CHECK(nd.vertices > plain_verts); // the focus ring added geometry

    CHECK(violation_count(c) == 0);
    CHECK(g_violation_events == 0);
    destroy_context(c);
}

PUI_TEST(test_thread_isolation)
{
    // The current-context slot is genuinely thread-local: two threads, two
    // contexts, two violation sinks. A violation reported on one thread must
    // never appear on the other (the old global slot crossed them).
    static std::atomic<i32> t1_events{0}, t2_events{0};
    static std::atomic<bool> go{false};

    auto thread_body = [&](context *c, std::atomic<i32> *mine, std::atomic<i32> *theirs)
    {
        set_current_context(c); // this thread owns this context
        set_violation_handler(
            c, [](void *user, const char *, const char *, const char *, i32)
            { (*static_cast<std::atomic<i32> *>(user))++; }, mine);
        // Wait until both threads exist, then report from this thread only.
        while (!go.load())
        {
        }
        for (i32 i = 0; i < 8; ++i)
            report_violation(VIOL_LAYOUT_CLAMPED, "clamped", "thread isolation", "", 0, false);
        set_current_context(nullptr);
    };

    context *c1 = create_context(nullptr);
    context *c2 = create_context(nullptr);
    std::thread a(thread_body, c1, &t1_events, &t2_events);
    std::thread b(thread_body, c2, &t2_events, &t1_events);
    go.store(true);
    a.join();
    b.join();
    CHECK(t1_events.load() == 8);
    CHECK(t2_events.load() == 8);

    destroy_context(c1);
    destroy_context(c2);
    // (current_context was never set on the main thread by the workers.)
    CHECK(current_context() == nullptr);
}
