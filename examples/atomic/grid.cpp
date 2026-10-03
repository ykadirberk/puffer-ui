// grid — responsive auto-fit grids (CSS `repeat(auto-fit, minmax(...))`).
//
// Shows: `auto_fit_grid` / `grid_cursor`, `columns`/`rows`/`item_width`,
// `next()` (reading order) and `cell(i)` (random access).
//
//   pui_ex_grid   (resize the window: the grid reflows)
#include "../example_common.h"

static void grid_frame(ui &u, example_app &app)
{
    const theme &th = u.th();
    example_page page =
        example_begin_page(u, app, "Auto-fit grid", "responsive cells that reflow with the window");
    column col(page.content, example_ui::SECTION_GAP);

    const i32 count = 12;
    const f32 min_w = 120.0f;
    const f32 cell_h = 64.0f;
    const f32 gap = 8.0f;

    const rect body =
        example_section(u, col.next(324.0f), "auto_fit_grid",
                        "repeat(auto-fit, minmax(120, 1fr)): resize the window", app.font_bold);
    column c(body, 6.0f);

    // Probe the available width to learn the row count, then reserve the height
    // (same probe-then-lay-out pattern the demo uses).
    const f32 avail_w = c.remaining().w;
    const grid_cursor probe =
        auto_fit_grid(rect::make(0.0f, 0.0f, avail_w, 0.0f), count, min_w, cell_h, gap);
    const f32 grid_h =
        static_cast<f32>(probe.rows()) * cell_h + static_cast<f32>(probe.rows() - 1) * gap;

    u.textf(c.next(18.0f), th.text_dim, ALIGN_LEFT,
            "auto_fit_grid: %d columns x %d rows, cell %.0f x %.0f (resize me)", probe.columns(),
            probe.rows(), static_cast<double>(probe.item_width()),
            static_cast<double>(probe.item_height()));

    grid_cursor g = auto_fit_grid(c.next(grid_h), count, min_w, cell_h, gap);
    for (i32 i = 0; i < g.count(); ++i)
    {
        const rect cell = g.next();
        u.card(cell);
        u.textf(cell.pad(10.0f, 0.0f), th.text, ALIGN_LEFT, "Cell %d", i + 1);
        // cell(i) addresses a cell without consuming the cursor.
        u.text(cell.pad(10.0f, 0.0f), i == 0 ? "cell(0)" : "", th.text_dim, ALIGN_RIGHT);
    }

    // Highlight the last cell through random access.
    const rect last = g.cell(count - 1);
    u.draw_rect(rect::make(last.x, last.bottom() - 4.0f, last.w, 4.0f), th.accent);
    u.text(c.next(18.0f), "the accent bar is drawn on g.cell(count - 1)", th.text_dim, ALIGN_LEFT);
}

int main(int argc, char **argv)
{
    return example_run("grid", 720, 440, argc, argv, grid_frame);
}
