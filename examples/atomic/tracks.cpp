// tracks — sized tracks and interactive splitters.
//
// Shows: `track_size::fixed/flex/ratio/fit_content` with `min`/`max`,
// `track_row` / `track_column` cursors, and `split_*_interactive`.
//
//   pui_ex_tracks
#include "../example_common.h"

static void tracks_frame(ui &u, example_app &app)
{
    const theme &th = u.th();
    example_page page =
        example_begin_page(u, app, "Tracks", "fixed, flex, ratio and fit_content sizes");
    column col(page.content, example_ui::SECTION_GAP);

    {
        const rect body = example_section(
            u, col.next(240.0f), "Sized tracks",
            "track_row resolves fixed, flex, ratio and fit_content with min/max clamping",
            app.font_bold);
        column stack(body, 6.0f);

        example_caption(u, stack.next(16.0f), "fixed(80) | flex(2) | flex(1)");
        {
            track_row tr(stack.next(34.0f),
                         {track_size::fixed(80.0f), track_size::flex(2.0f), track_size::flex(1.0f)},
                         8.0f);
            const rect fixed_r = tr.next();
            const rect a = tr.next();
            const rect b = tr.next();
            u.draw_rounded_rect(fixed_r, th.widget_bg, 4.0f);
            u.text(fixed_r, "fixed(80)", th.text_dim, ALIGN_CENTER);
            u.draw_rounded_rect(a, th.accent, 4.0f);
            u.draw_rounded_rect(b, th.panel_bg, 4.0f);
            char label[64];
            std::snprintf(label, sizeof(label), "flex(2) = %.0f px", static_cast<double>(a.w));
            u.text(a, label, th.bg, ALIGN_CENTER);
            std::snprintf(label, sizeof(label), "flex(1) = %.0f px", static_cast<double>(b.w));
            u.text(b, label, th.text, ALIGN_CENTER);
        }

        example_caption(u, stack.next(16.0f), "ratio(0.25) | flex()");
        {
            track_row tr(stack.next(30.0f), {track_size::ratio(0.25f), track_size::flex()}, 8.0f);
            const rect q = tr.next();
            const rect rest = tr.next();
            u.draw_rect(q, th.widget_hover);
            u.text(q, "ratio(0.25)", th.text, ALIGN_CENTER);
            u.draw_rect(rest, th.widget_bg);
            u.text(rest.pad(8.0f, 0.0f), "flex() takes what is left", th.text_dim, ALIGN_LEFT);
        }

        example_caption(u, stack.next(16.0f), "fit(60) | flex().min(120).max(200) | flex()");
        {
            track_row tr(stack.next(30.0f),
                         {track_size::fit_content(60.0f),
                          track_size::flex().min(120.0f).max(200.0f), track_size::flex()},
                         8.0f);
            const rect a = tr.next();
            const rect b = tr.next();
            const rect c = tr.next();
            u.draw_rect(a, th.widget_bg);
            u.draw_rect(b, th.accent);
            u.draw_rect(c, th.panel_bg);
            u.text(a, "fit(60)", th.text, ALIGN_CENTER);
            char label[64];
            std::snprintf(label, sizeof(label), "min(120) -> %.0f", static_cast<double>(b.w));
            u.text(b, label, th.bg, ALIGN_CENTER);
            u.text(c, "flex()", th.text, ALIGN_CENTER);
        }
    }

    // track_column works top-to-bottom; splitters are interactive.
    {
        const rect body = example_section(
            u, col.next(208.0f), "Track columns and splitters",
            "track_column stacks top-to-bottom; split_horizontal_interactive drags", app.font_bold);
        static f32 side_w = 170.0f;
        const auto panes = u.split_horizontal_interactive("track_split"_id, body, &side_w, 90.0f,
                                                          260.0f, 6.0f, 6.0f);
        u.draw_rect(panes.first, th.panel_bg);
        u.text(panes.first.pad(10.0f, 0.0f), "split_horizontal_interactive", th.text, ALIGN_LEFT);
        u.draw_rect(panes.second, th.widget_bg);
        {
            track_column tc(
                panes.second.pad(8.0f),
                {track_size::fixed(20.0f), track_size::flex(), track_size::fixed(20.0f)}, 6.0f);
            u.text(tc.next(), "track_column: fixed(20)", th.text, ALIGN_LEFT);
            u.text(tc.next(), "flex() in the middle", th.text_dim, ALIGN_LEFT);
            u.text(tc.next(), "fixed(20)", th.text_dim, ALIGN_LEFT);
        }
    }
}

int main(int argc, char **argv)
{
    return example_run("tracks", 720, 570, argc, argv, tracks_frame);
}
