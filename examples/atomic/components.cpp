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

static void components_frame(ui &u, example_app &app)
{
    const theme &th = u.th();
    example_page page =
        example_begin_page(u, app, "Components", "the pui::comp toggles: switch, radio, segmented");
    column col(page.content, example_ui::SECTION_GAP);

    {
        const rect body = example_section(
            u, col.next(160.0f), "Switches",
            "comp::switch_toggle(ui, rect, label, bool&, {.id}) - click or Space toggles",
            app.font_bold);
        column c(body, u.spacing());
        (void)comp::switch_toggle(u, c.next(u.control_h()), "Wi-Fi", g_wifi, {.id = "ex_wifi"_id});
        (void)comp::switch_toggle(u, c.next(u.control_h()), "Bluetooth", g_bluetooth,
                                  {.id = "ex_bt"_id});
        (void)comp::switch_toggle(u, c.next(u.control_h()), "Animations", g_animations,
                                  {.id = "ex_anim"_id});
    }

    {
        const rect body = example_section(
            u, col.next(160.0f), "Radio group",
            "comp::radio_group(ui, rect, labels, i32&, {.id}) - one row per label, Space selects",
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
        char line[128];
        std::snprintf(line, sizeof(line), "wifi %s | quality %d | range %d", g_wifi ? "on" : "off",
                      g_quality, g_range);
        u.text(rect::make(body.x, body.y + 4.0f, body.w, 18.0f), line, th.text_dim, ALIGN_LEFT);
    }

    {
        rect body = example_section(
            u, col.next(150.0f), "Tabs and accordion",
            "comp::tab_bar + comp::accordion_scope - the header clips and animates", app.font_bold);
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

    // a drawer over the page: the toggle button opens it, the scrim closes
    {
        if (u.button(rect::make(page.content.x, page.content.bottom() - u.control_h(),
                                u.button_size("Open drawer").x, u.control_h()),
                     "Open drawer", "ex_drawer_btn"_id, button_opts{.role = "primary"_id}))
            g_drawer_open = true;
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
}

int main(int argc, char **argv)
{
    return example_run("components", 640, 520, argc, argv, components_frame);
}