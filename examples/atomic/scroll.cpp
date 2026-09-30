// scroll — scrollable viewports.
//
// Shows: `scroll` with gutter and overlay modes, wheel + draggable bar,
// `set_content_height`, `overflows`, `ensure_visible` and `scroll_to`.
//
//   pui_ex_scroll   (wheel over a list; drag its bar)
#include "../example_common.h"

static const i32 ITEM_COUNT = 40;
static i32 g_selected = 3;
static i32 g_scroll_cmd = 0; // 1 top, 2 bottom, 3 show item 20

static void scroll_frame(ui &u, example_app &app)
{
    const theme &th = u.th();
    example_page page =
        example_begin_page(u, app, "Scroll", "gutter and overlay scrollbars over live lists");
    column col(page.content, example_ui::SECTION_GAP);

    const rect body = example_section(
        u, col.next(430.0f), "Scroll viewports",
        "scroll(viewport, id, {padding, flags}) + set_content_height()", app.font_bold);
    column c(body, 8.0f);

    {
        row r(c.next(28.0f), 8.0f);
        if (u.button(r.next(100.0f), "scroll top", "sc_top"_id)) g_scroll_cmd = 1;
        if (u.button(r.next(120.0f), "scroll bottom", "sc_bottom"_id)) g_scroll_cmd = 2;
        if (u.button(r.next(130.0f), "show item 20", "sc_item"_id)) g_scroll_cmd = 3;
        u.text(r.remaining(), "ensure_visible / scroll_to run on the live scope", th.text_dim,
               ALIGN_RIGHT);
    }

    row main(c.next(300.0f), 12.0f);

    // --- gutter mode: the bar reserves width while overflowing ---------------
    // The caption is drawn after the scroll scope: an alive `scroll_view`
    // clips its viewport, so text below it must come after the scope ends.
    rect gutter_area{};
    char gutter_caption[96] = "";
    {
        const rect area = main.next(260.0f);
        gutter_area = area;
        u.draw_rect(area, th.panel_bg);
        scroll_view sv = u.scroll(area.pad(4.0f), "sc_gutter"_id,
                                  scroll_options{4.0f, SCROLL_ALWAYS_RESERVE_BAR});

        if (g_scroll_cmd == 1)
            sv.scroll_to(0.0f);
        else if (g_scroll_cmd == 2)
            sv.scroll_to(1.0e9f); // clamped to the end
        else if (g_scroll_cmd == 3)
        {
            // item 20 in content space: ensure_visible takes a screen-space rect
            const rect target =
                rect::make(sv.content().x, sv.content().y + 19.0f * 28.0f, 1.0f, 24.0f);
            sv.ensure_visible(target);
            g_selected = 19;
        }

        column lc(sv.content(), 4.0f);
        for (i32 i = 0; i < ITEM_COUNT; ++i)
        {
            const rect item = lc.next(24.0f);
            interaction in = u.interact(u.auto_id(), item);
            if (in.hovered) u.set_cursor(CURSOR_HAND);
            const bool selected = (i == g_selected);
            u.draw_rounded_rect(item,
                                in.held      ? th.widget_active
                                : in.hovered ? th.widget_hover
                                : selected   ? th.accent
                                             : th.widget_bg,
                                3.0f);
            char label[32];
            std::snprintf(label, sizeof(label), "item %02d", i + 1);
            u.text(item.pad(8.0f, 0.0f), label, selected ? th.bg : th.text, ALIGN_LEFT);
            if (in.clicked)
            {
                g_selected = i;
                sv.ensure_visible(item);
            }
        }
        sv.set_content_height(static_cast<f32>(ITEM_COUNT) * 24.0f +
                              static_cast<f32>(ITEM_COUNT - 1) * 4.0f);

        std::snprintf(gutter_caption, sizeof(gutter_caption), "overflows: %s   offset: %.0f",
                      sv.overflows() ? "yes" : "no", static_cast<double>(sv.offset()));
    }
    u.text(rect::make(gutter_area.x + 6.0f, gutter_area.bottom() + 4.0f, gutter_area.w, 16.0f),
           gutter_caption, th.text_dim, ALIGN_LEFT);

    // --- overlay mode: the bar floats over the content -----------------------
    {
        const rect area = main.next(240.0f);
        u.draw_rect(area, th.panel_bg);
        scroll_view sv =
            u.scroll(area.pad(4.0f), "sc_overlay"_id, scroll_options{4.0f, SCROLL_OVERLAY});
        column lc(sv.content(), 6.0f);
        for (i32 i = 0; i < 12; ++i)
        {
            const rect line = lc.next(20.0f);
            char label[64];
            std::snprintf(label, sizeof(label), "overlay row %d - the bar floats", i + 1);
            u.text(line, label, th.text_dim, ALIGN_LEFT);
        }
        sv.set_content_height(12.0f * 20.0f + 11.0f * 6.0f);
    }

    g_scroll_cmd = 0;
}

int main(int argc, char **argv)
{
    return example_run("scroll", 720, 540, argc, argv, scroll_frame);
}
