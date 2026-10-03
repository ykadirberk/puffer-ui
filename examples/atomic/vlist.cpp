// vlist — virtual_list: scroll thousands of rows, submit only the visible.
//
// Shows `scroll_view::virtual_list(count, item_height, fn)`: the callback
// runs only for indices that can paint, and the content height is derived
// from count * item_height (no separate set_content_height call). The frame
// renders the same 5,000-row list whether it shows rows 0-3 or 4996-4999.
//
//   pui_ex_vlist
#include "../example_common.h"

static constexpr i32 kRowCount = 5000;
static i32 g_picked = -1;

static void vlist_frame(ui &u, example_app &app)
{
    const theme &th = u.th();
    example_page page = example_begin_page(u, app, "Virtual list",
                                           "5,000 rows - only the visible ones are submitted");
    column col(page.content, example_ui::SECTION_GAP);
    // the summary is cut off the bottom first; the list takes what is left
    const rect costs_r = col.cut_bottom(84.0f);

    i32 rendered = 0; // rows the callback saw this frame

    {
        const rect body = example_section(
            u, col.remaining(), "The list",
            "sv.virtual_list(count, row_height, fn) - fn runs for the visible slice only",
            app.font_bold);
        scroll_view sv = u.scroll(body, "vlist_rows"_id);
        sv.virtual_list(
            kRowCount, 26.0f,
            [&](ui &uu, i32 index, rect row)
            {
                ++rendered;
                row = row.pad(0.0f, 2.0f);
                const bool picked = g_picked == index;
                uu.draw_rounded_rect(row, picked ? th.accent : th.widget_bg, 4.0f);
                char label[96];
                std::snprintf(label, sizeof(label), "row %d", index);
                uu.text(row.pad(8.0f, 0.0f), label, picked ? th.bg : th.text, ALIGN_LEFT);
                if (uu.button(rect::make(row.right() - 74.0f, row.y + 2.0f, 64.0f, row.h - 4.0f),
                              picked ? "picked" : "pick",
                              id_child("vlist_pick"_id, static_cast<uiid>(index)), "vlist_pick"_id,
                              button_override{.bg = some(picked ? th.bg : th.widget_hover)}))
                    g_picked = picked ? -1 : index;
            });
    }

    {
        const rect body = example_section(
            u, costs_r, "What a frame costs",
            "the callback count, not the list length, decides the work", app.font_bold);
        column c(body, 6.0f);
        u.textf(c.next(18.0f), th.text_dim, ALIGN_LEFT,
                "submitted %d of %d rows this frame - scrolling never grows the cost", rendered,
                kRowCount);
    }
}

int main(int argc, char **argv)
{
    return example_run("vlist", 640, 460, argc, argv, vlist_frame);
}
