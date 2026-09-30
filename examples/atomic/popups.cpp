// popups — menus, modal dialogs and the focus trap.
//
// Shows: `popup` + flags (`POPUP_CLOSE_ON_ESCAPE`, `POPUP_CLOSE_ON_CLICK_OUTSIDE`,
// `POPUP_MODAL`), `popup_scope.close_requested`, and how popups block the base UI.
//
//   pui_ex_popups   (open the menu; try Tab / Escape / clicking outside)
#include "../example_common.h"

static bool g_menu_open = false;
static bool g_modal_open = false;
static char g_status[96] = "pick a menu item";

static void popups_frame(ui &u, example_app &app)
{
    const theme &th = u.th();
    const rect client =
        rect::make(0.0f, 0.0f, static_cast<f32>(app.width), static_cast<f32>(app.height));
    example_page page =
        example_begin_page(u, app, "Popups", "menus, modal dialogs and the focus trap");
    column col(page.content, example_ui::SECTION_GAP);

    {
        const rect body =
            example_section(u, col.next(98.0f), "Menu and modal dialog",
                            "popup(id, rect, flags): a top input-capturing layer", app.font_bold);
        row r(body, 8.0f);
        const rect menu_btn = r.next(120.0f);
        if (u.button(menu_btn, "Menu", "pp_menu"_id, "primary"_id)) g_menu_open = !g_menu_open;
        if (u.button(r.next(150.0f), "Modal dialog", "pp_modal"_id)) g_modal_open = true;
        u.text(r.remaining(), g_status, th.text_dim, ALIGN_RIGHT);

        // The menu is drawn after the base UI (on top) and closes through
        // close_requested (Escape / click outside) or by picking an item.
        if (g_menu_open)
        {
            const rect menu = rect::make(menu_btn.x, menu_btn.bottom() + 4.0f, 190.0f, 138.0f);
            popup_scope p = u.popup("pp_menu_popup"_id, menu,
                                    POPUP_CLOSE_ON_ESCAPE | POPUP_CLOSE_ON_CLICK_OUTSIDE);
            if (p.close_requested)
                g_menu_open = false;
            else
            {
                style_scope flat(u, button_override{.radius = some(0.0f)});
                u.draw_rounded_rect(menu, th.panel_bg, 6.0f);
                u.draw_rect(rect::make(menu.x, menu.y, menu.w, 2.0f), th.accent);
                column items(menu.pad(8.0f), 4.0f);
                u.text(items.next(18.0f), "Actions", th.text_dim, ALIGN_LEFT);
                if (u.button(items.next(26.0f), "Rename", "pp_rename"_id))
                {
                    std::snprintf(g_status, sizeof(g_status), "rename picked");
                    g_menu_open = false;
                }
                if (u.button(items.next(26.0f), "Duplicate", "pp_dup"_id))
                {
                    std::snprintf(g_status, sizeof(g_status), "duplicate picked");
                    g_menu_open = false;
                }
                if (u.button(items.next(26.0f), "Delete", "pp_del"_id, "danger"_id))
                {
                    std::snprintf(g_status, sizeof(g_status), "delete picked");
                    g_menu_open = false;
                }
            }
        }
    }

    {
        const rect body = example_section(
            u, col.next(98.0f), "Blocked base UI",
            "Tab stays inside an open popup; the base UI is blocked while the modal is open",
            app.font_bold);
        row r(body, 8.0f);
        if (u.button(r.next(140.0f), "Blocked A", "pp_base_a"_id))
            std::snprintf(g_status, sizeof(g_status), "base A clicked");
        if (u.button(r.next(140.0f), "Blocked B", "pp_base_b"_id))
            std::snprintf(g_status, sizeof(g_status), "base B clicked");
    }

    // The modal dialog is constructed last so it draws above everything.
    if (g_modal_open)
    {
        const rect dlg =
            rect::make(client.center_x() - 160.0f, client.center_y() - 70.0f, 320.0f, 140.0f);
        popup_scope p = u.popup("pp_modal_popup"_id, dlg, POPUP_MODAL | POPUP_CLOSE_ON_ESCAPE);
        if (p.close_requested)
            g_modal_open = false;
        else
        {
            u.draw_rounded_rect(dlg, th.panel_bg, 8.0f);
            u.draw_rect(rect::make(dlg.x, dlg.y, dlg.w, 3.0f), th.accent);
            column dc(dlg.pad(16.0f), 8.0f);
            u.text(dc.next(24.0f), "Modal dialog", th.text, ALIGN_LEFT);
            u.text(dc.next(20.0f), "POPUP_MODAL blocks the base UI and traps Tab.", th.text_dim,
                   ALIGN_LEFT);
            row buttons(dc.next(30.0f), 8.0f);
            if (u.button(buttons.next(120.0f), "Cancel", "pp_cancel"_id)) g_modal_open = false;
            if (u.button(buttons.next(120.0f), "OK", "pp_ok"_id, "primary"_id))
            {
                std::snprintf(g_status, sizeof(g_status), "modal confirmed");
                g_modal_open = false;
            }
        }
    }
}

int main(int argc, char **argv)
{
    return example_run("popups", 680, 420, argc, argv, popups_frame);
}
