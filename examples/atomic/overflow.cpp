// overflow — seeing (and silencing) layout-overflow reports.
//
// Shows: `set_report_layout_overflow(true)` makes every clamped
// `column::next`/`row::next` (and track/grid slice) report a non-fatal violation
// naming the requested and granted sizes, so "my list is cut off" becomes a
// testable event instead of a screenshot review. Also shows `region::corner` for
// corner-pinned labels that can never collide with each other.
//
//   pui_ex_overflow          (reports printed to stderr, as default)
//   pui_ex_overflow --selftest
#include "../example_common.h"

static bool g_report = true;
static bool g_narrow = false;
static i32 g_seen = 0;
static char g_last[128] = "(none yet)";

// The suite-style capture: the default handler prints to stderr; this one keeps
// the last message so the UI can show what was reported this frame.
static void capture_report(void *, const char * /*condition*/, const char *message,
                           const char * /*file*/, i32 /*line*/)
{
    g_seen += 1;
    std::snprintf(g_last, sizeof(g_last), "%s", message ? message : "");
}

static void overflow_frame(ui &u, example_app &app)
{
    const theme &th = u.th();

    // The example enables reporting and captures the message for display; with
    // the flag off the last line stays "(none yet)".
    set_violation_handler(app.ctx, capture_report, nullptr);
    set_report_layout_overflow(app.ctx, g_report);

    example_page page = example_begin_page(u, app, "Overflow feedback", "clamped slices, reported");
    scroll_view page_scroll = u.scroll(page.content, "ov_page"_id);
    // Section heights are known up front, so pre-set the scroll extent: the very
    // first frame is then correct (see the scroll chapter in the README).
    // Section heights sized to their content (verified by this example's own
    // reporter: if these constants are wrong, the orange line tells you).
    constexpr f32 H_REPORTER = 135.0f, H_DEMO = 226.0f, H_CORNER = 160.0f;
    page_scroll.set_content_height(H_REPORTER + H_DEMO + H_CORNER + 2.0f * example_ui::SECTION_GAP);
    column col(page_scroll.content(), example_ui::SECTION_GAP);

    {
        rect body =
            example_section(u, col.next(H_REPORTER), "Reporter",
                            "non-fatal violations for slices that did not fit", app.font_bold);
        column c(body, 8.0f);
        {
            row r(c.next(26.0f), 18.0f);
            (void)u.checkbox(r.next(170.0f), "Report overflow", g_report, "ov_report"_id);
            (void)u.checkbox(r.next(150.0f), "Make it overflow", g_narrow, "ov_narrow"_id);
            u.textf(r.remaining(), th.text_dim, ALIGN_RIGHT, "reports so far: %d", g_seen);
        }
        // The last report of the previous frame, so the nothing-fits case is legible.
        u.text(c.next(18.0f), g_last, g_seen > 0 ? color{230, 130, 90, 255} : th.text_dim,
               ALIGN_LEFT);
    }

    // The buggy-by-intent layout: a column whose rows do not fit when narrowed.
    {
        rect body =
            example_section(u, col.next(H_DEMO), g_narrow ? "Deliberately too small" : "Fits",
                            g_narrow ? "the rows are requested taller than the box"
                                     : "everything the rows asked for was granted",
                            app.font_bold);
        column c(body, 8.0f);
        // The toggle is real: a tall box fits all four 20px rows (98 + 16 padding
        // = 114); a short one (60px) cannot, so rows clamp and get reported.
        rect box = c.next(g_narrow ? 60.0f : 114.0f);
        u.draw_rect(box, th.panel_bg);
        {
            // Each row asks for its natural size; with the "too small" toggle the
            // rows are clamped and reported (see the orange line above + stderr).
            column rows(box.pad(8.0f), 6.0f);
            static const char *labels[4] = {"alpha", "beta", "gamma", "delta"};
            for (i32 i = 0; i < 4; ++i)
            {
                const rect r = rows.next(20.0f);
                u.draw_rounded_rect(r, th.widget_bg, 5.0f);
                u.text(r.pad(10.0f, 0.0f), labels[i], th.text, ALIGN_LEFT);
            }
        }
        example_caption(u, c.next(18.0f),
                        g_narrow ? "row slices were clamped (see the report and stderr)"
                                 : "all rows got their requested height - no reports");
    }

    // region::corner: badges that cannot collide, whatever the box size.
    {
        rect body = example_section(u, col.next(H_CORNER), "region::corner",
                                    "corner-pinned badges stay clear of each other", app.font_bold);
        rect box = body;
        region demo(u, "ov_corner"_id, box);
        u.draw_rect(demo.area(), th.panel_bg);
        u.text(demo.area().pad(8.0f), "resize me - the badges follow the corners", th.text_dim,
               ALIGN_LEFT);
        for (i32 i = 0; i < 4; ++i)
        {
            const rect b = demo.corner(i, 84.0f, 24.0f, 8.0f);
            u.draw_rounded_rect(b, i == 0 ? th.accent : th.widget_hover, 5.0f);
            u.textf(b, i == 0 ? th.bg : th.text, ALIGN_CENTER, "corner(%d)", i);
        }
    }
}

int main(int argc, char **argv)
{
    g_seen = 0;
    return example_run("overflow", 640, 480, argc, argv, overflow_frame);
}
