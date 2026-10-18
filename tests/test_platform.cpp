// Platform integration in the core (r100): pictures, OS drag-and-drop, file
// dialog results and saved window state. The SDL halves (decoding, routing,
// dialogs, applying a state) are covered in tests/test_pump.cpp.
#include "test_util.h"

PUI_TEST(test_fit_image)
{
    rect uv{};
    // contain: a 200x100 image in a 100x100 box is letterboxed, uv untouched
    rect r = fit_image(rect::make(0, 0, 100, 100), 200, 100, FIT_CONTAIN, uv);
    CHECK(r.x == 0.0f && r.w == 100.0f && r.h == 50.0f && r.y == 25.0f);
    CHECK(uv.x == 0.0f && uv.w == 1.0f && uv.h == 1.0f);
    // cover: the box stays; the middle half of the image shows
    r = fit_image(rect::make(10, 20, 100, 100), 200, 100, FIT_COVER, uv);
    CHECK(r.x == 10.0f && r.y == 20.0f && r.w == 100.0f && r.h == 100.0f);
    CHECK(uv.w == 0.5f && uv.x == 0.25f && uv.y == 0.0f && uv.h == 1.0f);
    // stretch: box and full uv
    r = fit_image(rect::make(0, 0, 30, 70), 200, 100, FIT_STRETCH, uv);
    CHECK(r.w == 30.0f && r.h == 70.0f && uv.w == 1.0f);
    // degenerate sizes return the area untouched
    r = fit_image(rect::make(0, 0, 30, 70), 0, 100, FIT_CONTAIN, uv);
    CHECK(r.w == 30.0f);
}

PUI_TEST(test_image_draw_and_lifetime)
{
    tf_env env;
    const u8 px[2 * 2 * 4] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
    const i32 before = env.nd.textures;
    image_handle img = create_image(env.c, 2, 2, px);
    CHECK(img.valid() && img.w == 2 && img.h == 2);
    CHECK(env.nd.textures == before + 1);
    CHECK(!create_image(env.c, 0, 2, px).valid());
    CHECK(!create_image(env.c, 2, 2, nullptr).valid());
    rect drawn{};
    env.frame(0.0, [&](ui &u) { drawn = u.image(rect::make(10, 10, 100, 50), img); });
    CHECK(drawn.w == 50.0f && drawn.h == 50.0f && drawn.x == 35.0f); // contain, centered
    CHECK(env.nd.last_texture == img.tex);
    env.frame(
        0.016, [&](ui &u)
        { (void)u.image(rect::make(10, 10, 100, 50), img, {.fit = FIT_COVER, .radius = 8}); });
    CHECK(env.nd.last_texture == img.tex); // rounded: the fan samples the picture too
    destroy_image(env.c, img);
    CHECK(!img.valid());
    env.frame(0.032, [&](ui &u) { (void)u.image(rect::make(0, 0, 10, 10), img); }); // no-op
}

PUI_TEST(test_drop_target)
{
    tf_env env;
    env.frame(0.0, [](ui &) {});
    window &w = *first_window(env.c);
    const rect left = rect::make(0, 0, 100, 100), right = rect::make(150, 0, 100, 100);

    // A drag enters and moves over the right target: it hovers, nothing drops.
    drop_event(w, drop_kind::BEGIN, 10, 10);
    drop_event(w, drop_kind::POSITION, 170, 40);
    drop_result l{}, r{};
    env.frame(0.016,
              [&](ui &u)
              {
                  l = u.drop_target(left, "left"_id);
                  r = u.drop_target(right, "right"_id);
              });
    CHECK(!l.hovering && r.hovering && !r.dropped);

    // Two files and a text are released there, then the drag completes.
    drop_event(w, drop_kind::FILE, 180, 50, "C:/pics/a.png");
    drop_event(w, drop_kind::FILE, 180, 50, "C:/pics/b.jpg");
    drop_event(w, drop_kind::TEXT, 180, 50, "hello");
    drop_event(w, drop_kind::COMPLETE, 0, 0);
    drop_result r2{};
    env.frame(0.032,
              [&](ui &u)
              {
                  l = u.drop_target(left, "left"_id);
                  r = u.drop_target(right, "right"_id);
                  r2 = u.drop_target(right, "right_again"_id); // first target took them
              });
    CHECK(!l.dropped && r.dropped && !r2.dropped);
    CHECK(!r.hovering); // the drag is over
    CHECK(r.count == 3);
    CHECK(std::strcmp(r.item(0), "C:/pics/a.png") == 0 && r.is_file(0));
    CHECK(std::strcmp(r.item(1), "C:/pics/b.jpg") == 0 && r.is_file(1));
    CHECK(std::strcmp(r.item(2), "hello") == 0 && !r.is_file(2));
    CHECK(std::strcmp(r.item(3), "") == 0 && !r.is_file(3)); // out of range

    // The items were an edge: the next frame sees nothing.
    env.frame(0.048, [&](ui &u) { r = u.drop_target(right, "right"_id); });
    CHECK(!r.dropped && !r.hovering && r.count == 0);

    // A drag that leaves without dropping clears the hover.
    drop_event(w, drop_kind::BEGIN, 170, 40);
    drop_event(w, drop_kind::COMPLETE, 0, 0);
    env.frame(0.064, [&](ui &u) { r = u.drop_target(right, "right"_id); });
    CHECK(!r.hovering && !r.dropped);
}

PUI_TEST(test_drop_overflow_is_reported)
{
    tf_env env;
    env.expect_clean = false;
    env.frame(0.0, [](ui &) {});
    window &w = *first_window(env.c);
    std::string big(3000, 'x');
    drop_event(w, drop_kind::FILE, 5, 5, big.c_str());
    g_last_violation = nullptr;
    drop_event(w, drop_kind::FILE, 5, 5, big.c_str()); // does not fit next to the first
    CHECK(violation_was("dropped items did not fit the per-frame drop buffer (item dropped)"));
    drop_result r{};
    env.frame(0.016, [&](ui &u) { r = u.drop_target(rect::make(0, 0, 50, 50), "t"_id); });
    CHECK(r.dropped && r.count == 1); // the one that fit
}

PUI_TEST(test_file_dialog_results)
{
    tf_env env;
    const char *picked[] = {"C:/a.txt", "D:/b c.txt"};
    CHECK(!take_file_dialog(env.c, "open"_id).done); // nothing yet
    deliver_file_dialog(env.c, "open"_id, picked, 2);
    deliver_file_dialog(env.c, "save"_id, nullptr, -1); // cancelled
    dialog_result r = take_file_dialog(env.c, "open"_id);
    CHECK(r.done && !r.cancelled && r.count == 2);
    CHECK(std::strcmp(r.path(0), "C:/a.txt") == 0 && std::strcmp(r.path(1), "D:/b c.txt") == 0);
    CHECK(std::strcmp(r.path(2), "") == 0);
    CHECK(!take_file_dialog(env.c, "open"_id).done); // reported once
    r = take_file_dialog(env.c, "save"_id);
    CHECK(r.done && r.cancelled && r.count == 0);
    // an empty choice is a cancel too
    deliver_file_dialog(env.c, "open"_id, picked, 0);
    CHECK(take_file_dialog(env.c, "open"_id).cancelled);
}

PUI_TEST(test_window_state_round_trip)
{
    const window_state s{.x = -1200, .y = 40, .w = 900, .h = 600, .maximized = true};
    const std::string text = save_window_state(s);
    window_state back{};
    CHECK(load_window_state(text, back));
    CHECK(back.x == -1200 && back.y == 40 && back.w == 900 && back.h == 600 && back.maximized);
    window_state bad{};
    CHECK(!load_window_state("", bad));
    CHECK(!load_window_state("1 2 3", bad));
    CHECK(!load_window_state("1 2 0 4 0", bad));   // empty size
    CHECK(!load_window_state("1 2 3 4 2", bad));   // maximized is 0/1
    CHECK(!load_window_state("1 2 3 4 0 9", bad)); // trailing junk
    CHECK(!load_window_state("a 2 3 4 0", bad));
    CHECK(load_window_state("10 20 30 40 0\n", bad) && bad.w == 30 && !bad.maximized);

    null_device nd;
    context *c = create_context(&nd, nullptr);
    window *w = add_window(c, reinterpret_cast<void *>(1), nullptr, rect::make(50, 60, 700, 500));
    w->maximized = true;
    const window_state cur = get_window_state(*w);
    CHECK(cur.x == 50 && cur.y == 60 && cur.w == 700 && cur.h == 500 && cur.maximized);
    destroy_context(c);
}

PUI_TEST(test_clamp_to_displays)
{
    const rect displays[] = {rect::make(0, 0, 1920, 1040), rect::make(1920, 0, 1280, 1000)};
    // on the second display: kept
    rect r = clamp_to_displays(rect::make(2000, 100, 800, 600), displays);
    CHECK(r.x == 2000.0f && r.y == 100.0f);
    // mostly off-screen but a grabbable part shows: kept
    r = clamp_to_displays(rect::make(-700, 100, 800, 600), displays);
    CHECK(r.x == -700.0f);
    // a monitor that is gone: centered on the first display
    r = clamp_to_displays(rect::make(-3000, 100, 800, 600), displays);
    CHECK(r.x == 560.0f && r.y == 220.0f && r.w == 800.0f);
    // larger than the display: shrunk to it
    r = clamp_to_displays(rect::make(9000, 9000, 4000, 3000), displays);
    CHECK(r.x == 0.0f && r.y == 0.0f && r.w == 1920.0f && r.h == 1040.0f);
    // no displays known: untouched
    r = clamp_to_displays(rect::make(-3000, 1, 2, 3), {});
    CHECK(r.x == -3000.0f);
}

// ---- r101: popups beyond the window -------------------------------------------
namespace
{
// Surfaces that tell the device which one is current, so a draw can be
// attributed to the window or to the overlay.
struct tagged_surface : null_surface
{
    i32 tag = 0;
    i32 *current = nullptr;
    i32 presents = 0;
    void make_current(render_device &) override
    {
        if (current) *current = tag;
    }
    void present() override { ++presents; }
};

struct surface_log_device : null_device
{
    i32 current = 0;
    struct draw_rec
    {
        i32 surface;
        f32 min_y, max_y;
    };
    std::vector<draw_rec> draws;
    struct clip_rec
    {
        i32 surface;
        bool on;
        rect r;
    };
    std::vector<clip_rec> clips;
    void set_clip(const rect *r) override
    {
        clips.push_back({current, r != nullptr, r ? *r : rect{}});
        null_device::set_clip(r);
    }
    void draw(texture_handle tex, const vertex *v, i32 vc, const i32 *idx, i32 ic) override
    {
        f32 lo = 1e9f, hi = -1e9f;
        for (i32 i = 0; i < vc; ++i)
        {
            lo = v[i].y < lo ? v[i].y : lo;
            hi = v[i].y > hi ? v[i].y : hi;
        }
        draws.push_back({current, lo, hi});
        null_device::draw(tex, v, vc, idx, ic);
    }
};

struct overlay_host : window_host
{
    rect bounds = rect::make(-500, -500, 2000, 2000);
    bool can_open = true;
    i32 opens = 0, moves = 0, closes = 0;
    rect last{};
    tagged_surface surf;
    void move_window(window &, f32, f32) override {}
    void resize_window(window &, f32, f32) override {}
    bool popup_bounds(window &, rect &local) override
    {
        local = bounds;
        return true;
    }
    bool open_overlay(window &, rect local, overlay_handle &out) override
    {
        if (!can_open) return false;
        ++opens;
        last = local;
        out.native = &surf;
        out.surface = &surf;
        return true;
    }
    void move_overlay(window &, rect local, overlay_handle &) override
    {
        ++moves;
        last = local;
    }
    void close_overlay(window &, overlay_handle &h) override
    {
        ++closes;
        h = overlay_handle{};
    }
};

struct overlay_env
{
    surface_log_device dev;
    tagged_surface main_surf;
    overlay_host host;
    context *c = nullptr;
    overlay_env()
    {
        main_surf.tag = 1;
        main_surf.current = &dev.current;
        host.surf.tag = 2;
        host.surf.current = &dev.current;
        c = create_context(&dev, &main_surf);
        set_window_host(c, &host);
    }
    ~overlay_env() { destroy_context(c); }
    // One frame of a window holding a combo near its bottom edge.
    bool frame(f64 t, i32 &sel, f32 mx, f32 my, i32 button)
    {
        static const char *items[3] = {"Alpha", "Beta", "Gamma"};
        bool changed = false;
        begin_frame(c, t, 0.016, rect::make(0, 0, 300, 200));
        mouse_move(c, mx, my);
        if (button >= 0) mouse_button(c, button == 1);
        {
            ui u(c);
            changed = u.combo(rect::make(20, 160, 160, 28), "", items, 3, sel, "cb"_id);
        }
        end_frame(c);
        return changed;
    }
};
} // namespace

PUI_TEST(test_popup_overlay_spills_out_of_the_window)
{
    overlay_env env;
    i32 sel = 0;
    env.frame(0.000, sel, 60, 174, 1); // press the header
    env.dev.draws.clear();
    env.frame(0.016, sel, 60, 174, 0); // release: the dropdown opens
    // It did not fit below a 200px-high window, so it went to an overlay below it
    // (the display has room; nothing flipped above the header).
    CHECK(env.host.opens == 1);
    CHECK(env.host.last.y >= 188.0f && env.host.last.bottom() > 200.0f);
    CHECK(env.host.surf.presents == 1);
    bool overlay_drawn = false, window_spill = false;
    for (const auto &d : env.dev.draws)
    {
        if (d.surface == 2) overlay_drawn = true;
        if (d.surface == 2) CHECK(d.min_y >= -2.0f && d.max_y <= env.host.last.h + 2.0f);
        if (d.surface == 1 && d.max_y > 202.0f) window_spill = true;
    }
    CHECK(overlay_drawn); // replayed onto the overlay, in its own coordinates
    CHECK(!window_spill); // nothing of the dropdown went to the window

    // Pointer events over the overlay arrive in the window's local space
    // (the SDL pump adds the overlay's offset): picking "Beta" works.
    const rect a = env.host.last;
    const f32 beta_y = a.y + 6.0f + 26.0f + 2.0f + 13.0f;
    env.frame(0.032, sel, a.x + 40.0f, beta_y, 1);
    const bool changed = env.frame(0.048, sel, a.x + 40.0f, beta_y, 0) ||
                         env.frame(0.064, sel, a.x + 40.0f, beta_y, -1);
    CHECK(changed && sel == 1);
    env.frame(0.080, sel, 5, 5, -1);
    CHECK(env.host.closes == 1); // no popup drew into it: closed
    CHECK(violation_count(env.c) == 0);
}

// r109: the recorder shifts draws and clips into the overlay's pixels as they
// arrive and replays them as recorded. Clips land inside the overlay, and an
// unchanged open popup replays the same geometry frame after frame.
PUI_TEST(test_popup_overlay_replays_in_its_own_pixels)
{
    overlay_env env;
    i32 sel = 0;
    env.frame(0.000, sel, 60, 174, 1);
    env.frame(0.016, sel, 60, 174, 0); // the dropdown opens in an overlay
    CHECK(env.host.opens == 1);
    const rect a = env.host.last;
    std::vector<surface_log_device::draw_rec> prev;
    for (i32 f = 0; f < 3; ++f)
    {
        env.dev.draws.clear();
        env.dev.clips.clear();
        env.frame(0.032 + 0.016 * f, sel, 5, 5, -1); // the pointer away: nothing changes
        std::vector<surface_log_device::draw_rec> now;
        for (const auto &d : env.dev.draws)
            if (d.surface == 2) now.push_back(d);
        CHECK(!now.empty());
        bool clips_inside = true;
        i32 overlay_clips = 0;
        for (const auto &cl : env.dev.clips)
            if (cl.surface == 2 && cl.on)
            {
                ++overlay_clips;
                clips_inside = clips_inside && cl.r.x >= -1.0f && cl.r.y >= -1.0f &&
                               cl.r.right() <= a.w + 1.0f && cl.r.bottom() <= a.h + 1.0f;
            }
        CHECK(overlay_clips > 0 && clips_inside);
        if (f > 0)
        {
            bool same = now.size() == prev.size();
            for (usize i = 0; same && i < now.size(); ++i)
                same = now[i].min_y == prev[i].min_y && now[i].max_y == prev[i].max_y;
            CHECK(same);
        }
        prev = now;
    }
}

PUI_TEST(test_popup_overlay_fallbacks)
{
    // The host cannot open one: the dropdown still opens, inside the window.
    {
        overlay_env env;
        env.host.can_open = false;
        i32 sel = 0;
        env.frame(0.000, sel, 60, 174, 1);
        env.frame(0.016, sel, 60, 174, 0);
        CHECK(env.host.opens == 0);
        CHECK(popup_present(env.c, id_child("cb"_id, "dropdown"_id), false) ||
              popup_present(env.c, id_child("cb"_id, "dropdown"_id), true));
        CHECK(violation_count(env.c) == 0);
    }
    // Overlays switched off: placement stays inside the window (flipped above).
    {
        overlay_env env;
        set_popup_overlays(env.c, false);
        i32 sel = 0;
        env.frame(0.000, sel, 60, 174, 1);
        env.dev.draws.clear();
        env.frame(0.016, sel, 60, 174, 0);
        CHECK(env.host.opens == 0);
        for (const auto &d : env.dev.draws) CHECK(d.max_y <= 202.0f);
    }
    // POPUP_IN_WINDOW keeps an app popup in the window even when it spills.
    {
        overlay_env env;
        begin_frame(env.c, 0.0, 0.016, rect::make(0, 0, 300, 200));
        {
            ui u(env.c);
            popup_scope p = u.popup("mine"_id, rect::make(250, 150, 120, 120), POPUP_IN_WINDOW);
            u.draw_rect(p.content(), color::white());
        }
        end_frame(env.c);
        CHECK(env.host.opens == 0);
        begin_frame(env.c, 0.016, 0.016, rect::make(0, 0, 300, 200));
        {
            ui u(env.c);
            popup_scope p = u.popup("theirs"_id, rect::make(250, 150, 120, 120));
            u.draw_rect(p.content(), color::white());
        }
        end_frame(env.c);
        CHECK(env.host.opens == 1);
    }
}

PUI_TEST(test_popup_overlay_dismiss_and_teardown)
{
    overlay_env env;
    i32 sel = 0;
    env.frame(0.000, sel, 60, 174, 1);
    env.frame(0.016, sel, 60, 174, 0);
    CHECK(env.host.opens == 1);
    // The window lost focus to another app: the dropdown closes like a menu.
    dismiss_popups(*first_window(env.c));
    env.frame(0.032, sel, 60, 120, -1);
    env.frame(0.048, sel, 60, 120, -1);
    CHECK(env.host.closes == 1);
    CHECK(!popup_present(env.c, id_child("cb"_id, "dropdown"_id), true));
    // Reopen; destroying the context closes the overlay it still holds.
    env.frame(0.064, sel, 60, 174, 1);
    env.frame(0.080, sel, 60, 174, 0);
    CHECK(env.host.opens == 2);
    destroy_context(env.c);
    env.c = create_context(&env.dev, &env.main_surf); // the env's destructor frees this one
    CHECK(env.host.closes == 2);
}

// ---- r106: accessibility -----------------------------------------------------
PUI_TEST(test_a11y_records_widgets)
{
    tf_env env;
    window *w = nullptr;
    bool check = true;
    f32 level = 0.5f;
    std::string name = "Ada";
    i32 calls = 0;
    const auto draw = [&](ui &u)
    {
        (void)u.button(rect::make(0, 0, 80, 24), "Save", "save"_id);
        (void)u.checkbox(rect::make(0, 30, 120, 24), "Wrap", check, "wrap"_id);
        (void)u.slider_float(rect::make(0, 60, 120, 24), "Level", level, 0.0f, 1.0f, "lvl"_id);
        (void)u.text_field(rect::make(0, 90, 120, 24), name, "name"_id);
        auto off = u.disabled(true);
        (void)u.button(rect::make(0, 120, 80, 24), "Gone", "gone"_id);
    };
    env.frame(0.0, draw);
    w = first_window(env.c);
    CHECK(a11y_count(*w) == 0); // off by default: nothing recorded
    set_accessibility(env.c, true);
    w->on_a11y = [](window &win) { ++*static_cast<i32 *>(win.a11y_user); };
    w->a11y_user = &calls;
    env.frame(0.016, draw);
    CHECK(calls == 1);
    CHECK(a11y_count(*w) == 5);
    const a11y_node_view save = a11y_node(*w, 0);
    CHECK(save.role == a11y_role::BUTTON && save.name == "Save" && save.id == "save"_id);
    CHECK(save.bounds.w == 80.0f);
    const a11y_node_view wrap = a11y_node(*w, 1);
    CHECK(wrap.role == a11y_role::CHECKBOX && (wrap.state & A11Y_CHECKED));
    const a11y_node_view lvl = a11y_node(*w, 2);
    CHECK(lvl.role == a11y_role::SLIDER && lvl.name == "Level" && lvl.value == "0.5");
    CHECK(a11y_node(*w, 3).role == a11y_role::TEXT_FIELD && a11y_node(*w, 3).value == "Ada");
    CHECK(a11y_node(*w, 4).state & A11Y_DISABLED);
    CHECK(a11y_node(*w, 9).id == 0); // out of range: empty

    // Assistive technology acts: invoke = a click, focus = keyboard focus.
    bool saved = false;
    a11y_invoke(*w, "save"_id);
    a11y_invoke(*w, "wrap"_id); // one invoke per frame: the last request wins
    env.frame(0.032,
              [&](ui &u)
              {
                  saved = u.button(rect::make(0, 0, 80, 24), "Save", "save"_id);
                  (void)u.checkbox(rect::make(0, 30, 120, 24), "Wrap", check, "wrap"_id);
              });
    CHECK(!saved && !check); // the checkbox toggled
    a11y_invoke(*w, "save"_id);
    env.frame(0.048, [&](ui &u) { saved = u.button(rect::make(0, 0, 80, 24), "Save", "save"_id); });
    CHECK(saved);
    a11y_focus(*w, "name"_id);
    env.frame(0.064, draw);
    env.frame(0.080, draw);
    CHECK(a11y_node(*w, 3).state & A11Y_FOCUSED);
    w->on_a11y = nullptr;
}

// r108: node slots (and their strings) are reused from frame to frame instead
// of rebuilt; the published tree must still be exactly this frame's nodes as
// the count and the names grow and shrink.
PUI_TEST(test_a11y_reused_slots_publish_the_current_frame)
{
    tf_env env;
    set_accessibility(env.c, true);
    const auto frame = [&](f64 t, std::initializer_list<const char *> names)
    {
        env.frame(t,
                  [&](ui &u)
                  {
                      i32 i = 0;
                      for (const char *n : names)
                      {
                          (void)u.button(rect::make(0, 30.0f * i, 200, 24), n,
                                         id_child("b"_id, static_cast<uiid>(i)));
                          ++i;
                      }
                  });
    };
    frame(0.0, {"a rather long label that leaves the small-string buffer", "two", "three"});
    window *w = first_window(env.c); // created by the first frame
    CHECK(a11y_count(*w) == 3);
    frame(0.016, {"x", "y"});
    CHECK(a11y_count(*w) == 2);
    CHECK(a11y_node(*w, 0).name == "x" && a11y_node(*w, 1).name == "y");
    frame(0.032, {"p", "q", "r", "s"});
    CHECK(a11y_count(*w) == 4);
    CHECK(a11y_node(*w, 0).name == "p" && a11y_node(*w, 3).name == "s");
    CHECK(a11y_node(*w, 3).id == id_child("b"_id, 3));
    frame(0.048, {});
    CHECK(a11y_count(*w) == 0);
    frame(0.064, {"back"});
    CHECK(a11y_count(*w) == 1 && a11y_node(*w, 0).name == "back");
}
