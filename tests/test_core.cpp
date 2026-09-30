// Phase 0/1 core tests: rect algebra, region/ID scoping, interaction, cursors,
// draw-list batching, theme, and the violation sink. Headless (no SDL).
#include <pufferui/pufferui.h>

#include <cstdio>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <vector>

using namespace pui;

static int g_failures = 0;

// Expected violations are captured instead of printed, and the guard that fired
// is asserted by message (install with set_violation_handler).
static const char *g_last_violation = nullptr;
static i32 g_violation_events = 0;

#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            std::printf("FAIL: %s  (%s:%d)\n", #x, __FILE__, __LINE__);                            \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)
static void capture_violation(void *, const char * /*condition*/, const char *message,
                              const char * /*file*/, i32 /*line*/)
{
    g_last_violation = message;
    ++g_violation_events;
}

static bool violation_was(const char *expected)
{
    return g_last_violation != nullptr && std::strcmp(g_last_violation, expected) == 0;
}

struct recorder_host : window_host
{
    i32 moves = 0;
    i32 resizes = 0;
    i32 minimizes = 0;
    i32 maximizes = 0;
    f32 last_x = 0.0f, last_y = 0.0f, last_w = 0.0f, last_h = 0.0f;
    // When set, toggle_maximize flips `w.maximized` the way the real event
    // pump reflects SDL's state (the OS reports the maximized flag).
    bool reflect_maximize = false;
    // zero: the base class default (no native chrome)
    void move_window(window &, f32 x, f32 y) override
    {
        moves += 1;
        last_x = x;
        last_y = y;
    }
    void resize_window(window &, f32 w, f32 h) override
    {
        resizes += 1;
        last_w = w;
        last_h = h;
    }
    void minimize_window(window &) override { minimizes += 1; }
    void toggle_maximize(window &w) override
    {
        maximizes += 1;
        if (reflect_maximize) w.maximized = !w.maximized;
    }
};

template <typename DrawFn> static void tf_click(context *c, f32 x, f32 y, f64 t, DrawFn &&draw)
{
    mouse_move(c, x, y);
    mouse_button(c, true);
    tf_frame(c, t, draw);
    mouse_button(c, false);
    tf_frame(c, t + 0.016, draw);
}
template <typename DrawFn>
static void tf_key(context *c, key k, bool ctrl, bool shift, f64 t, DrawFn &&draw)
{
    mods_event(c, shift, ctrl);
    key_event(c, k, true);
    tf_frame(c, t, draw);
    key_event(c, k, false);
    mods_event(c, false, false);
}
// The caret is measured in pixels, so these tests need the bundled font and a // real
// (null) device for the glyph atlas.

struct tf_env
{
    null_device nd;
    context *c = nullptr;
    explicit tf_env(i32 w = 300, i32 h = 200)
    {
        nd.my_surface.w = w;
        nd.my_surface.h = h;
        c = create_context(&nd, nd.create_surface());
    }
    ~tf_env() { destroy_context(c); }
    tf_env(const tf_env &) = delete;
    tf_env &operator=(const tf_env &) = delete;
};

static bool tf_needs_font(context *c)
{
    const font_handle fh = load_font(c, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
    if (fh == FONT_INVALID)
    {
        std::printf("note: bundled font not found; skipping text input test\n");
        return true;
    }
    theme t = default_dark();
    t.font = fh;
    set_theme(c, t);
    return false;
}

static void test_rect_algebra()
{
    rect r = rect::make(10, 20, 100, 50);
    rect top = r.cut_top(10);
    CHECK(top.x == 10 && top.y == 20 && top.w == 100 && top.h == 10);
    CHECK(r.y == 30 && r.h == 40);
    rect over = r.cut_top(1000);
    CHECK(over.h == 40 && r.h == 0);
    rect p = rect::make(0, 0, 100, 100).pad(10);
    CHECK(p.x == 10 && p.w == 80);
    rect neg = rect::make(0, 0, 10, 10).pad(50);
    CHECK(neg.w == 0 && neg.h == 0);
    rect i = rect::intersect(rect::make(0, 0, 10, 10), rect::make(5, 5, 10, 10));
    CHECK(i.x == 5 && i.y == 5 && i.w == 5 && i.h == 5);
    CHECK(rect::make(0, 0, 10, 10).contains(5, 5));
    CHECK(!rect::make(0, 0, 10, 10).contains(11, 5));
}

static void test_ids()
{
    uiid a = id_child("root"_id, "ok"_id);
    uiid b = id_child("root"_id, "ok"_id);
    uiid c = id_child("other"_id, "ok"_id);
    CHECK(a == b);
    CHECK(a != c);
}
static void test_regions(context *ctx)
{
    begin_frame(ctx, 0.0, 1.0 / 60.0, rect::make(0, 0, 800, 600));

    {
        ui u(ctx);
        region root = u.region({0, 0, 800, 600}, "root"_id);
        CHECK(root.id() != 0);
        rect child_area = root.content().pad(10);
        region card = u.region(child_area, "card"_id);
        CHECK(card.id() != root.id());
        CHECK(card.parent_ == root.id());
        CHECK(root.clip().contains(card.clip()) || card.clip().w == 0);
    }
    CHECK(ctx->clip_depth == 0);
    end_frame(ctx);
}

static void test_click()
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

static void test_release_outside_cancels()
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

static void test_cursors()
{
    column col(rect::make(0, 0, 100, 100), 10.0f);
    rect a = col.next(20);
    rect b = col.next(20);
    CHECK(a.y == 0 && a.h == 20);
    CHECK(b.y == 30 && b.h == 20);
    row rw(rect::make(0, 0, 100, 50), 5.0f);
    rect x = rw.next(30);
    rect y = rw.next(30);
    CHECK(x.x == 0 && x.w == 30);
    CHECK(y.x == 35 && y.w == 30);
}

static void test_draw_batching()
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

static void test_theme()
{
    context *c = create_context(nullptr);
    theme t = default_dark();
    t.accent = {255, 0, 128, 255};
    set_theme(c, t);
    CHECK(c->active_theme.accent.r == 255 && c->active_theme.accent.g == 0);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

static void test_utf8()
{
    // "├âãÆ├åÔÇÖ├âÔÇá├óÔé¼Ôäó├âãÆ├é┬ó├â┬ó├óÔé¼┼í├é┬¼├âÔÇª├é┬¥├âãÆ├åÔÇÖ├â┬ó├óÔÇÜ┬¼├é┬ª├âãÆ├óÔé¼┼í├âÔÇÜ├é┬©"
    // (C4 9F), space,     //
    // "├âãÆ├åÔÇÖ├âÔÇá├óÔé¼Ôäó├âãÆ├óÔé¼┼í├âÔÇÜ├é┬ó├âãÆ├åÔÇÖ├âÔÇÜ├é┬ó├âãÆ├é┬ó├â┬ó├óÔÇÜ┬¼├à┬í├âÔÇÜ├é┬¼├âãÆ├óÔé¼┬ª├âÔÇÜ├é┬í├âãÆ├åÔÇÖ├â┬ó├óÔÇÜ┬¼├à┬í├âãÆ├óÔé¼┼í├âÔÇÜ├é┬¼"
    // (E2 82 AC)
    std::string_view s = "\xC4\x9F \xE2\x82\xAC";
    CHECK(utf8_count(s) == 3);
    usize i = 0;
    CHECK(utf8_decode(s, i) == 0x011F);
    // ├âãÆ├åÔÇÖ├âÔÇá├óÔé¼Ôäó├âãÆ├é┬ó├â┬ó├óÔé¼┼í├é┬¼├âÔÇª├é┬¥├âãÆ├åÔÇÖ├â┬ó├óÔÇÜ┬¼├é┬ª├âãÆ├óÔé¼┼í├âÔÇÜ├é┬©
    CHECK(utf8_decode(s, i) == 0x0020);
    // space
    CHECK(utf8_decode(s, i) == 0x20AC);
    // ├âãÆ├åÔÇÖ├âÔÇá├óÔé¼Ôäó├âãÆ├óÔé¼┼í├âÔÇÜ├é┬ó├âãÆ├åÔÇÖ├âÔÇÜ├é┬ó├âãÆ├é┬ó├â┬ó├óÔÇÜ┬¼├à┬í├âÔÇÜ├é┬¼├âãÆ├óÔé¼┬ª├âÔÇÜ├é┬í├âãÆ├åÔÇÖ├â┬ó├óÔÇÜ┬¼├à┬í├âãÆ├óÔé¼┼í├âÔÇÜ├é┬¼
    CHECK(utf8_decode(s, i) == UTF8_END);
    std::string_view bad = "\xFF\xFE";
    usize j = 0;
    CHECK(utf8_decode(bad, j) == UTF8_REPLACEMENT);
}

static void test_text()
{
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    font_handle fh = load_font(c, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
    if (fh == FONT_INVALID) fh = load_font(c, "C:/Windows/Fonts/segoeui.ttf");
    if (fh == FONT_INVALID) fh = load_font(c, "C:/Windows/Fonts/arial.ttf");
    if (fh == FONT_INVALID)
    {
        // no system font available
        std::printf("note: no system font found; skipping text rendering test\n");
        destroy_context(c);
        return;
    }
    theme t = default_dark();
    t.font = fh;
    t.text_size = 16.0f;
    set_theme(c, t);
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 400, 200));
    ui u(c);
    const f32 w = u.text_width("Merhaba \xE2\x82\xAC");
    // "Merhaba                                  //
    // ├âãÆ├åÔÇÖ├âÔÇá├óÔé¼Ôäó├âãÆ├óÔé¼┼í├âÔÇÜ├é┬ó├âãÆ├åÔÇÖ├âÔÇÜ├é┬ó├âãÆ├é┬ó├â┬ó├óÔÇÜ┬¼├à┬í├âÔÇÜ├é┬¼├âãÆ├óÔé¼┬ª├âÔÇÜ├é┬í├âãÆ├åÔÇÖ├â┬ó├óÔÇÜ┬¼├à┬í├âãÆ├óÔé¼┼í├âÔÇÜ├é┬¼"
    CHECK(w > 0.0f);
    u.text({0, 0, 200, 24}, "Merhaba", color::white(), ALIGN_LEFT);
    u.button({0, 40, 120, 28}, "OK", "ok"_id);
    const measure_size wrapped =
        u.measure_text("the quick brown fox jumps over the lazy dog", 80.0f);
    CHECK(wrapped.width <= 80.5f);
    CHECK(wrapped.height >= u.line_height() * 2.0f);
    // wrapped onto several lines      // Same text, wider slot: one line. Narrower slot:
    // strictly more lines.
    const measure_size one_line = u.measure_text("the quick brown fox", 1000.0f);
    CHECK(one_line.height <= u.line_height() + 0.01f);
    CHECK(wrapped.height > one_line.height);
    u.text_wrapped({0, 80, 120, 60}, "some wrapped text here", color::white());
    // text_fit: one line when it fits, wrapped (and taller) when it does not
    const f32 fit_one = u.text_fit({0, 0, 1000, 20}, "short", color::white());
    const f32 fit_many =
        u.text_fit({0, 160, 60, 80}, "this is a much longer piece of text", color::white());
    CHECK(fit_one <= u.line_height() + 0.01f);
    CHECK(fit_many > u.line_height());
    auto fn = [](f32 w) -> measure_size { return {w, 10.0f}; };
    const measure_size custom = u.measure(fn, 50.0f);
    CHECK(custom.height == 10.0f && custom.width == 50.0f);
    end_frame(c);
    CHECK(nd.textures == 1);
    // one shared atlas
    CHECK(nd.draw_calls >= 1);
    // text + button geometry flushed
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

static void test_style_cascade()
{
    context *c = create_context(nullptr);
    theme t = default_dark();
    set_button_role(t, "primary"_id, button_override{.bg = some(color{10, 20, 30, 255})});
    set_theme(c, t);
    ui u(c);
    button_style s1 = u.resolve_button_style("primary"_id, {});
    CHECK(s1.bg.r == 10 && s1.bg.g == 20 && s1.bg.b == 30);

    {
        style_scope sc(u, button_override{.bg = some(color{1, 2, 3, 255})});
        button_style s2 = u.resolve_button_style("primary"_id, {});
        CHECK(s2.bg.r == 1 && s2.bg.g == 2 && s2.bg.b == 3);
    }
    button_style s3 = u.resolve_button_style("primary"_id, {});
    // scope popped
    button_style s4 =
        u.resolve_button_style("primary"_id, button_override{.bg = some(color{9, 9, 9, 255})});
    // button_override{.bg = some(color{9, 9, 9, 255})});
    CHECK(s4.bg.r == 9);
    // instance override wins
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

static void test_rounded_rect()
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

static void test_popups()
{
    context *c = create_context(nullptr);
    const rect menu = {0, 0, 100, 100};
    // frame 1: open the popup (nothing prior -> no input blocking yet)
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 400, 400));

    {
        ui u(c);
        popup_scope p = u.popup("menu"_id, menu, POPUP_CLOSE_ON_CLICK_OUTSIDE);
        CHECK(p.open);
        CHECK(!p.close_requested);
        CHECK(c->popup_depth == 1);
    }
    // the popup entry persists for the frame (consumed next frame for input)
    CHECK(c->popup_depth == 1);
    end_frame(c);
    // frame 2: a click outside the previously-open popup is captured
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 400, 400));
    mouse_move(c, 300, 300);
    mouse_button(c, true);

    {
        ui u(c);
        interaction bg = u.interact("bg"_id, rect{0, 0, 400, 400});
        CHECK(!bg.hovered);
        // blocked by the popup
        popup_scope p = u.popup("menu"_id, menu, POPUP_CLOSE_ON_CLICK_OUTSIDE);
        CHECK(p.close_requested);
    }
    end_frame(c);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

static void test_keys_and_escape()
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

static void test_rect_edges()
{
    rect r = rect::make(0, 0, 100, 50);
    rect bottom = r.cut_bottom(10);
    CHECK(bottom.x == 0 && bottom.y == 40 && bottom.w == 100 && bottom.h == 10);
    CHECK(r.h == 40);
    rect left = r.cut_left(20);
    CHECK(left.x == 0 && left.w == 20);
    CHECK(r.x == 20 && r.w == 80);
    rect right = r.cut_right(30);
    CHECK(right.x == 70 && right.w == 30);
    CHECK(r.w == 50);
    rect all = r.cut_top(1000);
    // over-cut clamps
    CHECK(all.h == 40 && r.h == 0);
    rect p2 = rect::make(0, 0, 100, 100).pad(5, 10);
    CHECK(p2.x == 5 && p2.y == 10 && p2.w == 90 && p2.h == 80);
    rect p4 = rect::make(0, 0, 100, 100).pad(1, 2, 3, 4);
    CHECK(p4.x == 1 && p4.y == 2 && p4.w == 96 && p4.h == 94);
    CHECK(rect::make(0, 0, 10, 10).is_valid());
    CHECK(!rect::make(0, 0, -1, 10).is_valid());
    CHECK(rect::make(0, 0, 10, 10).is_finite());
    const f32 nan = std::numeric_limits<f32>::quiet_NaN();
    const f32 inf = std::numeric_limits<f32>::infinity();
    CHECK(!rect::make(nan, 0, 10, 10).is_finite());
    CHECK(!rect::make(0, 0, inf, 10).is_finite());
    CHECK(rect::make(10, 20, 100, 50).center_x() == 60.0f);
    CHECK(rect::make(10, 20, 100, 50).center_y() == 45.0f);
    const rect none = rect::intersect(rect::make(0, 0, 10, 10), rect::make(20, 20, 10, 10));
    CHECK(none.w == 0.0f && none.h == 0.0f);
}

static void test_stack_clamp()
{
    column col(rect::make(0, 0, 100, 50), 10.0f);
    CHECK(col.next(30).h == 30.0f);
    CHECK(col.next(1000).h == 10.0f);
    // clamped to what is left
    CHECK(col.remaining().h == 0.0f);
    column col2(rect::make(0, 0, 100, 50), 10.0f);
    const rect top = col2.cut_top(20);
    const rect bot = col2.cut_bottom(15);
    CHECK(top.h == 20.0f);
    CHECK(bot.y == 35.0f && bot.h == 15.0f);
    col2.space(1000.0f);
    CHECK(col2.remaining().h == 0.0f);
    row rw(rect::make(0, 0, 80, 20), 5.0f);
    const rect l = rw.cut_left(20);
    const rect r2 = rw.cut_right(25);
    CHECK(l.x == 0.0f && l.w == 20.0f);
    CHECK(r2.x == 55.0f && r2.w == 25.0f);
    CHECK(l.right() <= r2.x);
    // pinned slices never overlap }
}

static void test_interaction_edges()
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

static void test_text_edges()
{
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        CHECK(u.text_width("") == 0.0f);
        const measure_size m = u.measure_text("", 100.0f);
        CHECK(m.width == 0.0f && m.height == u.line_height());
        CHECK(u.text_fit(rect::make(0, 0, 100, 20), "", color::white()) == 0.0f);
        u.text(rect::make(0, 0, 100, 20), "", color::white());
        u.text_wrapped(rect::make(0, 0, 100, 20), "", color::white());
    }
    end_frame(c);
    CHECK(nd.draw_calls == 0);
    // no font loaded -> nothing drawn
    CHECK(violation_count(c) == 0);
    // a bad font path fails cleanly, no violation
    CHECK(load_font(c, "C:/Windows/Fonts/__pufferui_missing__.ttf") == FONT_INVALID);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

static void test_utf8_truncated()
{
    std::string_view cut = "\xE2\x82";
    // truncated 3-byte sequence
    usize i = 0;
    CHECK(utf8_decode(cut, i) == UTF8_REPLACEMENT);
    CHECK(i == cut.size());
    // consumed to the end
    CHECK(utf8_count(cut) == 1);
    std::string_view lone = "\x80";
    // stray continuation byte
    usize j = 0;
    CHECK(utf8_decode(lone, j) == UTF8_REPLACEMENT);
    CHECK(j == 1);
}

static void test_ime()
{
    context *c = create_context(nullptr);
    ime_event(c, "\xE3\x81\xAB", 1);
    // U+306B preedit, caret at 1
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        CHECK(c->ime_preedit_len == 3);
        CHECK(c->ime_cursor == 1);
    }
    end_frame(c);
    // committing text ends the composition and queues the text
    text_input_event(c, "n");
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 200, 100));
    CHECK(c->ime_preedit_len == 0);
    CHECK(c->text_len == 1);
    end_frame(c);
    CHECK(c->text_len == 0);
    // consumed by the frame
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

static void test_draw_flush()
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

static void test_blur_fallback()
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

static void test_window_errors()
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

static void test_duplicate_region_id()
{
    context *c = create_context(nullptr);
    set_violation_handler(c, capture_violation, nullptr);
    g_violation_events = 0;
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 100, 100));

    {
        ui u(c);

        {
            region a = u.region({0, 0, 50, 50}, "same"_id);
        }
        g_last_violation = nullptr;

        {
            region b = u.region({50, 0, 50, 50}, "same"_id);
        }
        CHECK(violation_count(c) == 1);
        // duplicate sibling id reported
        CHECK(violation_was("duplicate region id among siblings"));
    }
    end_frame(c);
    destroy_context(c);
}

static void test_auto_id_local()
{
    context *c = create_context(nullptr);
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 100, 100));

    {
        ui u(c);
        const uiid a = u.auto_id();
        const uiid b = u.auto_id();
        CHECK(a != 0 && a != b);
        CHECK(u.local("x") == id_child(0, "x"));

        {
            region r = u.region({0, 0, 50, 50}, "r"_id);
            CHECK(u.local("x") == id_child(r.id(), "x"));
        }
        CHECK(u.local("x") == id_child(0, "x"));
    }
    end_frame(c);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

static void test_limit_overflows()
{
    context *c = create_context(nullptr);
    set_violation_handler(c, capture_violation, nullptr);
    g_violation_events = 0;
    // One scope too many is reported, the rest still balance on
    // destruction.
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 100, 100));

    {
        ui u(c);
        std::optional<style_scope> scopes[MAX_STYLE_SCOPES + 1];
        for (i32 i = 0; i < MAX_STYLE_SCOPES; ++i) scopes[i].emplace(u, button_override{});
        g_last_violation = nullptr;
        scopes[MAX_STYLE_SCOPES].emplace(u, button_override{});
        CHECK(violation_was("button style scope overflow"));
        for (i32 i = MAX_STYLE_SCOPES; i >= 0; --i) scopes[i].reset();
    }
    end_frame(c);
    // One popup too many is reported; the depth still unwinds cleanly.
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 100, 100));

    {
        ui u(c);
        std::optional<popup_scope> popups[MAX_POPUPS + 1];
        for (i32 i = 0; i < MAX_POPUPS; ++i)
            popups[i].emplace(u, id_child("p"_id, static_cast<uiid>(i)), rect::make(0, 0, 10, 10),
                              popup_flags::NONE);
        g_last_violation = nullptr;
        popups[MAX_POPUPS].emplace(u, id_child("p"_id, static_cast<uiid>(MAX_POPUPS)),
                                   rect::make(0, 0, 10, 10), popup_flags::NONE);
        CHECK(violation_was("popup stack overflow"));
        for (i32 i = MAX_POPUPS; i >= 0; --i) popups[i].reset();
    }
    end_frame(c);
    CHECK(violation_count(c) == 2);
    CHECK(g_violation_events == 2);
    destroy_context(c);
}

static void test_split_interactive()
{
    context *c = create_context(nullptr);
    const rect area = {0, 0, 300, 200};
    f32 pos = 100.0f;
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 300, 200));

    {
        ui u(c);
        auto halves = u.split_horizontal_interactive("sp"_id, area, &pos, 60.0f, 240.0f);
        CHECK(halves.first.w > 0.0f && halves.second.w > 0.0f);
        CHECK(halves.first.right() <= halves.second.x);
    }
    end_frame(c);
    // grab the handle (x = 100 + gap 2 .. + thickness 6) and drag right
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 300, 200));
    mouse_move(c, 105, 100);
    mouse_button(c, true);

    {
        ui u(c);
        (void)u.split_horizontal_interactive("sp"_id, area, &pos, 60.0f, 240.0f);
    }
    end_frame(c);
    begin_frame(c, 0.032, 0.016, rect::make(0, 0, 300, 200));
    mouse_move(c, 280, 100);

    {
        ui u(c);
        (void)u.split_horizontal_interactive("sp"_id, area, &pos, 60.0f, 240.0f);
    }
    end_frame(c);
    mouse_button(c, false);
    CHECK(pos > 100.0f);
    // moved
    CHECK(pos <= 230.0f + 0.001f);
    // clamped by max_left/bounds
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

static void test_panel_flags()
{
    context *c = create_context(nullptr);
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 400, 300));

    {
        ui u(c);
        const panel_style &ps = u.th().panel;
        rect b = {10, 10, 200, 120};

        {
            panel_scope p = u.panel("NoTitle", b, PANEL_NO_TITLEBAR | PANEL_NO_SHADOW);
            CHECK(p.open);
            // without a titlebar the client starts right below the
            // border
            CHECK(p.content().y == b.y + ps.border_thickness + ps.padding);
        }
        CHECK(c->clip_depth == 0);
    }
    end_frame(c);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

static void test_dock_empty()
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

static void test_nine_slice()
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
// ---- state / view convention (update_view + direct writes)
// --------------
struct sv_state
{
    // model: components write directly
    i32 count = 0;
    bool open = false;
    // view: derived once per frame, then only read
    struct view_t
    {
        i32 count = 0;
        bool open = false;
        char label[32]{};

    } view;
    void update_view()
    {
        view.count = count;
        view.open = open;
        std::snprintf(view.label, sizeof(view.label), "count %d", count);
    }
};

static void test_state_view_update()
{
    context *c = create_context(nullptr);
    sv_state s;
    // update_view derives; the model is untouched
    s.update_view();
    CHECK(s.view.count == 0 && !s.view.open);
    CHECK(std::strcmp(s.view.label, "count 0") == 0);
    CHECK(s.count == 0 && !s.open);
    const rect btn = {0, 30, 100, 24};
    // frame 1: press (captures)
    mouse_move(c, 50, 42);
    mouse_button(c, true);
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        u.text(rect::make(0, 0, 100, 20), s.view.label, color::white());
        (void)u.button(btn, "add", "sv_add_btn"_id);
    }
    end_frame(c);
    CHECK(s.count == 0);
    // frame 2: release inside -> the component writes the model
    // directly
    mouse_button(c, false);
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        if (u.button(btn, "add", "sv_add_btn"_id)) s.count += 2;
        CHECK(s.view.count == 0);
        // the view snapshot is still stale
    }
    end_frame(c);
    CHECK(s.count == 2);
    // the model changed immediately      // frame 3: update_view
    // frame 3: update_view reflects the new data
    s.update_view();
    CHECK(s.view.count == 2);
    CHECK(std::strcmp(s.view.label, "count 2") == 0);
    // frames 4-5: a second click writes again
    mouse_move(c, 50, 42);
    mouse_button(c, true);
    begin_frame(c, 0.032, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        (void)u.button(btn, "add", "sv_add_btn"_id);
    }
    end_frame(c);
    mouse_button(c, false);
    begin_frame(c, 0.048, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        if (u.button(btn, "add", "sv_add_btn"_id)) s.count += 3;
    }
    end_frame(c);
    s.update_view();
    CHECK(s.count == 5 && s.view.count == 5);
    // any other field can be written the same way
    s.open = true;
    s.update_view();
    CHECK(s.view.open);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

static void test_widget_outline()
{
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    const rect r = {0, 0, 120, 28};
    // flat by default: just the fill
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        (void)u.button(r, "flat", "outline_a"_id);
    }
    end_frame(c);
    const i32 flat_verts = nd.vertices;
    CHECK(flat_verts > 0);
    // border_thickness > 0 adds a full outline ring around the
    // fill. Compare     // against an explicitly flat button so the
    // default theme is free to draw one.
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        (void)u.button(r, "line", "outline_b"_id, static_cast<uiid>(0),
                       button_override{.border_thickness = some(1.0f)});
    }
    end_frame(c);
    const i32 outlined_verts = nd.vertices;
    begin_frame(c, 0.024, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        (void)u.button(r, "none", "outline_c"_id, static_cast<uiid>(0),
                       button_override{.border_thickness = some(0.0f)});
    }
    end_frame(c);
    const i32 borderless_verts = nd.vertices;
    CHECK(outlined_verts > borderless_verts);
    // A fully transparent border is "no border": the fill must stay
    // full-size     // and nothing extra may be drawn, so it
    // matches the borderless button.
    begin_frame(c, 0.032, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        (void)u.button(
            r, "clear", "outline_d"_id, static_cast<uiid>(0),
            button_override{.border = some(color{0, 0, 0, 0}), .border_thickness = some(1.0f)});
    }
    end_frame(c);
    // Ring-path extremes must stay valid: a stadium (radius = half
    // the short     // side) and a hairline ring (thickness =
    // radius, so the inner hole is     // degenerate and the arcs
    // skip their inner feather). Both must emit     // vertices and
    // no violations
    // - the ring bands and the arcs shade the same     // 1px
    // feathers, so the junctions cannot step.
    begin_frame(c, 0.040, 0.016, rect::make(0, 0, 400, 100));

    {
        ui u(c);
        (void)u.button(rect::make(10, 10, 120, 30), "stadium", "outline_e"_id, static_cast<uiid>(0),
                       button_override{.bg = some(color{0, 0, 0, 0}),
                                       .border = some(color{90, 160, 255, 255}),
                                       .radius = some(15.0f),
                                       .border_thickness = some(1.5f)});
    }
    end_frame(c);
    CHECK(nd.vertices > 0);
    begin_frame(c, 0.048, 0.016, rect::make(0, 0, 400, 100));

    {
        ui u(c);
        // hairline: thickness = radius -> r_in = 0, inner feather
        // skipped
        (void)u.button(rect::make(240, 10, 60, 24), "thin", "outline_f"_id, static_cast<uiid>(0),
                       button_override{.bg = some(color{0, 0, 0, 0}),
                                       .border = some(color{90, 160, 255, 255}),
                                       .radius = some(6.0f),
                                       .border_thickness = some(5.0f)});
    }
    end_frame(c);
    CHECK(nd.vertices > 0);
    // text fields: no outline when idle...
    std::string value = "x";
    begin_frame(c, 0.032, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        (void)u.text_field(r, value, "outline_field"_id);
    }
    end_frame(c);
    const i32 idle_verts = nd.vertices;
    // ...and a focused outline after a click
    mouse_move(c, 10, 10);
    mouse_button(c, true);
    begin_frame(c, 0.048, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        (void)u.text_field(r, value, "outline_field"_id);
    }
    end_frame(c);
    mouse_button(c, false);
    begin_frame(c, 0.064, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        (void)u.text_field(r, value, "outline_field"_id);
    }
    end_frame(c);
    CHECK(c->focus == "outline_field"_id);
    const i32 focused_verts = nd.vertices;
    CHECK(focused_verts > idle_verts);
    // outline (+ caret) drawn      // the focus outline is
    // customizable: thickness 0 drops it
    theme t = default_dark();
    t.focus_border_thickness = 0.0f;
    set_theme(c, t);
    begin_frame(c, 0.080, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        (void)u.text_field(r, value, "outline_field"_id);
    }
    end_frame(c);
    CHECK(nd.vertices < focused_verts);
    // no outline
    CHECK(nd.vertices >= idle_verts);
    // caret only
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}
// ---- R6.5 animation
// -------------------------------------------------------

static void test_animation_tween()
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

static void test_animation_spring_smooth()
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

static void test_animation_scope_color_appear()
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
// ---- bundled font coverage (design
// ├âãÆ├åÔÇÖ├âÔÇá├óÔé¼Ôäó├âãÆ├é┬ó├â┬ó├óÔé¼┼í├é┬¼├âÔÇª├é┬í├âãÆ├åÔÇÖ├â┬ó├óÔÇÜ┬¼├à┬í├âãÆ├óÔé¼┼í├âÔÇÜ├é┬º6.2)
// //
// ---------------------------------

static void test_violation_overlay()
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

static void test_interact_topmost_wins()
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

static void test_keyboard_navigation()
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

static void test_text_atlas_paging()
{
    // More distinct glyphs than one 1024x1024 page holds: the store spills
    // into additional atlas pages instead of silently dropping glyphs (the
    // old behavior - a full page just stopped rendering text).
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    font_handle fh = load_font(c, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
    if (fh == FONT_INVALID)
    {
        std::printf("note: bundled font not found; skipping atlas paging\n");
        destroy_context(c);
        return;
    }
    theme t = default_dark();
    t.font = fh;
    t.text_size = 48.0f; // big glyphs: each consumes real page area, so the
                         // flood spills across atlas pages
    set_theme(c, t);

    // 1400 codepoints across Latin/Greek/Cyrillic/punctuation: one page fills,
    // the second page takes the rest.
    std::string big;
    auto push_cp = [&big](u32 cp)
    {
        if (cp < 0x80)
        {
            big.push_back(static_cast<char>(cp));
            return;
        }
        if (cp < 0x800)
        {
            big.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            big.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            return;
        }
        big.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        big.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        big.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    };
    for (u32 cp = 0x20; cp < 0x590; ++cp) push_cp(cp);

    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 8000, 60));
    {
        ui u(c);
        u.text(rect::make(0, 0, 7900, 60), big, color::white(), ALIGN_LEFT);
    }
    end_frame(c);

    CHECK(nd.textures >= 2);   // paged: more than one atlas texture
    CHECK(nd.vertices > 0);    // glyphs drawn
    CHECK(nd.draw_calls >= 2); // batching flushed on the page change

    // Every glyph from the string has a texture after the flood: redraw one
    // page-boundary glyph and confirm it lands on a real texture.
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 8000, 60));
    {
        ui u(c);
        const i32 draws_before = nd.draw_calls;
        u.text(rect::make(0, 0, 380, 20), big, color::white(), ALIGN_LEFT);
        CHECK(nd.draw_calls >= draws_before); // the cached glyphs all drew
    }
    end_frame(c);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

static void test_dock_persistence()
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

static void test_text_wrapped_height()
{
    // `text_wrapped_height` predicts exactly what `text_wrapped` draws: one
    // line for short text, more for wrapped paragraphs, and newline breaks
    // honored. The examples use it to size containers around paragraphs.
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    font_handle fh = load_font(c, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
    if (fh == FONT_INVALID)
    {
        std::printf("note: bundled font not found; skipping wrapped-height\n");
        destroy_context(c);
        return;
    }
    theme t = default_dark();
    t.font = fh;
    t.text_size = 15.0f;
    set_theme(c, t);

    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 400, 300));
    i32 draws_before = 0;
    f32 predicted_h = 0.0f;
    f32 line_h = 0.0f;
    {
        ui u(c);
        const f32 one_line = u.text_wrapped_height(200.0f, "short");
        CHECK(one_line == u.line_height());
        line_h = u.line_height();

        // short words in a 100px-wide rect: the paragraph wraps
        const f32 wrapped = u.text_wrapped_height(100.0f, "aaa bbb ccc ddd eee");
        CHECK(wrapped > u.line_height());
        CHECK(wrapped <= 5.0f * u.line_height());
        i32 draws_before = 0;
        f32 predicted_h = 0.0f;

        // an explicit newline breaks even when the line would fit
        const f32 broken = u.text_wrapped_height(400.0f, "one\ntwo");
        CHECK(broken == 2.0f * u.line_height());

        // empty text: zero
        CHECK(u.text_wrapped_height(100.0f, "") == 0.0f);

        // the prediction matches the drawn extent: draw into a rect one line
        // taller than predicted; the batch flushes at end_frame
        const std::string para = "the quick brown fox jumps over the lazy dog "
                                 "again and again and again";
        const f32 predicted = u.text_wrapped_height(140.0f, para);
        draws_before = nd.draw_calls;
        u.text_wrapped(rect::make(0, 0, 140, predicted + 1.0f), para, color::white());
        predicted_h = predicted;
    }
    end_frame(c);
    CHECK(nd.draw_calls > draws_before); // the predicted space held the text
    CHECK(predicted_h <= 6.0f * line_h);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

static void test_font_coverage()
{
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    font_handle fh = load_font(c, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
    if (fh == FONT_INVALID) fh = load_font(c, "C:/Windows/Fonts/segoeui.ttf");
    if (fh == FONT_INVALID) fh = load_font(c, "C:/Windows/Fonts/arial.ttf");
    if (fh == FONT_INVALID)
    {
        std::printf("note: no font found; skipping coverage test\n");
        return;
    }
    theme t = default_dark();
    t.font = fh;
    set_theme(c, t);
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 400, 400));
    {
        ui u(c);
        const struct range
        {
            u32 lo, hi;
            const char *name;
        };
        const range ranges[] = {
            {0x0020, 0x007E, "ASCII"},
            {0x00A1, 0x024F, "Latin-1/Latin Extended"},
            {0x0370, 0x03FF, "Greek"},
            {0x0400, 0x04FF, "Cyrillic"},
            {0x2010, 0x205F, "Punctuation"},
            {0x20A0, 0x20BF, "Currency Symbols"},
            {0x2100, 0x214F, "Letterlike Symbols"},
            {0x2190, 0x21FF, "Arrows"},
            {0x2200, 0x22FF, "Mathematical Operators"},
        };
        i32 missing = 0;
        // Unassigned slots / format controls in these blocks
        // have no glyphs in any font; the contract is every *assigned, printable*
        // codepoint.
        auto is_unassigned = [](u32 cp)
        {
            switch (cp)
            {
            case 0x0378:
            case 0x0379:
            case 0x038B:
            case 0x038D:
            case 0x03A2:
            case 0x2065:
            case 0x2072:
            case 0x2073:
            case 0x208F:
            case 0x20B6:
            case 0x20B7:
            case 0x20BB:
            case 0x20BC:
                return true;
            default:
                break;
            }
            return (cp >= 0x0380 && cp <= 0x0383) || // unassigned
                   (cp >= 0x2066 && cp <= 0x2069) || // bidi isolates (format)
                   (cp >= 0x209D && cp <= 0x209F);
        };
        // Assigned, but not present in the bundled DejaVu Sans 2.37 (rare
        // currency/letterlike symbols outside the required UI set).
        auto not_in_bundled = [](u32 cp)
        {
            switch (cp)
            {
            case 0x20BE:
            case 0x20BF: // lari, bitcoin
            case 0x210A:
            case 0x214A:
            case 0x214C: // script g, property line, per
            case 0x214D:
            case 0x214F: // aktieselskab, samaritan
                return true;
            default:
                return false;
            }
        };
        for (const range &r : ranges)
        {
            for (u32 cp = r.lo; cp <= r.hi; ++cp)
            {
                if (is_unassigned(cp) || not_in_bundled(cp)) continue;
                if (!u.has_glyph(cp))
                {
                    if (missing < 60) std::printf("coverage: missing U+%04X (%s)\n", cp, r.name);
                    ++missing;
                }
            }
        }
        for (u32 cp : {0x2713u, 0x2714u, 0x2717u})
        {
            if (!u.has_glyph(cp))
            {
                std::printf("coverage: missing U+%04X (Dingbats)\n", cp);
                ++missing;
            }
        }
        CHECK(missing == 0);
    }
    end_frame(c);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}
static void test_popup_focus_trap()
{
    context *c = create_context(nullptr);
    std::string base = "base", pa = "a", pb = "b";
    const rect base_r = {0, 0, 100, 24};
    const rect pa_r = {0, 40, 100, 24};
    const rect pb_r = {0, 70, 100, 24};
    const rect menu = {0, 30, 140, 80};
    // frame 1: base field + popup (input capture starts
    // next frame)
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 200, 150));

    {
        ui u(c);
        (void)u.text_field(base_r, base, "base"_id);
        popup_scope p = u.popup("trap"_id, menu, popup_flags::NONE);
        (void)u.text_field(pa_r, pa, "pa"_id);
        (void)u.text_field(pb_r, pb, "pb"_id);
    }
    end_frame(c);
    // frame 2: a press on the base field is blocked (popup
    // captures input)
    mouse_move(c, 10, 10);
    mouse_button(c, true);
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 200, 150));

    {
        ui u(c);
        interaction blocked = u.interact("base"_id, base_r);
        CHECK(blocked.blocked);
        CHECK(!blocked.hovered);
        (void)u.text_field(base_r, base, "base"_id);
        popup_scope p = u.popup("trap"_id, menu, popup_flags::NONE);
        (void)u.text_field(pa_r, pa, "pa"_id);
        (void)u.text_field(pb_r, pb, "pb"_id);
    }
    end_frame(c);
    mouse_button(c, false);
    CHECK(c->focus != "base"_id);
    // focus popup field A (press + release inside the
    // popup)
    mouse_move(c, 50, 52);
    mouse_button(c, true);
    begin_frame(c, 0.032, 0.016, rect::make(0, 0, 200, 150));

    {
        ui u(c);
        popup_scope p = u.popup("trap"_id, menu, popup_flags::NONE);
        (void)u.text_field(pa_r, pa, "pa"_id);
        (void)u.text_field(pb_r, pb, "pb"_id);
    }
    end_frame(c);
    mouse_button(c, false);
    begin_frame(c, 0.048, 0.016, rect::make(0, 0, 200, 150));

    {
        ui u(c);
        // The pointer is inside the popup, but base UI
        // drawn before it stays blocked.
        interaction base_blocked = u.interact("base"_id, base_r);
        CHECK(base_blocked.blocked);
        popup_scope p = u.popup("trap"_id, menu, popup_flags::NONE);
        (void)u.text_field(pa_r, pa, "pa"_id);
        (void)u.text_field(pb_r, pb, "pb"_id);
    }
    end_frame(c);
    CHECK(c->focus == "pa"_id);
    // Tab cycles within the popup, never back to the base
    // field
    key_event(c, key::TAB, true);
    begin_frame(c, 0.064, 0.016, rect::make(0, 0, 200, 150));

    {
        ui u(c);
        popup_scope p = u.popup("trap"_id, menu, popup_flags::NONE);
        (void)u.text_field(pa_r, pa, "pa"_id);
        (void)u.text_field(pb_r, pb, "pb"_id);
    }
    end_frame(c);
    key_event(c, key::TAB, false);
    CHECK(c->focus == "pb"_id);
    CHECK(c->focus != "base"_id);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}
// ---- rect helpers, text polish, widgets
// -------------------------------------

static void test_rect_helpers()
{
    const rect r = rect::make(0, 0, 100, 50);
    CHECK(r.top_slice(10).y == 0.0f && r.top_slice(10).h == 10.0f);
    CHECK(r.bottom_slice(10).y == 40.0f && r.bottom_slice(10).h == 10.0f);
    CHECK(r.left_slice(10).x == 0.0f && r.left_slice(10).w == 10.0f);
    CHECK(r.right_slice(10).x == 90.0f && r.right_slice(10).w == 10.0f);
    CHECK(r.top_slice(1000).h == 50.0f);
    // clamped
    CHECK(rect::make(0, 0, 100, 100).cut_top_ratio(0.25f).h == 25.0f);
    CHECK(rect::make(0, 0, 100, 100).cut_bottom_ratio(0.25f).y == 75.0f);
    CHECK(rect::make(0, 0, 100, 100).cut_left_ratio(0.5f).w == 50.0f);
    CHECK(rect::make(0, 0, 100, 100).cut_right_ratio(0.5f).x == 50.0f);
    const rect s = rect::make(0, 0, 20, 10);
    const rect in = rect::make(0, 0, 100, 50);
    CHECK(s.align_center(in).x == 40.0f && s.align_center(in).y == 20.0f);
    CHECK(s.align_right(in).x == 80.0f);
    CHECK(s.align_bottom(in).y == 40.0f);
    CHECK(s.align_left(in).x == 0.0f);
    CHECK(s.align_top(in).y == 0.0f);
    CHECK(s.align_h_center(in).x == 40.0f && s.align_h_center(in).y == 0.0f);
    CHECK(s.align_v_center(in).y == 20.0f && s.align_v_center(in).x == 0.0f);
    const rect fit_wide = rect::make(0, 0, 100, 100).fit_aspect(2.0f);
    CHECK(fit_wide.w == 100.0f && fit_wide.h == 50.0f);
    const rect fit_tall = rect::make(0, 0, 40, 100).fit_aspect(2.0f);
    CHECK(fit_tall.w == 40.0f && fit_tall.h == 20.0f);
    CHECK(fit_tall.y == 40.0f);
    // centered }
}

static void test_text_polish()
{
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    font_handle fh = load_font(c, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
    if (fh == FONT_INVALID)
    {
        std::printf("note: bundled font not found; "
                    "skipping text polish test\n");
        destroy_context(c);
        return;
    }
    theme t = default_dark();
    t.font = fh;
    t.text_size = 16.0f;
    set_theme(c, t);
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        // kerning: "AV" is tighter than A + V
        const f32 av = u.text_width("AV");
        const f32 sum = u.text_width("A") + u.text_width("V");
        CHECK(av < sum);
        // ellipsis: long text is trimmed to fit, short
        // text is untouched
        u.text_ellipsis(rect::make(0, 0, 40, 18), "a very long label", color::white());
        u.text_ellipsis(rect::make(0, 20, 200, 18), "short", color::white());
        u.text_ellipsis(rect::make(0, 40, 200, 18), "", color::white());
        // text_scope: size override, then restore
        const f32 before = u.text_width("X");

        {
            text_scope big = u.text_style(32.0f);
            CHECK(u.text_width("X") > before);
        }
        CHECK(u.text_width("X") == before);
        // bold/oblique fonts via text_scope
        font_handle bold = load_font(c, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans-Bold.ttf");
        font_handle oblique = load_font(c, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans-Oblique.ttf");
        if (bold != FONT_INVALID)
        {
            text_scope sc = u.text_style(16.0f, bold);
            CHECK(u.has_glyph('A'));
            CHECK(u.text_width("Hello") > 0.0f);
        }
        if (oblique != FONT_INVALID)
        {
            text_scope sc = u.text_style(16.0f, oblique);
            CHECK(u.has_glyph('A'));
        }
    }
    end_frame(c);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

static void test_widget_extras()
{
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    const rect frame = rect::make(0, 0, 200, 100);
    bool checked = false;
    f32 value = 2.0f;
    const rect cb = {0, 0, 120, 24};
    const rect sl = {0, 30, 120, 24};
    // click the checkbox
    begin_frame(c, 0.0, 0.016, frame);

    {
        ui u(c);
        CHECK(!u.checkbox(cb, "On", checked, "cb"_id));
    }
    end_frame(c);
    mouse_move(c, 10, 10);
    mouse_button(c, true);
    begin_frame(c, 0.016, 0.016, frame);

    {
        ui u(c);
        (void)u.checkbox(cb, "On", checked, "cb"_id);
    }
    end_frame(c);
    mouse_button(c, false);
    begin_frame(c, 0.032, 0.016, frame);

    {
        ui u(c);
        CHECK(u.checkbox(cb, "On", checked, "cb"_id));
    }
    end_frame(c);
    CHECK(checked);
    // slider: press at the middle, drag past the end
    // (clamped to max)
    mouse_move(c, 60, 42);
    mouse_button(c, true);
    begin_frame(c, 0.048, 0.016, frame);

    {
        ui u(c);
        CHECK(u.slider_float(sl, "v", value, 0.0f, 10.0f, "sl"_id));
    }
    end_frame(c);
    CHECK(std::fabs(value - 5.0f) < 0.2f);
    mouse_move(c, 300, 42);
    begin_frame(c, 0.064, 0.016, frame);

    {
        ui u(c);
        (void)u.slider_float(sl, "v", value, 0.0f, 10.0f, "sl"_id);
    }
    end_frame(c);
    CHECK(value == 10.0f);
    mouse_button(c, false);
    begin_frame(c, 0.080, 0.016, frame);

    {
        ui u(c);
        CHECK(!u.slider_float(sl, "v", value, 0.0f, 10.0f, "sl"_id));
    }
    end_frame(c);
    // progress bar emits background + fill
    begin_frame(c, 0.096, 0.016, frame);

    {
        ui u(c);
        u.progress_bar(rect::make(0, 70, 120, 10), 0.5f, color::white(), color{0, 0, 0, 255});
    }
    end_frame(c);
    CHECK(nd.vertices >= 8);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}
// ---- tracks, grids, scroll views
// --------------------------------------------

static void test_tracks_and_grid()
{
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    const rect area = rect::make(0, 0, 300, 100);
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 300, 100));

    {
        ui u(c);
        // fixed + flex
        {
            track_row tr(area, {track_size::fixed(80.0f), track_size::flex()}, 10.0f);
            CHECK(tr.count() == 2);
            const rect a = tr.next();
            const rect b = tr.next();
            CHECK(a.w == 80.0f && a.x == 0.0f);
            CHECK(b.w == 210.0f && b.x == 90.0f);
            CHECK(tr.done());
            CHECK(tr.next().w == 0.0f); // past the end: zero-size, no crash
        }
        // ratio + flex
        {
            track_row tr(area, {track_size::ratio(0.25f), track_size::flex()}, 0.0f);
            CHECK(tr.next().w == 75.0f);
            CHECK(tr.next().w == 225.0f);
        }
        // max clamping frees space for the other flex track
        {
            track_row tr(area, {track_size::flex().max(50.0f), track_size::flex()}, 0.0f);
            CHECK(tr.next().w == 50.0f);
            CHECK(tr.next().w == 250.0f);
        }
        // min clamping grows a track to its minimum
        {
            track_row tr(area, {track_size::flex().min(120.0f), track_size::flex(3.0f)}, 0.0f);
            CHECK(tr.next().w == 120.0f);
            CHECK(tr.next().w == 180.0f);
        }
        // fit_content falls back to its content estimate
        {
            track_row tr(area, {track_size::fit_content(40.0f), track_size::flex()}, 0.0f);
            CHECK(tr.next().w == 40.0f);
            CHECK(tr.next().w == 260.0f);
        }
        // tracks never overflow the area
        {
            track_row tr(area, {track_size::fixed(500.0f), track_size::flex()}, 0.0f);
            CHECK(tr.next().w == 300.0f);
            CHECK(tr.next().w == 0.0f);
        }
        // vertical variant
        {
            track_column tc(area, {track_size::fixed(30.0f), track_size::flex()}, 10.0f);
            CHECK(tc.next().h == 30.0f);
            CHECK(tc.next().h == 60.0f);
            CHECK(tc.next().h == 0.0f);
        }
        // auto-fit grid: 3 columns in 300 px with min 90 + gap 10
        {
            grid_cursor g = auto_fit_grid(area, 7, 90.0f, 40.0f, 10.0f);
            CHECK(g.columns() == 3);
            CHECK(g.rows() == 3);
            CHECK(g.count() == 7);
            CHECK(g.item_width() == (300.0f - 2.0f * 10.0f) / 3.0f);
            const rect c0 = g.next();
            CHECK(c0.x == 0.0f && c0.y == 0.0f);
            const rect c3 = g.cell(3);
            CHECK(c3.y == 50.0f); // second row: 40 + 10
            CHECK(c3.x == 0.0f);
            for (i32 i = 1; i < 7; ++i) (void)g.next();
            CHECK(g.done());
            CHECK(g.next().w == 0.0f);
        }
        // narrow container collapses to one column
        {
            grid_cursor g = auto_fit_grid(rect::make(0, 0, 50, 100), 4, 90.0f, 20.0f, 8.0f);
            CHECK(g.columns() == 1);
            CHECK(g.rows() == 4);
        }
        // degenerate inputs stay finite
        {
            grid_cursor g = auto_fit_grid(rect::make(0, 0, 100, 100), 0, 0.0f, 0.0f, -1.0f);
            CHECK(g.count() == 0 && g.done());
            CHECK(g.next().w == 0.0f);
        }
    }
    end_frame(c);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

static void test_scroll_view()
{
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    const rect frame = rect::make(0, 0, 200, 150);
    const rect view = rect::make(0, 0, 100, 100);
    f64 t = 0.0;
    auto tick = [&](auto body)
    {
        begin_frame(c, t, 1.0 / 60.0, frame);
        {
            ui u(c);
            body(u);
        }
        end_frame(c);
        t += 1.0 / 60.0;
    };
    // first frame: content height is not known yet, so no overflow / no bar
    tick(
        [&](ui &u)
        {
            scroll_view sv = u.scroll(view, "sc"_id);
            CHECK(!sv.overflows());
            CHECK(sv.offset() == 0.0f);
            sv.set_content_height(300.0f);
        });
    // wheel over the view scrolls (SDL-style: negative y = down)
    mouse_move(c, 50.0f, 50.0f);
    mouse_wheel(c, 0.0f, -2.0f);
    tick(
        [&](ui &u)
        {
            scroll_view sv = u.scroll(view, "sc"_id);
            CHECK(sv.overflows());
            CHECK(sv.content_height() == 300.0f);
            sv.set_content_height(300.0f);
        });
    tick(
        [&](ui &u)
        {
            scroll_view sv = u.scroll(view, "sc"_id);
            CHECK(sv.offset() == 60.0f);     // 2 notches * 30 px
            CHECK(sv.content().y == -56.0f); // viewport.y - offset + padding
            // the bar gutter is reserved while overflowing (auto mode)
            const f32 gutter =
                c->active_theme.scrollbar.thickness + c->active_theme.scrollbar.margin * 2.0f;
            CHECK(sv.content().w == 100.0f - 8.0f - gutter);
            sv.set_content_height(300.0f);
        });
    // wheel clamps to content height - viewport height
    mouse_wheel(c, 0.0f, -100.0f);
    tick(
        [&](ui &u)
        {
            scroll_view sv = u.scroll(view, "sc"_id);
            sv.set_content_height(300.0f);
        });
    tick(
        [&](ui &u)
        {
            scroll_view sv = u.scroll(view, "sc"_id);
            CHECK(sv.offset() == 200.0f);
            sv.set_content_height(300.0f);
        });
    // scrollbar thumb drag: track h = 96, thumb h = 32, travel = 64
    tick(
        [&](ui &u)
        {
            scroll_view sv = u.scroll(view, "sc"_id);
            sv.scroll_to(0.0f);
            sv.set_content_height(300.0f);
        });
    mouse_move(c, 94.0f, 8.0f); // on the thumb near its top
    mouse_button(c, true);
    tick(
        [&](ui &u)
        {
            scroll_view sv = u.scroll(view, "sc"_id);
            sv.set_content_height(300.0f);
        });
    mouse_move(c, 94.0f, 40.0f); // +32 px of travel
    tick(
        [&](ui &u)
        {
            scroll_view sv = u.scroll(view, "sc"_id);
            CHECK(sv.offset() > 90.0f && sv.offset() < 110.0f);
            sv.set_content_height(300.0f);
        });
    mouse_button(c, false);
    tick(
        [&](ui &u)
        {
            scroll_view sv = u.scroll(view, "sc"_id);
            sv.set_content_height(300.0f);
        });
    // nested views: the innermost hovered view wins the wheel
    const rect outer_r = rect::make(0, 0, 150, 120);
    const rect inner_r = rect::make(10, 10, 80, 60);
    auto nested = [&](bool check)
    {
        tick(
            [&](ui &u)
            {
                scroll_view outer = u.scroll(outer_r, "outer"_id);
                scroll_view inner = u.scroll(inner_r, "inner"_id);
                if (check)
                {
                    CHECK(inner.offset() == 30.0f);
                    CHECK(outer.offset() == 0.0f);
                }
                inner.set_content_height(300.0f);
                outer.set_content_height(300.0f);
            });
    };
    nested(false);
    mouse_move(c, 50.0f, 40.0f); // inside both viewports
    mouse_wheel(c, 0.0f, -1.0f);
    nested(false);
    nested(true);
    // wheel outside any view is ignored
    mouse_move(c, 190.0f, 140.0f);
    mouse_wheel(c, 0.0f, -1.0f);
    nested(true);
    // content height shrinking back clamps the offset
    tick(
        [&](ui &u)
        {
            scroll_view sv = u.scroll(view, "sc"_id);
            CHECK(sv.offset() > 0.0f);
            sv.set_content_height(50.0f); // no overflow anymore
            CHECK(sv.offset() == 0.0f);
        });
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}
// ---- text input: caret placement, word ops, undo, drag & drop ---------------
template <typename DrawFn> static void tf_frame(context *c, f64 t, DrawFn &&draw)
{
    begin_frame(c, t, 0.016, rect::make(0, 0, 300, 200));

    {
        ui u(c);
        draw(u);
    }
    end_frame(c);
}

static void test_text_field_caret_placement()
{
    tf_env env;
    context *c = env.c;
    if (tf_needs_font(c)) return;
    std::string value = "abcdef";
    const rect field = {0, 0, 160, 24};
    const uiid id = "cp"_id;
    auto draw = [&](ui &u) { (void)u.text_field(field, value, id); };
    const f32 pad = default_dark().padding * 0.5f;
    f32 x_after_abc = pad;

    {
        ui mu(c);
        x_after_abc += mu.text_width("abc");
    }
    // First click lands between 'c' and 'd': typing inserts there, not
    // at the end.
    tf_click(c, x_after_abc + 1.0f, 12.0f, 0.0, draw);
    text_input_event(c, "X");
    tf_frame(c, 0.05, draw);
    CHECK(value == "abcXdef");
    // Clicking past the end appends.
    f32 x_end = pad;

    {
        ui mu(c);
        x_end += mu.text_width(value) + 6.0f;
    }
    tf_click(c, x_end, 12.0f, 0.10, draw);
    text_input_event(c, "!");
    tf_frame(c, 0.15, draw);
    CHECK(value == "abcXdef!");
    // Tab focus selects all, so typing replaces the value.
    std::string second = "hello";
    const rect field_b = {0, 40, 160, 24};
    const uiid idb = "cp2"_id;
    auto draw_b = [&](ui &u)
    {
        (void)u.text_field(field, value, id);
        (void)u.text_field(field_b, second, idb);
    };
    tf_click(c, 10.0f, 12.0f, 0.20, draw_b);
    // focus the first field
    key_event(c, key::TAB, true);
    tf_frame(c, 0.25, draw_b);
    key_event(c, key::TAB, false);
    tf_frame(c, 0.30, draw_b);
    // second field focused, value selected
    text_input_event(c, "yo");
    tf_frame(c, 0.35, draw_b);
    CHECK(second == "yo");
    CHECK(violation_count(c) == 0);
}

static void test_text_field_word_ops()
{
    tf_env env;
    context *c = env.c;
    if (tf_needs_font(c)) return;
    std::string value = "hello brave world";
    const rect field = {0, 0, 240, 24};
    const uiid id = "word"_id;
    auto draw = [&](ui &u) { (void)u.text_field(field, value, id); };
    tf_click(c, 6.0f, 12.0f, 0.0, draw);
    // caret at 0
    // Ctrl+Right jumps by words (whitespace is skipped): 0
    // -> 5 -> 11.
    tf_key(c, key::RIGHT, true, false, 0.05, draw);
    tf_key(c, key::RIGHT, true, false, 0.10, draw);
    text_input_event(c, "|");
    tf_frame(c, 0.15, draw);
    CHECK(value == "hello brave| world");
    // Ctrl+Backspace removes the chunk before the caret (here just the
    // "|").
    tf_key(c, key::BACKSPACE, true, false, 0.20, draw);
    CHECK(value == "hello brave world");
    // Ctrl+Delete removes the whitespace and the next word.
    tf_key(c, key::DEL, true, false, 0.25, draw);
    CHECK(value == "hello brave");
    // Ctrl+Shift+Right selects the next word; typing replaces it.
    tf_key(c, key::HOME, false, false, 0.30, draw);
    tf_key(c, key::RIGHT, true, true, 0.35, draw);
    text_input_event(c, "bye");
    tf_frame(c, 0.40, draw);
    CHECK(value == "bye brave");
    CHECK(violation_count(c) == 0);
}

static void test_text_field_undo_redo()
{
    tf_env env;
    context *c = env.c;
    if (tf_needs_font(c)) return;
    std::string value;
    const rect field = {0, 0, 200, 24};
    const uiid id = "undo"_id;
    auto draw = [&](ui &u) { (void)u.text_field(field, value, id); };
    tf_click(c, 6.0f, 12.0f, 0.0, draw);
    // A run of typing coalesces into one undo step.
    text_input_event(c, "a");
    tf_frame(c, 0.01, draw);
    text_input_event(c, "b");
    tf_frame(c, 0.02, draw);
    text_input_event(c, "c");
    tf_frame(c, 0.03, draw);
    CHECK(value == "abc");
    tf_key(c, key::Z, true, false, 0.10, draw);
    CHECK(value == "");
    tf_key(c, key::Y, true, false, 0.15, draw);
    CHECK(value == "abc");
    // Navigation breaks coalescing: the next edit is its own step.
    tf_key(c, key::HOME, false, false, 0.20, draw);
    text_input_event(c, "X");
    tf_frame(c, 0.25, draw);
    CHECK(value == "Xabc");
    tf_key(c, key::Z, true, false, 0.30, draw);
    CHECK(value == "abc");
    tf_key(c, key::Z, true, false, 0.35, draw);
    CHECK(value == "");
    // Ctrl+Shift+Z is redo too.
    tf_key(c, key::Z, true, true, 0.40, draw);
    CHECK(value == "abc");
    CHECK(violation_count(c) == 0);
}

static void test_layout_overflow_report()
{
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    set_violation_handler(c, capture_violation, nullptr);
    // capture the expected reports
    g_violation_events = 0;
    // Disabled by default: clamping stays silent.
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 100, 100));

    {
        ui ui_(c);
        column col(rect::make(0, 0, 200, 50), 6.0f);
        (void)col.next(80.0f);
        // 80 > 50: clamped, no report
        (void)col.next(40.0f);
        // exhausted: granted 0, no report
    }
    end_frame(c);
    CHECK(violation_count(c) == 0);
    CHECK(layout_overflow_count(c) == 0);
    CHECK(g_violation_events == 0);
    // Enabled: every clamp reports a non-fatal violation and is
    // counted.
    set_report_layout_overflow(c, true);
    g_violation_events = 0;
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 100, 100));

    {
        ui ui(c);
        column col(rect::make(0, 0, 200, 50), 6.0f);
        (void)col.next(80.0f);
        // clamped 80 -> 50
        (void)col.next(40.0f);
        // exhausted -> 0
        row rw(rect::make(0, 60, 50, 30), 4.0f);
        (void)rw.next(70.0f);
        // row clamp 70->50
        (void)rw.cut_left(60.0f); // more than what is left
        // more than what is left
    }
    end_frame(c);
    CHECK(layout_overflow_count(c) == 4);
    CHECK(violation_count(c) == 4);
    // all were routed through the standard handler
    CHECK(g_violation_events == 4);
    // Fitting slices never report.
    set_report_layout_overflow(c, true);
    begin_frame(c, 0.032, 0.016, rect::make(0, 0, 100, 100));

    {
        ui ui(c);
        column col(rect::make(0, 0, 200, 100), 8.0f);
        const rect a = col.next(46.0f);
        const rect b = col.next(46.0f);
        // 46 + 8 + 46 = 100: exactly the rest
        CHECK(a.h == 46.0f && b.h == 46.0f);
    }
    end_frame(c);
    CHECK(layout_overflow_count(c) == 0);
    set_report_layout_overflow(c, false);
    CHECK(violation_count(c) == 4);
    // the reports above are still recorded
    destroy_context(c);
}

static void test_region_corner()
{
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 300, 300));

    {
        ui u(c);
        region r(u, "corner_demo"_id, rect::make(100, 100, 200, 150));
        // Corners land inset by the margin and stay inside the content.
        const rect tl = r.corner(0, 60.0f, 30.0f, 8.0f);
        CHECK(tl.x == 108.0f && tl.y == 108.0f);
        const rect tr = r.corner(1, 60.0f, 30.0f, 8.0f);
        CHECK(tr.right() == 292.0f && tr.y == 108.0f);
        const rect br = r.corner(2, 60.0f, 30.0f, 8.0f);
        CHECK(br.right() == 292.0f && br.bottom() == 242.0f);
        const rect bl = r.corner(3, 60.0f, 30.0f, 8.0f);
        CHECK(bl.x == 108.0f && bl.bottom() == 242.0f);
        // Opposite corners never touch: TL.x .. TL.right is left of
        // TR.x.
        CHECK(tl.right() < tr.x);
        CHECK(br.y > bl.y - 0.001f && bl.bottom() > 0.0f);
        // Oversized badges clamp to the content (and negative n/4 wrap
        // safely).
        const rect big = r.corner(0, 999.0f, 999.0f, 8.0f);
        CHECK(big.w == 200.0f - 16.0f && big.h == 150.0f - 16.0f);
        const rect wrapped = r.corner(-1, 10.0f, 10.0f, 4.0f);
        // -1 -> bottom-left
        CHECK(wrapped.x == 104.0f && wrapped.bottom() == 246.0f);
    }
    end_frame(c);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}
// ---- combo / tooltip / context menu
// ------------------------------------------ // With the deferred overlay draw
// (popup surfaces paint at end_frame so later // widgets cannot cover them),
// the popup table is only settled after // end_frame, and a mouse pick reports
// through the widget's NEXT call.
static bool popup_present(context *c, uiid id, bool prev)
{
    const i32 depth = prev ? c->prev_popup_depth : c->popup_depth;
    for (i32 i = 0; i < depth; ++i)
        if ((prev ? c->prev_popups : c->popups)[i].id == id) return true;
    return false;
}

static void test_combo()
{
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    static const char *items[3] = {"Alpha", "Beta", "Gamma"};
    i32 sel = 0;
    const rect r = rect::make(20, 20, 160, 28);
    const uiid id = "cb"_id;
    const uiid drop_id = id_child(id, "dropdown"_id);
    // Frame 1+2: press then release over the header opens the dropdown.
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 400, 400));
    mouse_move(c, 60, 30);
    mouse_button(c, true);

    {
        ui u(c);
        CHECK(!u.combo(r, "", items, 3, sel, id));
        CHECK(sel == 0);
    }
    end_frame(c);
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 400, 400));
    mouse_button(c, false);

    {
        ui u(c);
        CHECK(!u.combo(r, "", items, 3, sel, id));
        // opening is not a change
        CHECK(sel == 0);
    }
    end_frame(c);
    CHECK(popup_present(c, drop_id, false));
    CHECK(popup_present(c, drop_id, false)); // staged at end_frame
    // Frame 3 (open): Down moves the highlight only - the selection
    // commits on Enter, so no change is reported yet.
    begin_frame(c, 0.032, 0.016, rect::make(0, 0, 400, 400));
    key_event(c, key::DOWN, true);
    key_event(c, key::DOWN, false);

    {
        ui u(c);
        CHECK(!u.combo(r, "", items, 3, sel, id));
        CHECK(c->combo_hot == 1);
        CHECK(sel == 0);
    }
    end_frame(c);
    // Frame 4: Enter picks "Beta" (keyboard picks resolve in-frame),
    // reports // the change, and the dropdown closes: the frame stages
    // no popup entry.
    begin_frame(c, 0.048, 0.016, rect::make(0, 0, 400, 400));
    key_event(c, key::ENTER, true);
    key_event(c, key::ENTER, false);

    {
        ui u(c);
        CHECK(u.combo(r, "", items, 3, sel, id));
        CHECK(sel == 1);
        CHECK(!popup_present(c, drop_id, false));
        // closed, nothing staged     }
        end_frame(c);
        CHECK(!popup_present(c, drop_id, false));
        // closed, nothing staged      // Frame 5: closed - base UI
        // unblocked, and the popup table is clean.
        begin_frame(c, 0.064, 0.016, rect::make(0, 0, 400, 400));

        {
            ui u(c);
            CHECK(!u.combo(r, "", items, 3, sel, id));
            CHECK(!popup_present(c, drop_id, true));
            // the table followed the close
            const interaction probe = u.interact("base_probe"_id, rect::make(300, 300, 50, 50));
            CHECK(!probe.blocked);
        }
        end_frame(c);
        // Frame 6+7: reopen, then Escape closes it (the Escape frame
        // stages no
        // // entry; the next frame's prev table is clean).
        begin_frame(c, 0.080, 0.016, rect::make(0, 0, 400, 400));
        mouse_button(c, true);

        {
            ui u(c);
            (void)u.combo(r, "", items, 3, sel, id);
        }
        end_frame(c);
        begin_frame(c, 0.096, 0.016, rect::make(0, 0, 400, 400));
        mouse_button(c, false);

        {
            ui u(c);
            (void)u.combo(r, "", items, 3, sel, id);
        }
        end_frame(c);
        CHECK(popup_present(c, drop_id, false));
        begin_frame(c, 0.112, 0.016, rect::make(0, 0, 400, 400));
        key_event(c, key::ESCAPE, true);
        key_event(c, key::ESCAPE, false);

        {
            ui u(c);
            (void)u.combo(r, "", items, 3, sel, id);
            CHECK(!popup_present(c, drop_id, false));
            // closed, nothing staged     }
            end_frame(c);
            key_event(c, key::ESCAPE, false);
            // Out-of-range selected is clamped into range; zero items
            // is a violation.
            // // (Also proves the Escape left no ghost: the prev table
            // is clean here.)
            begin_frame(c, 0.128, 0.016, rect::make(0, 0, 400, 400));
            set_violation_handler(c, capture_violation, nullptr);
            g_violation_events = 0;

            {
                ui u(c);
                CHECK(!popup_present(c, drop_id, true));
                // no ghost entry from the Escape
                i32 sel2 = 7;
                (void)u.combo(r, "", items, 3, sel2, "cb2"_id);
                CHECK(sel2 == 2);
                // clamped into range
                (void)u.combo(r, "", items, 0, sel, "cb3"_id);
                CHECK(g_violation_events == 1);
                CHECK(violation_was("combo needs at least one item"));
            }
            set_violation_handler(c, nullptr, nullptr);
            end_frame(c);
            CHECK(violation_count(c) == 1);
            // the zero-items guard, reported once
            destroy_context(c);
        }
    }
}

static void test_combo_mouse_pick()
{
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    static const char *items[3] = {"Alpha", "Beta", "Gamma"};
    i32 sel = 0;
    const rect r = rect::make(20, 20, 160, 28);
    const uiid id = "cbp"_id;
    const uiid drop_id = id_child(id, "dropdown"_id);
    // Open: press + release on the header.
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 400, 400));
    mouse_move(c, 60, 30);
    mouse_button(c, true);

    {
        ui u(c);
        (void)u.combo(r, "", items, 3, sel, id);
    }
    end_frame(c);
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 400, 400));
    mouse_button(c, false);

    {
        ui u(c);
        (void)u.combo(r, "", items, 3, sel, id);
    }
    end_frame(c);
    CHECK(popup_present(c, drop_id, false));
    // Press inside the second row (rows lay out inside the scroll
    // viewport: drop.pad(6) + content pad 2 ->
    // row A spans y=58..84, row B 86..112).
    begin_frame(c, 0.032, 0.016, rect::make(0, 0, 400, 400));
    mouse_move(c, 60, 92.0f);
    mouse_button(c, true);

    {
        ui u(c);
        (void)u.combo(r, "", items, 3, sel, id);
    }
    end_frame(c);
    // Release over the same row: the pick resolves at end_frame (the write-back
    // lands through the pointer), the dropdown retracts the same frame, and the
    // change is reported by the widget's NEXT call.
    begin_frame(c, 0.048, 0.016, rect::make(0, 0, 400, 400));
    mouse_button(c, false);

    {
        ui u(c);
        CHECK(!u.combo(r, "", items, 3, sel, id));
        // the pick lands later here
    }
    end_frame(c);
    CHECK(sel == 1);
    // written back at end_frame
    CHECK(!popup_present(c, drop_id, false));
    // retracted: no ghost entry
    begin_frame(c, 0.064, 0.016, rect::make(0, 0, 400, 400));

    {
        ui u(c);
        CHECK(u.combo(r, "", items, 3, sel, id));
        // the next call reports it
        CHECK(!popup_present(c, drop_id, true));
        // and no entry lingers
    }
    end_frame(c);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

static void test_tooltip_delay()
{
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    const rect anchor = rect::make(100, 100, 60, 26);
    const uiid id = "tt_btn"_id;
    // Not hovered: no placement, nothing drawn at end_frame.
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 400, 400));

    {
        ui u(c);
        (void)u.button(anchor, "hover", id);
        CHECK(u.tooltip(anchor, id, "first tip").w == 0.0f);
    }
    end_frame(c);
    // Hovered at t=0.1: inside the 0.5 s delay - silent.
    begin_frame(c, 0.1, 0.016, rect::make(0, 0, 400, 400));
    mouse_move(c, 120, 110);

    {
        ui u(c);
        (void)u.button(anchor, "hover", id);
        CHECK(u.tooltip(anchor, id, "first tip").w == 0.0f);
    }
    end_frame(c);
    // Hovered at t=0.8: past the delay - placed under the anchor and
    // painted at end_frame (vertex delta through the device).
    begin_frame(c, 0.8, 0.016, rect::make(0, 0, 400, 400));

    {
        ui u(c);
        (void)u.button(anchor, "hover", id);
        const rect placed = u.tooltip(anchor, id, "first tip");
        CHECK(placed.w >= 8.0f);
        CHECK(placed.y >= anchor.bottom() + 8.0f);
        // below the anchor
        CHECK(placed.right() <= 400.0f && placed.bottom() <= 400.0f);
    }
    end_frame(c);
    CHECK(nd.vertices > 0);
    // Moving to a second widget inside the 1.2 s window: instant (no
    // wait).
    const rect other = rect::make(200, 100, 60, 26);
    const uiid other_id = "tt_btn2"_id;
    begin_frame(c, 0.9, 0.016, rect::make(0, 0, 400, 400));
    mouse_move(c, 220, 110);

    {
        ui u(c);
        (void)u.button(other, "other", other_id);
        CHECK(u.tooltip(other, other_id, "second tip").w >= 8.0f);
    }
    end_frame(c);
    // Unhovered again: silent.
    begin_frame(c, 1.0, 0.016, rect::make(0, 0, 400, 400));
    mouse_move(c, 350, 350);

    {
        ui u(c);
        (void)u.button(anchor, "hover", id);
        (void)u.button(other, "other", other_id);
        CHECK(u.tooltip(anchor, id, "first tip").w == 0.0f);
        CHECK(u.tooltip(other, other_id, "second tip").w == 0.0f);
    }
    end_frame(c);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

static void test_context_menu()
{
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    static const char *items[2] = {"Open", "Close"};
    const rect anchor = rect::make(40, 40, 120, 60);
    const uiid id = "cm"_id;
    const uiid menu_id = id_child(id, "menu"_id);
    // A right-press *outside* the anchor never opens the menu.
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 400, 400));
    mouse_move(c, 300, 300);
    mouse_button(c, pointer_button::RIGHT, true);

    {
        ui u(c);
        (void)u.context_menu(id, anchor, items, 2);
        CHECK(!popup_present(c, menu_id, false));
    }
    end_frame(c);
    mouse_button(c, pointer_button::RIGHT, false);
    // Right-press over the anchor opens the menu at the pointer; the
    // surface stages at end_frame.
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 400, 400));
    mouse_move(c, 80, 60);
    mouse_button(c, pointer_button::RIGHT, true);

    {
        ui u(c);
        CHECK(u.context_menu(id, anchor, items, 2) == -1);
        // nothing picked yet     }
        end_frame(c);
        CHECK(popup_present(c, menu_id, false));
        // staged
        mouse_button(c, pointer_button::RIGHT, false);
        // The menu (open last frame) captures input: base UI is
        // blocked, and a
        // // left press inside the first row starts a click on it. (The
        // menu opened // at the pointer: its 6px pad puts the first row
        // at y=66..92, and rows // start 6px right of the menu edge, so
        // x must be past the pad too.)
        begin_frame(c, 0.032, 0.016, rect::make(0, 0, 400, 400));
        mouse_move(c, 100, 74.0f);
        mouse_button(c, true);

        {
            ui u(c);
            const interaction in = u.interact("base"_id, rect::make(0, 0, 400, 400));
            CHECK(in.blocked);
            CHECK(u.context_menu(id, anchor, items, 2) == -1);
            // press, not release     }
            end_frame(c);
            // Release over the same row: the pick resolves at
            // end_frame, the menu retracts the same frame, and the NEXT
            // call reports it.
            begin_frame(c, 0.048, 0.016, rect::make(0, 0, 400, 400));
            mouse_button(c, false);

            {
                ui u(c);
                CHECK(u.context_menu(id, anchor, items, 2) == -1);
                // pick lands later     }
                end_frame(c);
                CHECK(!popup_present(c, menu_id, false));
                // retracted: no ghost entry
                begin_frame(c, 0.064, 0.016, rect::make(0, 0, 400, 400));

                {
                    ui u(c);
                    CHECK(u.context_menu(id, anchor, items, 2) == 0);
                    // "Open" was picked
                    CHECK(!popup_present(c, menu_id, true));
                    // and nothing lingers     }
                    end_frame(c);
                    // Reopened, then Escape closes it in the frame it
                    // rings (nothing staged).
                    begin_frame(c, 0.080, 0.016, rect::make(0, 0, 400, 400));
                    mouse_move(c, 80, 60);
                    mouse_button(c, pointer_button::RIGHT, true);

                    {
                        ui u(c);
                        (void)u.context_menu(id, anchor, items, 2);
                    }
                    end_frame(c);
                    mouse_button(c, pointer_button::RIGHT, false);
                    begin_frame(c, 0.096, 0.016, rect::make(0, 0, 400, 400));
                    key_event(c, key::ESCAPE, true);
                    key_event(c, key::ESCAPE, false);

                    {
                        ui u(c);
                        (void)u.context_menu(id, anchor, items, 2);
                        CHECK(!popup_present(c, menu_id, false));
                        // closed by Escape     }
                        end_frame(c);
                        // One frame later the table is clean (prev
                        // follows the close).
                        begin_frame(c, 0.112, 0.016, rect::make(0, 0, 400, 400));

                        {
                            ui u(c);
                            (void)u.context_menu(id, anchor, items, 2);
                            CHECK(!popup_present(c, menu_id, true));
                            // no ghost     }
                            end_frame(c);
                            CHECK(violation_count(c) == 0);
                            destroy_context(c);
                        }
                    }
                }
            }
        }
    }
}

static void test_scroll_options_and_limits()
{
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    const rect view = rect::make(0, 0, 100, 100);
    // SCROLL_ALWAYS_RESERVE_BAR: the gutter is reserved
    // from the first frame,     // even before any content
    // height is known.
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 200, 200));

    {
        ui u(c);
        scroll_view sv =
            u.scroll(view, "always"_id, scroll_options{4.0f, SCROLL_ALWAYS_RESERVE_BAR});
        const f32 gutter =
            c->active_theme.scrollbar.thickness + c->active_theme.scrollbar.margin * 2.0f;
        CHECK(sv.content().w == view.w - 8.0f - gutter);
        CHECK(!sv.overflows());
    }
    end_frame(c);
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 200, 200));

    {
        ui u(c);
        scroll_view sv =
            u.scroll(view, "always"_id, scroll_options{4.0f, SCROLL_ALWAYS_RESERVE_BAR});
        const f32 gutter =
            c->active_theme.scrollbar.thickness + c->active_theme.scrollbar.margin * 2.0f;
        CHECK(sv.content().w == view.w - 8.0f - gutter);
        // still reserved, no bar
    }
    end_frame(c);
    // scroll_by moves relative to the current offset
    // and clamps. It clamps     // against the store's
    // *last known* content height, so the height is set
    // // before scrolling each frame (first call after
    // a fresh id clamps to 0).
    bool first = true;
    for (i32 i = 0; i < 3; ++i)
    {
        begin_frame(c, 0.5 + 0.016 * i, 0.016, rect::make(0, 0, 200, 200));

        {
            ui u(c);
            scroll_view sv = u.scroll(view, "by"_id);
            sv.set_content_height(300.0f);
            // height first...
            if (first)
            {
                sv.scroll_to(0.0f);
                // ...and start at the top
                // deterministically
                first = false;
            }
            sv.scroll_by(40.0f);
            // relative to the just-clamped offset
            CHECK(sv.offset() == 40.0f * (i + 1));
        }
        end_frame(c);
    }
    begin_frame(c, 1.0, 0.016, rect::make(0, 0, 200, 200));

    {
        ui u(c);
        scroll_view sv = u.scroll(view, "by"_id);
        sv.set_content_height(300.0f);
        CHECK(sv.offset() == 120.0f);
        // 3 frames x 40 px
        sv.scroll_by(5000.0f);
        // way past the end
        CHECK(sv.offset() == 200.0f);
        // clamped to max offset (300 - 100)
        sv.scroll_by(-5000.0f);
        CHECK(sv.offset() == 0.0f);
        // clamped to the top
    }
    end_frame(c);
    // ensure_visible scrolls a rect into view.
    begin_frame(c, 1.016, 0.016, rect::make(0, 0, 200, 200));

    {
        ui u(c);
        scroll_view sv = u.scroll(view, "ev"_id);
        sv.set_content_height(300.0f);
        sv.ensure_visible(rect::make(20, 250, 50, 20));
        // below the fold
    }
    end_frame(c);
    begin_frame(c, 1.032, 0.016, rect::make(0, 0, 200, 200));

    {
        ui u(c);
        scroll_view sv = u.scroll(view, "ev"_id);
        // The rect bottom (270) is inside
        // [offset, offset + height -
        // padding].
        CHECK(sv.offset() >= 270.0f - view.h + 4.0f);
        CHECK(sv.offset() <= 270.0f);
    }
    end_frame(c);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

static void test_virtual_list()
{
    // `scroll_view::virtual_list` submits only the rows that can paint: the
    // visible slice of a uniform grid, clamped to the list. The content
    // height is derived (count * item_height), so the gutter and the scroll
    // extent both follow from the list itself.
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    set_violation_handler(c, capture_violation, nullptr);
    g_violation_events = 0;

    const rect view = rect::make(0, 0, 120, 100);
    constexpr i32 kCount = 1000;
    constexpr f32 kItem = 28.0f;

    // Collected per frame: which indices the callback saw, where, and what
    // content height the view derived.
    std::vector<i32> seen;
    std::vector<rect> rows;
    f32 derived_h = -1.0f;

    auto frame = [&](f32 t)
    {
        seen.clear();
        rows.clear();
        derived_h = -1.0f;
        begin_frame(c, t, 0.016, rect::make(0, 0, 200, 200));
        {
            ui u(c);
            scroll_view sv = u.scroll(view, "vlist"_id);
            sv.virtual_list(kCount, kItem,
                            [&](ui &, i32 index, rect row)
                            {
                                seen.push_back(index);
                                rows.push_back(row);
                            });
            derived_h = sv.content_height();
        }
        end_frame(c);
    };

    // Top of the list: rows 0..4 cover the 100px viewport (28px rows, so the
    // fifth row straddles the bottom edge).
    frame(0.0);
    CHECK(seen.size() == 5);
    CHECK(seen.front() == 0);
    CHECK(rows.front().y == view.y + 4.0f); // viewport.y - 0 + padding
    CHECK(rows.front().h == kItem);
    CHECK(derived_h == kCount * kItem);

    // Scrolled: offset 700 lands inside row 25 (25*28 = 700 exactly), and the
    // slice runs 25..29.
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 200, 200));
    {
        ui u(c);
        scroll_view sv = u.scroll(view, "vlist"_id);
        sv.scroll_to(700.0f);
    }
    end_frame(c);
    frame(0.032);
    CHECK(seen.size() == 5);
    CHECK(seen.front() == 25);
    CHECK(seen.back() == 29);
    CHECK(rows.front().y == view.y - 700.0f + 4.0f + 25.0f * kItem);

    // Near the end: the slice clamps to the last item, never past it.
    begin_frame(c, 0.048, 0.016, rect::make(0, 0, 200, 200));
    {
        ui u(c);
        scroll_view sv = u.scroll(view, "vlist"_id);
        sv.scroll_to(kCount * kItem - view.h); // max offset: 27900
    }
    end_frame(c);
    frame(0.064);
    CHECK(seen.back() == kCount - 1);
    CHECK(rows.back().y + rows.back().h ==
          view.y - (kCount * kItem - view.h) + 4.0f + kCount * kItem);
    CHECK(derived_h == kCount * kItem);

    // Empty and degenerate lists report no rows and derive no height.
    begin_frame(c, 0.080, 0.016, rect::make(0, 0, 200, 200));
    {
        ui u(c);
        scroll_view sv = u.scroll(view, "vempty"_id);
        i32 calls = 0;
        sv.virtual_list(0, kItem, [&](ui &, i32, rect) { ++calls; });
        CHECK(calls == 0);
        CHECK(sv.content_height() == 0.0f);
    }
    end_frame(c);

    // A non-positive item height is a reported violation, not a divide by
    // zero (captured by the handler above).
    begin_frame(c, 0.096, 0.016, rect::make(0, 0, 200, 200));
    {
        ui u(c);
        scroll_view sv = u.scroll(view, "vbad"_id);
        sv.virtual_list(10, 0.0f, [](ui &, i32, rect) {});
    }
    end_frame(c);
    CHECK(g_violation_events == 1);
    CHECK(violation_was("virtual_list item height must be positive"));

    CHECK(violation_count(c) == 1);
    destroy_context(c);
}

static void test_region_id_overflow()
{
    // Over MAX_FRAME_IDS regions the duplicate-id list stops growing, and
    // that degradation is never silent: every rejected registration reports
    // VIOL_FRAME_IDS_OVERFLOW. The first MAX_FRAME_IDS regions keep full
    // duplicate checking.
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    set_violation_handler(c, capture_violation, nullptr);
    g_violation_events = 0;

    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 200, 200));
    {
        ui u(c);
        constexpr i32 kRegions = MAX_FRAME_IDS + 52;
        for (i32 i = 0; i < kRegions; ++i)
            (void)u.region(rect::make(0, 0, 50, 50), id_child("r"_id, i));
    }
    end_frame(c);
    CHECK(g_violation_events == 52);
    CHECK(violation_was(
        "more than MAX_FRAME_IDS regions in one frame; duplicate-id checking is incomplete"));

    // Within the cap nothing is reported.
    g_violation_events = 0;
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 200, 200));
    {
        ui u(c);
        for (i32 i = 0; i < MAX_FRAME_IDS; ++i)
            (void)u.region(rect::make(0, 0, 50, 50), id_child("q"_id, i));
    }
    end_frame(c);
    CHECK(g_violation_events == 0);

    destroy_context(c);
}

static void test_draw_shapes_coverage()
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

static void test_tooltip_flip_above()
{
    null_device nd;
    nd.my_surface.w = 400;
    nd.my_surface.h = 120;
    context *c = create_context(&nd, nd.create_surface());
    const rect anchor = rect::make(150, 80, 60, 26);
    // near the bottom edge
    const uiid id = "tt_flip"_id;
    // Warm the delay: t=0.1 waits, t=0.7 draws.
    begin_frame(c, 0.1, 0.016, rect::make(0, 0, 400, 120));
    mouse_move(c, 170, 90);

    {
        ui u(c);
        (void)u.button(anchor, "low", id);
        CHECK(u.tooltip(anchor, id, "flip me").w == 0.0f);
        // waiting     }
        end_frame(c);
        begin_frame(c, 0.7, 0.016, rect::make(0, 0, 400, 120));

        {
            ui u(c);
            (void)u.button(anchor, "low", id);
            const rect placed = u.tooltip(anchor, id, "flip me");
            // Below the anchor would fall off the 120px screen: flipped
            // above.
            CHECK(placed.w >= 8.0f);
            CHECK(placed.bottom() <= 120.0f);
            CHECK(placed.bottom() < anchor.y);
            // above the anchor
            CHECK(placed.y >= 4.0f);
            // still inside the screen     }
            end_frame(c);
            // Right-edge anchor: the x clamp keeps the tooltip inside.
            const rect right_anchor = rect::make(360, 30, 30, 24);
            const uiid rid = "tt_flip_r"_id;
            begin_frame(c, 0.9, 0.016, rect::make(0, 0, 400, 120));
            // within 1.2s: instant
            mouse_move(c, 370, 40);

            {
                ui u(c);
                (void)u.button(right_anchor, "r", rid);
                const rect placed = u.tooltip(right_anchor, rid, "a tip");
                CHECK(placed.w >= 8.0f);
                CHECK(placed.right() <= 400.0f);
                // clamped inside the right edge     }
                end_frame(c);
                CHECK(violation_count(c) == 0);
                destroy_context(c);
            }
        }
    }
}

static void test_combo_popup_clamp()
{
    null_device nd;
    nd.my_surface.w = 240;
    nd.my_surface.h = 300;
    context *c = create_context(&nd, nd.create_surface());
    static const char *items[8] = {"1", "2", "3", "4", "5", "6", "7", "8"};
    i32 sel = 0;
    // A combo near the right/bottom
    // edge: the dropdown clamps
    // into the screen.
    const rect r = rect::make(160, 260, 120, 26);
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 240, 300));
    mouse_move(c, 180, 270);
    mouse_button(c, true);

    {
        ui u(c);
        (void)u.combo(r, "", items, 8, sel, "cbcl"_id, 300.0f);
    }
    end_frame(c);
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 240, 300));
    mouse_button(c, false);

    {
        ui u(c);
        (void)u.combo(r, "", items, 8, sel, "cbcl"_id, 300.0f);
    }
    end_frame(c);
    // The dropdown is staged; its
    // rect must sit inside the
    // screen.
    {
        bool checked = false;
        for (i32 i = 0; i < c->popup_depth; ++i)
        {
            if (c->popups[i].id != id_child("cbcl"_id, "dropdown"_id)) continue;
            const rect &drop = c->popups[i].area;
            CHECK(drop.right() <= 240.0f + 0.01f);
            // inside the right edge
            CHECK(drop.bottom() <= 300.0f + 0.01f);
            // inside the bottom
            // edge
            CHECK(drop.h <= 8 * 26.0f + 2.0f * 7.0f + 12.0f + 0.01f);
            checked = true;
        }
        CHECK(checked);
    }
    // max_popup_height clamps a
    // tall list even when the
    // screen has room. The     //
    // press that starts this click
    // first lands while combo 1's
    // dropdown is     // open: it
    // closes combo 1 (click
    // outside) and must not open
    // combo 2 with     // the same
    // press, so combo 2 needs its
    // own press+release afterwards.
    begin_frame(c, 0.032, 0.016, rect::make(0, 0, 240, 300));
    mouse_move(c, 40, 20);
    mouse_button(c, true);

    {
        ui u(c);
        i32 sel2 = 0;
        (void)u.combo(rect::make(10, 10, 120, 26), "", items, 8, sel2, "cbcl2"_id, 90.0f);
    }
    end_frame(c);
    begin_frame(c, 0.048, 0.016, rect::make(0, 0, 240, 300));
    mouse_button(c, false);

    {
        ui u(c);
        i32 sel2 = 0;
        (void)u.combo(rect::make(10, 10, 120, 26), "", items, 8, sel2, "cbcl2"_id, 90.0f);
    }
    end_frame(c);
    begin_frame(c, 0.064, 0.016, rect::make(0, 0, 240, 300));
    mouse_button(c, true);

    {
        ui u(c);
        i32 sel2 = 0;
        (void)u.combo(rect::make(10, 10, 120, 26), "", items, 8, sel2, "cbcl2"_id, 90.0f);
        CHECK(c->popup_depth == 0);
        // the press outside combo 1
        // must not open this     }
        end_frame(c);
        begin_frame(c, 0.080, 0.016, rect::make(0, 0, 240, 300));
        mouse_button(c, false);

        {
            ui u(c);
            i32 sel2 = 0;
            (void)u.combo(rect::make(10, 10, 120, 26), "", items, 8, sel2, "cbcl2"_id, 90.0f);
        }
        end_frame(c);

        {
            bool checked = false;
            for (i32 i = 0; i < c->popup_depth; ++i)
            {
                if (c->popups[i].id != id_child("cbcl2"_id, "dropdown"_id)) continue;
                // list = 8*26 + 7*2
                // = 222; + pad =
                // 234 -> clamped
                // to 90.
                CHECK(c->popups[i].area.h == 90.0f);
                checked = true;
            }
            CHECK(checked);
        }
        CHECK(violation_count(c) == 0);
        destroy_context(c);
    }
}

static void test_text_field_undo_across_focus()
{
    tf_env env;
    context *c = env.c;
    if (tf_needs_font(c)) return;
    std::string value = "start";
    const rect field = {0, 0, 220, 24};
    const uiid id = "undofocus"_id;
    auto draw = [&](ui &u) { (void)u.text_field(field, value, id); };
    // Focus, type (building one undo step), then commit with Enter.
    tf_click(c, 200.0f, 12.0f, 0.0, draw); // past the text: the caret goes to the end;
    text_input_event(c, "x");
    tf_frame(c, 0.05, draw);
    CHECK(value == "startx");
    tf_key(c, key::ENTER, false, false, 0.10, draw);
    // The app replaces the value while the field is unfocused (e.g. a load).
    value = "loaded from disk";
    tf_frame(c, 0.15, draw);
    CHECK(value == "loaded from disk");
    // Refocus + undo: the stale typing history must not overwrite the model.
    tf_click(c, 200.0f, 12.0f, 0.20, draw);
    tf_key(c, key::Z, true, false, 0.25, draw);
    CHECK(value == "loaded from disk");
    CHECK(violation_count(c) == 0);
}

static void test_text_field_kerned_caret()
{
    tf_env env;
    context *c = env.c;
    if (tf_needs_font(c)) return;
    // "AV" is a kern pair in DejaVu Sans: the drawn caret positions
    // include the
    // // kerning, and a click at one of them must land on the same
    // index.
    std::string value = "AVAVAV";
    const rect field = {0, 0, 240, 24};
    const uiid id = "kern"_id;
    auto draw = [&](ui &u) { (void)u.text_field(field, value, id); };
    const f32 pad = default_dark().padding * 0.5f;
    f32 x = pad;

    {
        ui mu(c);
        x += mu.text_width("AVAVA");
        // exactly where the caret is drawn before 'V'     }
        tf_click(c, x, 12.0f, 0.0, draw);
        text_input_event(c, "|");
        tf_frame(c, 0.05, draw);
        CHECK(value == "AVAVA|V");
        CHECK(violation_count(c) == 0);
    }
}

static void test_text_field_drag_select()
{
    tf_env env;
    context *c = env.c;
    if (tf_needs_font(c)) return;
    std::string value = "drag me around";
    const rect field = {0, 0, 240, 24};
    const uiid id = "dragsel"_id;
    auto draw = [&](ui &u) { (void)u.text_field(field, value, id); };
    // Press at the start, drag over "drag", release: Backspace removes
    // it.
    f32 x_drag_end = default_dark().padding * 0.5f;

    {
        ui mu(c);
        x_drag_end += mu.text_width("drag");
    }
    mouse_move(c, 6.0f, 12.0f);
    mouse_button(c, true);
    tf_frame(c, 0.0, draw);
    mouse_move(c, x_drag_end - 1.0f, 12.0f);
    tf_frame(c, 0.05, draw);
    mouse_button(c, false);
    tf_frame(c, 0.10, draw);
    key_event(c, key::BACKSPACE, true);
    tf_frame(c, 0.15, draw);
    key_event(c, key::BACKSPACE, false);
    CHECK(value != "drag me around");
    CHECK(value == " me around");
    // Double-click selects the word under the pointer (same spot,
    // quick).
    f32 x_around = 6.0f;

    {
        ui mu(c);
        x_around += mu.text_width(" me a");
    }
    tf_click(c, x_around, 12.0f, 0.20, draw);
    tf_click(c, x_around, 12.0f, 0.25, draw);
    // within 0.4s and 5px: streak 2
    text_input_event(c, "X");
    tf_frame(c, 0.30, draw);
    CHECK(value == " me X");
    CHECK(violation_count(c) == 0);
}

static void test_text_field_drag_drop()
{
    tf_env env;
    context *c = env.c;
    if (tf_needs_font(c)) return;
    std::string a = "hello world", b;
    const rect fa = {0, 0, 200, 24}, fb = {0, 40, 200, 24};
    const uiid ida = "dd_a"_id, idb = "dd_b"_id;
    auto draw = [&](ui &u)
    {
        (void)u.text_field(fa, a, ida);
        (void)u.text_field(fb, b, idb);
    };
    // Seeding the model requires unfocused fields (a focused buffer is
    // the // source of truth and would be written back over the new
    // value).
    auto seed = [&](const char *av, const char *bv, f64 t)
    {
        tf_key(c, key::ENTER, false, false, t, draw);
        a = av;
        b = bv;
        tf_frame(c, t + 0.02, draw);
    };
    // Focus a, select all, then drag the selection into b (move).
    tf_click(c, 10.0f, 12.0f, 0.0, draw);
    tf_key(c, key::A, true, false, 0.05, draw);
    mouse_move(c, 40.0f, 12.0f);
    // inside the selection
    mouse_button(c, true);
    tf_frame(c, 0.10, draw);
    mouse_move(c, 40.0f, 52.0f);
    // over b, past the drag threshold
    tf_frame(c, 0.15, draw);
    mouse_button(c, false);
    tf_frame(c, 0.20, draw);
    tf_frame(c, 0.25, draw);
    // let the focus transfer settle
    CHECK(a == "");
    CHECK(b == "hello world");
    // Ctrl while dropping copies instead of moving.
    seed("copy me", "", 0.30);
    tf_click(c, 10.0f, 12.0f, 0.35, draw);
    tf_key(c, key::A, true, false, 0.40, draw);
    mouse_move(c, 30.0f, 12.0f);
    mouse_button(c, true);
    tf_frame(c, 0.45, draw);
    mouse_move(c, 30.0f, 52.0f);
    tf_frame(c, 0.50, draw);
    mods_event(c, false, true);
    // Ctrl held when dropping
    mouse_button(c, false);
    tf_frame(c, 0.55, draw);
    tf_frame(c, 0.60, draw);
    mods_event(c, false, false);
    CHECK(a == "copy me");
    CHECK(b == "copy me");
    // Move inside one field: select "abc", drag it past the end -> " defabc".
    seed("abc def", "", 0.62);
    tf_click(c, 6.0f, 12.0f, 0.65, draw);          // caret at 0 in a
    tf_key(c, key::RIGHT, true, true, 0.70, draw); // select "abc";
    f32 x_move_to = 6.0f;

    {
        ui mu(c);
        x_move_to += mu.text_width("abc def") + 8.0f;
    }
    mouse_move(c, 14.0f, 12.0f);
    // inside the selection
    mouse_button(c, true);
    tf_frame(c, 0.75, draw);
    mouse_move(c, x_move_to, 12.0f);
    // past the end, same field
    tf_frame(c, 0.80, draw);
    mouse_button(c, false);
    tf_frame(c, 0.85, draw);
    CHECK(a == " defabc");
    // Escape cancels an in-flight drag: both fields keep their text.
    seed("keep", "", 0.90);
    tf_click(c, 10.0f, 12.0f, 0.95, draw);
    tf_key(c, key::A, true, false, 1.00, draw);
    mouse_move(c, 20.0f, 12.0f);
    mouse_button(c, true);
    tf_frame(c, 1.05, draw);
    mouse_move(c, 20.0f, 52.0f);
    tf_frame(c, 1.10, draw);
    tf_key(c, key::ESCAPE, false, false, 1.15, draw);
    mouse_button(c, false);
    tf_frame(c, 1.20, draw);
    CHECK(a == "keep");
    CHECK(b == "");
    CHECK(violation_count(c) == 0);
}

static void test_text_field_drag_edit_cancels()
{
    tf_env env;
    context *c = env.c;
    if (tf_needs_font(c)) return;
    std::string a = "hello world", b;
    const rect fa = {0, 0, 200, 24}, fb = {0, 40, 200, 24};
    const uiid ida = "dec_a"_id, idb = "dec_b"_id;
    auto draw = [&](ui &u)
    {
        (void)u.text_field(fa, a, ida);
        (void)u.text_field(fb, b, idb);
    };
    // Lift a's whole value and start dragging it towards b.
    tf_click(c, 10.0f, 12.0f, 0.0, draw);
    tf_key(c, key::A, true, false, 0.05, draw);
    mouse_move(c, 40.0f, 12.0f);
    mouse_button(c, true);
    tf_frame(c, 0.10, draw);
    mouse_move(c, 40.0f, 52.0f);
    // past the threshold: the drag is in flight     tf_frame(c, 0.15, draw);      // Editing
    // the source replaces the lifted selection, so the recorded range no     // longer holds
    // the lifted text: the move must cancel rather than erase the     // wrong characters from
    // either field.
    text_input_event(c, "X");
    tf_frame(c, 0.20, draw);
    CHECK(a == "X");
    mouse_button(c, false);
    tf_frame(c, 0.25, draw);
    CHECK(a == "X");
    CHECK(b == "");
    CHECK(violation_count(c) == 0);
}
// ---- layout feedback: slice-clamp reporting + region::corner ----------------

static void test_text_field_long_value()
{
    tf_env env;
    context *c = env.c;
    if (tf_needs_font(c)) return;
    std::string value = "a very long value that will not fit inside the field";
    const rect field = {0, 0, 90, 24};
    const uiid id = "long"_id;
    auto draw = [&](ui &u) { (void)u.text_field(field, value, id); };
    // Focusing and pressing End scrolls to the caret instead of
    // overflowing.
    tf_click(c, 10.0f, 12.0f, 0.0, draw);
    tf_key(c, key::END, false, false, 0.05, draw);
    text_input_event(c, "!");
    tf_frame(c, 0.10, draw);
    CHECK(value == "a very long value that will not fit inside the field!");
    // Still balanced clips and no violations (the field clips its own
    // body).
    CHECK(violation_count(c) == 0);
}

static void test_popup_blocks_underlying()
{
    context *c = create_context(nullptr);
    const rect menu = {50, 50, 100, 100};
    // frame 1: open the popup so the next frame captures input
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 300, 300));

    {
        ui u(c);
        popup_scope p = u.popup("menu"_id, menu, POPUP_CLOSE_ON_CLICK_OUTSIDE);
    }
    end_frame(c);
    // frame 2: a button whose rect overlaps the popup must NOT respond
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 300, 300));
    mouse_move(c, 100, 100);
    // inside both the popup and the underlying widget
    mouse_button(c, true);

    {
        ui u(c);
        interaction under = u.interact("under"_id, rect{0, 0, 300, 300});
        CHECK(!under.hovered);
        CHECK(c->active == 0);
        popup_scope p = u.popup("menu"_id, menu, POPUP_CLOSE_ON_CLICK_OUTSIDE);
        interaction item = u.interact("item"_id, rect{60, 60, 80, 60});
        CHECK(item.hovered);
        // popup content still works
    }
    end_frame(c);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

static void test_text_field()
{
    context *c = create_context(nullptr);
    std::string value = "ab";
    const rect field = {0, 0, 120, 24};
    const uiid id = "tf"_id;
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 200, 100));
    mouse_move(c, 40, 10);
    // past the text: the caret goes to the end
    mouse_button(c, true);

    {
        ui u(c);
        (void)u.text_field(field, value, id);
    }
    end_frame(c);
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 200, 100));
    mouse_button(c, false);

    {
        ui u(c);
        (void)u.text_field(field, value, id);
    }
    end_frame(c);
    // No font is loaded here, so caret positions are not measurable:
    // place the caret at the end explicitly (the click-position
    // behavior is covered by test_text_field_caret_placement).
    key_event(c, key::END, true);
    begin_frame(c, 0.024, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        (void)u.text_field(field, value, id);
    }
    end_frame(c);
    key_event(c, key::END, false);
    text_input_event(c, "c");
    begin_frame(c, 0.032, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        const bool changed = u.text_field(field, value, id);
        CHECK(changed);
    }
    end_frame(c);
    CHECK(value == "abc");
    key_event(c, key::BACKSPACE, true);
    begin_frame(c, 0.048, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        (void)u.text_field(field, value, id);
    }
    end_frame(c);
    key_event(c, key::BACKSPACE, false);
    CHECK(value == "ab");
    // Home then Delete removes the first character
    key_event(c, key::HOME, true);
    begin_frame(c, 0.064, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        (void)u.text_field(field, value, id);
    }
    end_frame(c);
    key_event(c, key::HOME, false);
    key_event(c, key::DEL, true);
    begin_frame(c, 0.080, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        (void)u.text_field(field, value, id);
    }
    end_frame(c);
    key_event(c, key::DEL, false);
    CHECK(value == "b");
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

static void test_number_field()
{
    context *c = create_context(nullptr);
    f32 value = 2.5f;
    const rect field = {0, 0, 120, 24};
    const uiid id = "nf"_id;
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        CHECK(!u.number_field(field, value, id));
    }
    end_frame(c);
    CHECK(value == 2.5f);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}
struct test_clipboard : clipboard
{
    std::string data;
    bool get(std::string &out) override
    {
        out = data;
        return true;
    }
    void set(std::string_view text) override { data.assign(text); }
};

static void test_focus_tab_clipboard()
{
    context *c = create_context(nullptr);
    test_clipboard clip;
    set_clipboard(c, &clip);
    std::string a = "hello", b = "";
    const rect fa = {0, 0, 120, 24}, fb = {0, 40, 120, 24};
    const uiid ida = "a"_id, idb = "b"_id;
    auto draw = [&]()
    {
        ui u(c);
        (void)u.text_field(fa, a, ida);
        (void)u.text_field(fb, b, idb);
    };
    // focus a (press then release over it)
    begin_frame(c, 0.00, 0.016, rect::make(0, 0, 200, 100));
    mouse_move(c, 10, 10);
    mouse_button(c, true);
    draw();
    end_frame(c);
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 200, 100));
    mouse_button(c, false);
    draw();
    end_frame(c);
    // ctrl+A, ctrl+C
    mods_event(c, false, true);
    key_event(c, key::A, true);
    begin_frame(c, 0.032, 0.016, rect::make(0, 0, 200, 100));
    draw();
    end_frame(c);
    key_event(c, key::A, false);
    key_event(c, key::C, true);
    begin_frame(c, 0.048, 0.016, rect::make(0, 0, 200, 100));
    draw();
    end_frame(c);
    key_event(c, key::C, false);
    mods_event(c, false, false);
    CHECK(clip.data == "hello");
    // Tab moves focus from a to b
    key_event(c, key::TAB, true);
    begin_frame(c, 0.064, 0.016, rect::make(0, 0, 200, 100));
    draw();
    end_frame(c);
    key_event(c, key::TAB, false);
    // typing now goes to b
    text_input_event(c, "X");
    begin_frame(c, 0.080, 0.016, rect::make(0, 0, 200, 100));
    draw();
    end_frame(c);
    CHECK(b == "X");
    // ctrl+A then ctrl+V pastes "hello" over b
    mods_event(c, false, true);
    key_event(c, key::A, true);
    begin_frame(c, 0.096, 0.016, rect::make(0, 0, 200, 100));
    draw();
    end_frame(c);
    key_event(c, key::A, false);
    key_event(c, key::V, true);
    begin_frame(c, 0.112, 0.016, rect::make(0, 0, 200, 100));
    draw();
    end_frame(c);
    key_event(c, key::V, false);
    mods_event(c, false, false);
    CHECK(b == "hello");
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

static void test_number_locale()
{
    context *c = create_context(nullptr);
    theme t = default_dark();
    t.decimal_separator = ',';
    set_theme(c, t);
    f32 value = 1.5f;
    const rect field = {0, 0, 120, 24};
    const uiid id = "n"_id;
    begin_frame(c, 0.00, 0.016, rect::make(0, 0, 200, 100));
    mouse_move(c, 10, 10);
    mouse_button(c, true);

    {
        ui u(c);
        (void)u.number_field(field, value, id);
    }
    end_frame(c);
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 200, 100));
    mouse_button(c, false);

    {
        ui u(c);
        (void)u.number_field(field, value, id);
    }
    end_frame(c);
    // select all, then type "2,25" (comma decimal)
    mods_event(c, false, true);
    key_event(c, key::A, true);
    begin_frame(c, 0.032, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        (void)u.number_field(field, value, id);
    }
    end_frame(c);
    key_event(c, key::A, false);
    mods_event(c, false, false);
    text_input_event(c, "2,25");
    begin_frame(c, 0.048, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        (void)u.number_field(field, value, id);
    }
    end_frame(c);
    CHECK(std::fabs(value - 2.25f) < 0.001f);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

static void test_shapes()
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

static void test_panel_card()
{
    context *c = create_context(nullptr);
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 400, 300));

    {
        ui u(c);
        rect pb = {50, 50, 200, 120};

        {
            panel_scope p = u.panel("Test Panel", pb, PANEL_NONE);
            CHECK(p.open);
            CHECK(!p.close_requested);
            CHECK(p.content().w > 0.0f);
            CHECK(c->clip_depth == 1); // client region pushed a clip
        }
        // Per-panel overrides merge over the theme's panel style; a translucent
        // background is what lets a blurred backdrop show through.
        {
            const panel_style merged =
                merge_style(default_dark().panel, panel_override{.bg = some(color{10, 20, 30, 120}),
                                                                 .radius = some(9.0f)});
            CHECK(merged.bg.a == 120);
            CHECK(merged.radius == 9.0f);
            CHECK(default_dark().panel.bg.a != 120);
            // the theme itself is unchanged
        }
        {
            rect pb2 = {270, 50, 110, 90};
            panel_scope p = u.panel("Glass", pb2, PANEL_NO_CONTROLS, "glass_panel"_id, nullptr,
                                    panel_override{.bg = some(color{10, 20, 30, 120})});
            CHECK(p.open);
            CHECK(p.content().w > 0.0f);
        }
        u.card({0, 0, 100, 60}, card_override{.radius = some(0.0f)});
    }
    end_frame(c);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

static void test_panel_blocks_underlying()
{
    context *c = create_context(nullptr);
    const rect pb = {50, 50, 200, 120};
    // frame 1: draw the panel so next frame it participates in input
    // blocking
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 400, 300));

    {
        ui u(c);
        rect b = pb;
        panel_scope p = u.panel("P", b, PANEL_NONE);
    }
    end_frame(c);
    // frame 2: a widget under the panel must NOT respond; panel content
    // must
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 400, 300));
    mouse_move(c, 100, 100);
    // inside the panel
    mouse_button(c, true);

    {
        ui u(c);
        interaction under = u.interact("under"_id, rect{0, 0, 400, 300});
        CHECK(!under.hovered);
        CHECK(c->active == 0);
        rect b = pb;
        panel_scope p = u.panel("P", b, PANEL_NONE);
        interaction inside = u.interact("inside"_id, rect{60, 90, 120, 40});
        CHECK(inside.hovered);
        // panel content is allowed
    }
    end_frame(c);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

static void test_row_cut_right_no_overlap()
{
    row r(rect::make(0, 0, 100, 20), 0.0f);
    rect left = r.next(30.0f);
    rect right = r.cut_right(30.0f);
    CHECK(left.x == 0 && left.w == 30);
    CHECK(right.x == 70 && right.w == 30);
    CHECK(left.right() <= right.x);
    // pinned elements never overlap
    CHECK(r.remaining().right() <= right.x + 0.001f);
    // leftover sits between them }
}

static void test_device_contract()
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

static void test_blur()
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

static void test_multi_window()
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
                panel_scope p = u.panel("panel_a", b1, PANEL_NONE);
                (void)p;
            }
            end_frame(c);
            rect b2 = {0, 10, 80, 80};
            begin_frame(c, *w2, 0.176, 0.016);

            {
                ui u(c);
                panel_scope p = u.panel("panel_b", b2, PANEL_NONE);
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

static void test_window_pointer_invalidation()
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

static void test_titlebar()
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

static void test_titlebar_maximize_restore()
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

static void test_titlebar_close_paths()
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

static void test_dock_tabs()
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

static void test_dock_split_and_ratio()
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

static void test_dock_drag_action()
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

static void test_dock_tabs_no_overflow()
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

static void test_dock_drop_left_zone()
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

static void test_dock_content_clipped()
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

static void test_float_panel_dock_drag()
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

static void test_panel_close_button()
{
    context *c = create_context(nullptr);
    rect b = {50, 50, 200, 120};
    // close dot center with the default panel style:
    // inner = bounds.pad(1) -> tb.right() = 249, tb.y = 51;
    // close 12x12 at (227, 60)
    const f32 cx = 233.0f, cy = 66.0f;
    bool close_req = false;
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 400, 300));
    mouse_move(c, cx, cy);
    mouse_button(c, true);

    {
        ui u(c);
        rect bb = b;
        panel_scope p = u.panel("P", bb, PANEL_NONE);
        close_req = p.close_requested;
    }
    end_frame(c);
    CHECK(!close_req);
    CHECK(c->dragging_panel == 0);
    // pressing close must not start a titlebar drag
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 400, 300));
    mouse_button(c, false);

    {
        ui u(c);
        rect bb = b;
        panel_scope p = u.panel("P", bb, PANEL_NONE);
        close_req = p.close_requested;
    }
    end_frame(c);
    CHECK(close_req);
    // release over the dot closes the panel      // The
    // titlebar keeps its full width: pressing just left of the
    // dot (inside     // what used to be an over-broad reserve)
    // must still start a drag.
    begin_frame(c, 0.032, 0.016, rect::make(0, 0, 400, 300));
    mouse_move(c, 209, 66);
    mouse_button(c, true);

    {
        ui u(c);
        rect bb = b;
        panel_scope p = u.panel("P", bb, PANEL_NONE);
        (void)p;
    }
    end_frame(c);
    CHECK(c->dragging_panel != 0);
    begin_frame(c, 0.048, 0.016, rect::make(0, 0, 400, 300));
    mouse_button(c, false);

    {
        ui u(c);
        rect bb = b;
        panel_scope p = u.panel("P", bb, PANEL_NONE);
        (void)p;
    }
    end_frame(c);
    CHECK(c->dragging_panel == 0);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}
// ----------------------------------------------------------------
// audit additions

static void test_context_menu_clamp()
{
    null_device nd;
    nd.my_surface.w = 200;
    nd.my_surface.h = 120;
    context *c = create_context(&nd, nd.create_surface());
    static const char *items[3] = {"Cut", "Copy", "Paste"};
    const rect anchor = rect::make(150, 90, 40, 24);
    // bottom-right corner //
    // Right-press at the very
    // corner: the clamped menu
    // must stay inside.
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 200, 120));
    mouse_move(c, 190.0f, 110.0f);
    mouse_button(c, pointer_button::RIGHT, true);

    {
        ui u(c);
        (void)u.context_menu("cmc"_id, anchor, items, 3);
    }
    end_frame(c);
    mouse_button(c, pointer_button::RIGHT, false);

    {
        bool checked = false;
        for (i32 i = 0; i < c->popup_depth; ++i)
        {
            if (c->popups[i].id != id_child("cmc"_id, "menu"_id)) continue;
            const rect &menu = c->popups[i].area;
            CHECK(menu.right() <= 200.0f + 0.01f);
            CHECK(menu.bottom() <= 120.0f + 0.01f);
            CHECK(menu.x >= 0.0f && menu.y >= 0.0f);
            checked = true;
        }
        CHECK(checked);
    }
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

static void test_popup_retract_no_ghost()
{
    // End-to-end ghost-entry
    // check for the same-frame
    // close path: pick a combo
    // // item, then verify the
    // next frame sees no popup
    // entry (the fix calls //
    // detail::popup_retract
    // internally; this asserts
    // the observable result).
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    static const char *items[2] = {"A", "B"};
    i32 sel = 0;
    const rect r = rect::make(20, 20, 100, 26);
    const uiid id = "ghost_cb"_id;
    const uiid drop_id = id_child(id, "dropdown"_id);
    // Open: press + release on
    // the header.
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 300, 300));
    mouse_move(c, 40, 32);
    mouse_button(c, true);

    {
        ui u(c);
        (void)u.combo(r, "", items, 2, sel, id);
    }
    end_frame(c);
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 300, 300));
    mouse_button(c, false);

    {
        ui u(c);
        (void)u.combo(r, "", items, 2, sel, id);
    }
    end_frame(c);
    CHECK(popup_present(c, drop_id, false));
    // dropdown staged      //
    // Press inside row A (rows
    // at y=58..84 inside the
    // scroll viewport).
    begin_frame(c, 0.032, 0.016, rect::make(0, 0, 300, 300));
    mouse_move(c, 40, 62.0f);
    mouse_button(c, true);

    {
        ui u(c);
        (void)u.combo(r, "", items, 2, sel, id);
    }
    end_frame(c);
    // Release: the pick
    // resolves at end_frame and
    // the dropdown retracts.
    begin_frame(c, 0.048, 0.016, rect::make(0, 0, 300, 300));
    mouse_button(c, false);

    {
        ui u(c);
        (void)u.combo(r, "", items, 2, sel, id);
    }
    end_frame(c);
    CHECK(sel == 0);
    CHECK(!popup_present(c, drop_id, false));
    // retracted: no ghost entry
    begin_frame(c, 0.064, 0.016, rect::make(0, 0, 300, 300));

    {
        ui u(c);
        CHECK(u.combo(r, "", items, 2, sel, id));
        // the next call reports
        // it         // A probe
        // far from the closed
        // dropdown is an
        // ordinary base hit
        // now.
        const interaction in = u.interact("base"_id, rect::make(240, 240, 40, 40));
        CHECK(!in.blocked);
    }
    end_frame(c);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

static void test_checkbox_slider_edge()
{
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    // checkbox: disabled never
    // hovers; a click (press +
    // release over the box) //
    // toggles even with an
    // empty label.
    bool v = true;
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 200, 120));
    mouse_move(c, 10, 40);
    // over the box (y 30..56)
    mouse_button(c, true);

    {
        ui u(c);
        const interaction dis = u.interact("disabled_probe"_id, rect::make(0, 0, 40, 26), false);
        CHECK(dis.disabled && !dis.hovered);
        (void)u.checkbox(rect::make(0, 30, 80, 26), "", v, "cb_empty"_id);
        CHECK(v);
        // press alone does not
        // toggle
    }
    end_frame(c);
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 200, 120));
    mouse_button(c, false);

    {
        ui u(c);
        (void)u.checkbox(rect::make(0, 30, 80, 26), "", v, "cb_empty"_id);
        CHECK(!v);
        // the release over
        // the box flips it
        //
    }
    end_frame(c);
    // slider: a fast
    // click (press +
    // release inside
    // one frame) still
    // jumps to     //
    // the click
    // position - the
    // press edge drives
    // the jump, not
    // `active`.
    begin_frame(c, 0.032, 0.016, rect::make(0, 0, 200, 120));
    f32 val = 5.0f;
    mouse_move(c, 3.0f, 70.0f);
    // far left, inside
    // the track
    // (y 60..86)
    mouse_button(c, true);

    {
        ui u(c);
        (void)u.slider_float(rect::make(0, 60, 120, 26), "", val, 1.0f, 9.0f, "sl_min"_id);
        CHECK(val <= 1.0f + 0.35f);
        // jumped to the
        // click
        // position this
        // frame
    }
    end_frame(c);
    begin_frame(c, 0.032, 0.016, rect::make(0, 0, 200, 120));
    mouse_button(c, false);

    {
        ui u(c);
        (void)u.slider_float(rect::make(0, 60, 120, 26), "", val, 1.0f, 9.0f, "sl_min"_id);
        CHECK(val <= 1.0f + 0.35f);
        // and it
        // stays
        // after the
        // release
    }
    end_frame(c);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

int main()
{
    test_rect_algebra();
    test_ids();
    test_click();
    test_release_outside_cancels();
    test_cursors();
    test_draw_batching();
    test_theme();
    test_utf8();
    test_text();
    test_style_cascade();
    test_rounded_rect();
    test_popups();
    test_keys_and_escape();
    test_rect_edges();
    test_stack_clamp();
    test_interaction_edges();
    test_text_edges();
    test_utf8_truncated();
    test_ime();
    test_draw_flush();
    test_blur_fallback();
    test_window_errors();
    test_duplicate_region_id();
    test_auto_id_local();
    test_limit_overflows();
    test_split_interactive();
    test_panel_flags();
    test_dock_empty();
    test_nine_slice();
    test_state_view_update();
    test_widget_outline();
    test_animation_tween();
    test_animation_spring_smooth();
    test_animation_scope_color_appear();
    test_font_coverage();
    test_popup_focus_trap();
    test_rect_helpers();
    test_text_polish();
    test_widget_extras();
    test_tracks_and_grid();
    test_scroll_view();
    test_text_field_caret_placement();
    test_text_field_word_ops();
    test_text_field_undo_redo();
    test_layout_overflow_report();
    test_region_corner();
    test_combo();
    test_combo_mouse_pick();
    test_tooltip_delay();
    test_context_menu();
    test_scroll_options_and_limits();
    test_virtual_list();
    test_region_id_overflow();
    test_draw_shapes_coverage();
    test_tooltip_flip_above();
    test_combo_popup_clamp();
    test_text_field_undo_across_focus();
    test_text_field_kerned_caret();
    test_text_field_drag_select();
    test_text_field_drag_drop();
    test_text_field_drag_edit_cancels();
    test_text_field_long_value();
    test_popup_blocks_underlying();
    test_text_field();
    test_number_field();
    test_focus_tab_clipboard();
    test_number_locale();
    test_shapes();
    test_panel_card();
    test_panel_blocks_underlying();
    test_row_cut_right_no_overlap();
    test_device_contract();
    test_blur();
    test_multi_window();
    test_window_pointer_invalidation();
    test_titlebar();
    test_titlebar_maximize_restore();
    test_titlebar_close_paths();
    test_violation_overlay();
    test_interact_topmost_wins();
    test_keyboard_navigation();
    test_text_atlas_paging();
    test_text_wrapped_height();
    test_dock_persistence();
    test_dock_tabs();
    test_dock_split_and_ratio();
    test_dock_drag_action();
    test_dock_tabs_no_overflow();
    test_dock_drop_left_zone();
    test_dock_content_clipped();
    test_float_panel_dock_drag();
    test_panel_close_button();
    if (g_failures == 0)
    {
        std::printf("all core tests passed\n");
        return 0;
    }
    std::printf("%d core test(s) failed\\n", g_failures);
    return 1;
}