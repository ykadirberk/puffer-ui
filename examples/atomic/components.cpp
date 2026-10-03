// components — the pui::comp toggle library: switch, radio, segmented.
//
// Shows: `comp::switch_toggle`, `comp::radio_group`, `comp::segmented` —
// reusable toggles built on the public API: theme-driven styles, explicit
// ids, props/result structs, keyboard operable (Tab + Space/Enter), and
// zero pollution of app state.
//
//   pui_ex_components
#include "../example_common.h"

static bool g_wifi = true;
static bool g_bluetooth = false;
static bool g_animations = true;
static i32 g_quality = 1;
static i32 g_range = 1;
static i32 g_mode = 0;
static bool g_drawer_open = false;
static bool g_palette_open = false;
static comp::palette_state g_palette;
static comp::toast_host g_toasts;

static void components_frame(ui &u, example_app &app)
{
    const theme &th = u.th();
    example_page page =
        example_begin_page(u, app, "Components", "the pui::comp toggles: switch, radio, segmented");
    // Six sections are taller than the window: the page scrolls. The overlays
    // (drawer, palette, toasts) are drawn after the scroll view, over the page.
    {
        scroll_view sv = u.scroll(page.content, "ex_page"_id, scroll_options{0.0f, SCROLL_OVERLAY});
        const rect content = sv.content();
        // lay out in an open-ended column; the scroll view learns the real height
        column col(rect::make(content.x, content.y, content.w, 100000.0f), example_ui::SECTION_GAP);

        {
            const rect body = example_section(
                u, col.next(160.0f), "Switches",
                "comp::switch_toggle(ui, rect, label, bool&, {.id}) - click or Space toggles",
                app.font_bold);
            column c(body, u.spacing());
            (void)comp::switch_toggle(u, c.next(u.control_h()), "Wi-Fi", g_wifi,
                                      {.id = "ex_wifi"_id});
            (void)comp::switch_toggle(u, c.next(u.control_h()), "Bluetooth", g_bluetooth,
                                      {.id = "ex_bt"_id});
            (void)comp::switch_toggle(u, c.next(u.control_h()), "Animations", g_animations,
                                      {.id = "ex_anim"_id});
        }

        {
            const rect body = example_section(u, col.next(160.0f), "Radio group",
                                              "comp::radio_group(ui, rect, labels, i32&, {.id}) - "
                                              "one row per label, Space selects",
                                              app.font_bold);
            static const char *const items[] = {"Draft", "Balanced", "Best quality"};
            (void)comp::radio_group(u, body, items, g_quality, {.id = "ex_quality"_id});
        }

        {
            rect body = example_section(
                u, col.next(140.0f), "Segmented control",
                "comp::segmented(ui, rect, labels, i32&, {.id}) - Day/Week/Month", app.font_bold);
            static const char *const items[] = {"Day", "Week", "Month"};
            (void)comp::segmented(u, body.cut_top(u.control_h()), items, g_range,
                                  {.id = "ex_range"_id});
            u.textf(rect::make(body.x, body.y + 4.0f, body.w, 18.0f), th.text_dim, ALIGN_LEFT,
                    "wifi %s | quality %d | range %d", g_wifi ? "on" : "off", g_quality, g_range);
        }

        {
            rect body = example_section(
                u, col.next(150.0f), "Tabs and accordion",
                "comp::tab_bar + comp::accordion_scope - the header clips and animates",
                app.font_bold);
            static const char *const tabs[] = {"Files", "Search", "Settings"};
            static i32 tab = 0;
            static bool adv_open = true;
            (void)comp::tab_bar(u, body.cut_top(u.control_h()), tabs, tab, {.id = "ex_tabs"_id});
            comp::accordion_scope acc(u, body.cut_top(96.0f), "Advanced", adv_open,
                                      {.id = "ex_acc"_id, .content_h = 60.0f});
            if (acc)
            {
                u.text(acc.content(), "The content area animates open and clips.", th.text_dim,
                       ALIGN_LEFT);
            }
        }

        // table: uniform columns, virtualized body
        {
            const rect body = example_section(
                u, col.next(170.0f), "Table",
                "comp::table(ui, area, headers, rows, cell_fn, {.id}) - only visible rows submit",
                app.font_bold);
            static const char *const headers[] = {"Name", "Size", "Kind"};
            (void)comp::table(
                u, body, headers, 500,
                [](ui &uu, rect cell, i32 row, i32 col)
                {
                    char label[48];
                    if (col == 0)
                        std::snprintf(label, sizeof(label), "file_%03d.bin", row);
                    else if (col == 1)
                        std::snprintf(label, sizeof(label), "%d KB", (row * 7) % 900 + 1);
                    else
                        std::snprintf(label, sizeof(label), "%s", (row % 2) ? "image" : "text");
                    uu.text(cell, label, uu.th().text, ALIGN_LEFT);
                },
                {.id = "ex_table"_id});
        }

        // the overlays' triggers: palette, drawer and a toast
        {
            const rect body = example_section(u, col.next(96.0f), "Palette, drawer and toasts",
                                              "Ctrl+K style filter-and-run; a sliding drawer; "
                                              "transient notifications",
                                              app.font_bold);
            row r(body.top_slice(u.control_h()), u.spacing());
            if (u.button(r.next(u.button_size("Open palette").x), "Open palette", "ex_pal_btn"_id,
                         button_opts{.role = "primary"_id}))
                g_palette_open = true;
            if (u.button(r.next(u.button_size("Open drawer").x), "Open drawer", "ex_drawer_btn"_id))
                g_drawer_open = true;
            if (u.button(r.next(u.button_size("Show a toast").x), "Show a toast",
                         "ex_toast_btn"_id))
                g_toasts.push("Saved to disk", 1);
        }
        sv.set_content_height(col.remaining().y - content.y);
    }

    // a drawer over the page: the scrim closes it
    {
        const rect host = page.content;
        comp::drawer_scope dr(u, host, g_drawer_open, {.id = "ex_drawer"_id, .width = 220.0f});
        if (dr)
        {
            column dc(dr.content(), u.spacing());
            u.text(dc.next(u.text_size("Drawer").y), "Drawer", th.text, ALIGN_LEFT);
            u.text(dc.next(18.0f), "the scrim closes on click;", th.text_dim, ALIGN_LEFT);
            u.text(dc.next(18.0f), "the content clips and slides.", th.text_dim, ALIGN_LEFT);
        }
    }

    // command palette + toast host
    {
        static const comp::palette_command cmds[] = {{"Open file", "Ctrl+O"},
                                                     {"Save file", "Ctrl+S"},
                                                     {"Toggle theme", ""},
                                                     {"Close window", "Esc"}};
        comp::palette_result pr = comp::command_palette(
            u, page.content, g_palette_open, g_palette,
            std::span<const comp::palette_command>(cmds), {.id = "ex_pal"_id});
        if (pr.chosen >= 0) g_toasts.push(cmds[pr.chosen].name, 0);
    }

    comp::toast_draw(u, page.content, g_toasts, {.id = "ex_toasts"_id});
}

int main(int argc, char **argv)
{
    return example_run("components", 640, 520, argc, argv, components_frame);
}