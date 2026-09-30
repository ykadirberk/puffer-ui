// panels — cards and floating panels.
//
// Shows: `card` / `card_override`, `panel` flags (`PANEL_NO_CONTROLS`,
// `PANEL_NO_DRAG`, `PANEL_NO_TITLEBAR`, `PANEL_NO_SHADOW`), `panel_scope`
// content, close reporting, and dragging.
//
//   pui_ex_panels   (drag the panel titlebars)
#include "../example_common.h"

static bool g_show_a = true, g_show_b = true, g_show_c = true;
static rect g_panel_a = rect::make(340.0f, 190.0f, 270.0f, 160.0f);
static rect g_panel_b = rect::make(30.0f, 200.0f, 250.0f, 120.0f);
static rect g_panel_c = rect::make(320.0f, 360.0f, 240.0f, 96.0f);

static void panels_frame(ui &u, example_app &app)
{
    const theme &th = u.th();
    example_page page =
        example_begin_page(u, app, "Panels", "cards and floating panels with scope content");
    column col(page.content, example_ui::SECTION_GAP);

    {
        const rect body =
            example_section(u, col.next(144.0f), "Cards",
                            "u.card(rect) / u.card(rect, card_override)", app.font_bold);
        row r(body, 8.0f);
        const rect a = r.next(200.0f);
        u.card(a);
        u.text(a.pad(12.0f, 0.0f), "u.card(rect)", th.text, ALIGN_LEFT);

        const rect b = r.next(220.0f);
        u.card(b, card_override{.bg = some(color{40, 80, 60, 220}),
                                .border = some(th.accent),
                                .radius = some(10.0f),
                                .border_thickness = some(2.0f)});
        u.text(b.pad(12.0f, 0.0f), "card_override", th.text, ALIGN_LEFT);
    }

    {
        const rect body = example_section(u, col.next(96.0f), "Floating panels",
                                          "drag the titlebar; the dot reports through panel_scope",
                                          app.font_bold);
        row r(body, 8.0f);
        if (u.button(r.next(120.0f), "Reopen A", "pn_reopen_a"_id)) g_show_a = true;
        if (u.button(r.next(120.0f), "Reopen B", "pn_reopen_b"_id)) g_show_b = true;
        if (u.button(r.next(120.0f), "Reopen C", "pn_reopen_c"_id)) g_show_c = true;
        u.text(r.remaining(), "close_requested -> your model decides", th.text_dim, ALIGN_RIGHT);
    }

    // A: default panel (titlebar, close dot, draggable).
    if (g_show_a)
    {
        panel_scope p = u.panel("Default panel", g_panel_a, PANEL_NONE, "pn_a"_id);
        if (p.close_requested)
            g_show_a = false;
        else if (p)
        {
            column pc(p.content(), 6.0f);
            u.text(pc.next(18.0f), "panel_scope content()", th.text, ALIGN_LEFT);
            u.text(pc.next(18.0f), "p.close_requested on the dot", th.text_dim, ALIGN_LEFT);
            u.text(pc.next(18.0f), "bounds are written back on drag", th.text_dim, ALIGN_LEFT);
        }
    }

    // B: pinned info box (no controls, no drag).
    if (g_show_b)
    {
        panel_scope p = u.panel("Pinned", g_panel_b, PANEL_NO_CONTROLS | PANEL_NO_DRAG, "pn_b"_id);
        column pc(p.content(), 6.0f);
        u.text(pc.next(18.0f), "PANEL_NO_CONTROLS | PANEL_NO_DRAG", th.text, ALIGN_LEFT);
        u.text(pc.next(18.0f), "close it with the Reopen B button", th.text_dim, ALIGN_LEFT);
    }

    // C: titlebar-less floating card.
    if (g_show_c)
    {
        panel_scope p = u.panel("", g_panel_c, PANEL_NO_TITLEBAR | PANEL_NO_SHADOW, "pn_c"_id);
        column pc(p.content(), 6.0f);
        u.text(pc.next(18.0f), "PANEL_NO_TITLEBAR | PANEL_NO_SHADOW", th.text, ALIGN_LEFT);
        u.text(pc.next(18.0f), "a floating card", th.text_dim, ALIGN_LEFT);
    }
}

int main(int argc, char **argv)
{
    return example_run("panels", 720, 480, argc, argv, panels_frame);
}
