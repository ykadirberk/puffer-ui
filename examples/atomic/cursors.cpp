// cursors — `column` and `row` layout cursors.
//
// Shows: `next`, `cut_top`/`cut_bottom`/`cut_left`/`cut_right`, `space`,
// `remaining`, and the clamp behavior (a slice never overflows its cursor).
//
//   pui_ex_cursors
#include "../example_common.h"

static void cursors_frame(ui &u, example_app &app)
{
    const theme &th = u.th();
    example_page page = example_begin_page(u, app, "Layout cursors",
                                           "column and row: next, cuts, space and clamping");
    column col(page.content, example_ui::SECTION_GAP);

    // Header / footer via cut_top / cut_bottom; the middle is what remains.
    {
        const rect body =
            example_section(u, col.next(144.0f), "Header and footer",
                            "cut_top / cut_bottom leave the middle as remaining()", app.font_bold);
        column c(body, 8.0f);
        const rect header = c.next(40.0f);
        const rect footer = c.next(28.0f);
        u.draw_rect(header, th.panel_bg);
        u.text(header.pad(10.0f, 0.0f), "col.cut_top(40)", th.text, ALIGN_LEFT);
        u.draw_rect(footer, th.panel_bg);
        u.text(footer.pad(10.0f, 0.0f), "col.cut_bottom(28) - remaining() is the body", th.text_dim,
               ALIGN_LEFT);
    }

    // A row splits the body left-to-right; `next` advances the cursor, and a
    // stack clamps (asking for more than what is left returns the remainder).
    {
        const rect body = example_section(
            u, col.next(300.0f), "Row and clamping",
            "row.next advances; space skips; stacks clamp and the next slice is empty",
            app.font_bold);
        row main_row(body, 8.0f);
        const rect sidebar = main_row.next(150.0f);
        const rect content = main_row.remaining();

        u.draw_rect(sidebar, th.widget_bg);
        {
            column side(sidebar.pad(8.0f), 6.0f);
            u.text(side.next(18.0f), "sidebar", th.text, ALIGN_LEFT);
            for (i32 i = 0; i < 4; ++i)
            {
                char label[24];
                std::snprintf(label, sizeof(label), "item %d", i + 1);
                const rect item = side.next(24.0f);
                u.draw_rounded_rect(item, th.panel_bg, 4.0f);
                u.text(item.pad(8.0f, 0.0f), label, th.text_dim, ALIGN_LEFT);
            }
        }

        {
            column cc(content, 8.0f);
            // `space` skips a gap without producing a rect.
            cc.space(4.0f);
            for (i32 i = 0; i < 3; ++i)
            {
                const rect card = cc.next(56.0f);
                u.card(card);
                char label[64];
                std::snprintf(label, sizeof(label), "cc.next(56)  ->  #%d", i + 1);
                u.text(card.pad(10.0f, 0.0f), label, th.text, ALIGN_LEFT);
            }

            const rect clamped = cc.next(500.0f);
            u.draw_rect(clamped, th.accent);
            char note[96];
            std::snprintf(note, sizeof(note), "cc.next(500) clamped to %.0f px",
                          static_cast<double>(clamped.h));
            u.text(clamped.pad(8.0f, 0.0f), note, th.bg, ALIGN_LEFT);
        }
    }
}

int main(int argc, char **argv)
{
    return example_run("cursors", 720, 560, argc, argv, cursors_frame);
}
