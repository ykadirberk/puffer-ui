// rect — pure rect-cutting algebra.
//
// Shows: `cut_top/bottom/left/right` (mutating), `cut_*_ratio`, non-mutating
// `*_slice`, `align_*`, `fit_aspect`, `pad`, `intersect`, `contains`, and
// `region::corner` (corner-pinned badges that stay clear of each other).
//
//   pui_ex_rect
#include "../example_common.h"

static void rect_frame(ui &u, example_app &app)
{
    const theme &th = u.th();
    example_page page =
        example_begin_page(u, app, "Rect algebra", "cuts, ratio cuts, slices, align and fit");
    column col(page.content, example_ui::SECTION_GAP);

    // --- mutating cuts: each cut shortens the source and returns the slice ----
    {
        const rect body =
            example_section(u, col.next(200.0f), "Mutating cuts",
                            "cut_top/bottom/left/right shorten the source", app.font_bold);
        column c(body, 8.0f);

        rect area = c.next(72.0f);
        const rect top = area.cut_top(24.0f);
        const rect bottom = area.cut_bottom(18.0f);
        const rect left = area.cut_left(96.0f);
        const rect right = area.cut_right(96.0f);
        u.draw_rect(top, th.widget_bg);
        u.draw_rect(bottom, th.widget_bg);
        u.draw_rect(left, th.widget_bg);
        u.draw_rect(right, th.widget_bg);
        u.draw_rect(area, th.accent);
        u.text(top.pad(8.0f, 0.0f), "cut_top(24)", th.text, ALIGN_LEFT);
        u.text(bottom.pad(8.0f, 0.0f), "cut_bottom(18)", th.text, ALIGN_LEFT);
        u.text(left, "cut_left(96)", th.text, ALIGN_CENTER);
        u.text(right, "cut_right(96)", th.text, ALIGN_CENTER);
        u.text(area, "remaining", th.bg, ALIGN_CENTER);

        // ratio cuts are the same, but relative to the current size.
        rect ratio_area = c.next(44.0f);
        const rect header = ratio_area.cut_top_ratio(0.30f);
        u.draw_rect(header, th.widget_hover);
        u.text(header.pad(8.0f, 0.0f), "cut_top_ratio(0.30)", th.text, ALIGN_LEFT);
        u.draw_rect(ratio_area, th.widget_bg);
        u.text(ratio_area.pad(8.0f, 0.0f), "the remaining 70%", th.text_dim, ALIGN_LEFT);
    }

    // --- slices, align/fit and queries never mutate the source ---------------
    {
        // 100 px demo box + 2 x 20px badge rows + the query line.
        const rect body = example_section(
            u, col.next(214.0f), "Non-mutating slices and queries",
            "slices, align/fit and intersect read without cutting the source", app.font_bold);
        column c(body, 8.0f);

        // The demo box gets its own region so the corner badges can pin to it
        // without hand math (region::corner keeps them clear of each other).
        rect area = c.next(100.0f);
        region demo(u, "rect_demo"_id, area);
        u.draw_rect(demo.area(), th.widget_bg);

        // The slice strip is at the top edge: a corner badge would collide with
        // it, so the slice demo draws the strip and the badges use the bottom
        // corners for their labels.
        const rect strip = demo.area().top_slice(14.0f);
        u.draw_rect(strip, th.accent);
        u.text(strip.pad(8.0f, 0.0f), "top_slice(14) - area is unchanged", th.bg, ALIGN_LEFT);
        u.text(rect::make(strip.x, strip.bottom() + 2.0f, strip.w, 16.0f),
               "the badge rows below "
               "are place by region::corner",
               th.text_dim, ALIGN_LEFT);

        // fit_aspect keeps an aspect ratio inside the area, centered.
        const rect video = demo.area().pad(8.0f, 44.0f).fit_aspect(16.0f / 9.0f);
        u.draw_rounded_rect(video, th.panel_bg, 4.0f);
        u.text(video, "fit_aspect(16:9)", th.text_dim, ALIGN_CENTER);

        // Corner-pinned badges: bottom-left and bottom-right only, so they can
        // never collide with the top strip or with each other.
        const rect bl = demo.corner(3, 74.0f, 20.0f, 6.0f);
        const rect br = demo.corner(2, 74.0f, 20.0f, 6.0f);
        u.draw_rounded_rect(bl, th.accent, 3.0f);
        u.draw_rounded_rect(br, th.accent, 3.0f);
        u.text(bl, "corner(3)", th.bg, ALIGN_CENTER);
        u.text(br, "corner(2)", th.bg, ALIGN_CENTER);

        // pad / intersect / contains are plain queries.
        const rect a = rect::make(40.0f, 0.0f, 120.0f, 40.0f);
        const rect b = rect::make(120.0f, 20.0f, 120.0f, 40.0f);
        const rect overlap = rect::intersect(a, b);
        u.textf(c.next(18.0f), th.text_dim, ALIGN_LEFT,
                "intersect(a,b) = %.0fx%.0f at (%.0f,%.0f)  |  a.contains(80,20): %s",
                static_cast<double>(overlap.w), static_cast<double>(overlap.h),
                static_cast<double>(overlap.x), static_cast<double>(overlap.y),
                a.contains(80.0f, 20.0f) ? "yes" : "no");
    }
}

int main(int argc, char **argv)
{
    return example_run("rect", 640, 560, argc, argv, rect_frame);
}
