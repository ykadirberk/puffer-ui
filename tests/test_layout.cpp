// layout: rect algebra, cursors, regions, tracks, overflow.
#include "test_util.h"

PUI_TEST(test_rect_algebra)
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

PUI_TEST(test_ids)
{
    uiid a = id_child("root"_id, "ok"_id);
    uiid b = id_child("root"_id, "ok"_id);
    uiid c = id_child("other"_id, "ok"_id);
    CHECK(a == b);
    CHECK(a != c);
}

PUI_TEST(test_cursors)
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

PUI_TEST(test_rect_edges)
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

PUI_TEST(test_stack_clamp)
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

PUI_TEST(test_duplicate_region_id)
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

PUI_TEST(test_auto_id_local)
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

PUI_TEST(test_limit_overflows)
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
    // Popups grow on demand (MAX_POPUPS is just the initial capacity): one
    // beyond it is fine, and no violation is reported.
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 100, 100));

    {
        ui u(c);
        std::optional<popup_scope> popups[MAX_POPUPS + 1];
        for (i32 i = 0; i < MAX_POPUPS + 1; ++i)
            popups[i].emplace(u, id_child("p"_id, static_cast<uiid>(i)), rect::make(0, 0, 10, 10),
                              popup_flags::NONE);
        CHECK(c->popup_depth == MAX_POPUPS + 1); // grew past the initial capacity
        CHECK(g_last_violation != nullptr);      // still the style-scope one, unchanged
        for (i32 i = MAX_POPUPS; i >= 0; --i) popups[i].reset();
    }
    end_frame(c);
    CHECK(violation_count(c) == 1); // only the style-scope overflow above
    destroy_context(c);
}

PUI_TEST(test_split_interactive)
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

PUI_TEST(test_rect_helpers)
{
    const rect r = rect::make(0, 0, 100, 50);
    CHECK(r.top_slice(10).y == 0.0f && r.top_slice(10).h == 10.0f);
    CHECK(r.bottom_slice(10).y == 40.0f && r.bottom_slice(10).h == 10.0f);
    CHECK(r.left_slice(10).x == 0.0f && r.left_slice(10).w == 10.0f);
    CHECK(r.right_slice(10).x == 90.0f && r.right_slice(10).w == 10.0f);
    CHECK(r.top_slice(1000).h == 50.0f);
    // clamped
    {
        rect ratio_src = rect::make(0, 0, 100, 100);
        CHECK(ratio_src.cut_top_ratio(0.25f).h == 25.0f);
        ratio_src = rect::make(0, 0, 100, 100);
        CHECK(ratio_src.cut_bottom_ratio(0.25f).y == 75.0f);
        ratio_src = rect::make(0, 0, 100, 100);
        CHECK(ratio_src.cut_left_ratio(0.5f).w == 50.0f);
        ratio_src = rect::make(0, 0, 100, 100);
        CHECK(ratio_src.cut_right_ratio(0.5f).x == 50.0f);
    }
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

PUI_TEST(test_tracks_and_grid)
{
    tf_env env;
    context *c = env.c;
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
}

PUI_TEST(test_layout_overflow_report)
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

PUI_TEST(test_region_corner)
{
    tf_env env;
    context *c = env.c;
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
}

PUI_TEST(test_region_id_overflow)
{
    // Region-id duplicate checking is a per-frame hash set: complete at any
    // frame scale. A frame with thousands of regions reports nothing, AND a
    // duplicate among them is still caught.
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    set_violation_handler(c, capture_violation, nullptr);
    g_violation_events = 0;

    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 200, 200));
    {
        ui u(c);
        constexpr i32 kRegions = 2100;
        for (i32 i = 0; i < kRegions; ++i)
            (void)u.region(rect::make(0, 0, 50, 50), id_child("r"_id, static_cast<uiid>(i)));
    }
    end_frame(c);
    CHECK(g_violation_events == 0); // no cap to outgrow

    // a duplicate among many regions is still caught
    g_violation_events = 0;
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 200, 200));
    {
        ui u(c);
        for (i32 i = 0; i < 2100; ++i)
            (void)u.region(rect::make(0, 0, 50, 50), id_child("q"_id, static_cast<uiid>(i)));
        (void)u.region(rect::make(0, 0, 50, 50), id_child("q"_id, 7)); // dup!
    }
    end_frame(c);
    CHECK(g_violation_events == 1);
    CHECK(violation_was("duplicate region id among siblings"));

    destroy_context(c);
}

PUI_TEST(test_row_cut_right_no_overlap)
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
