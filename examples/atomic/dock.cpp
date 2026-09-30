// dock — dockable panels: tabs, splits, drag-to-dock and undocking.
//
// Shows: `dock_space` + a `dock_node` tree + `dock_action`, tab dragging, drop
// zones (center = tab, edge = split, outside = undock), and panel content.
// The tree-mutation policy lives in examples/dock_helpers.h.
//
//   pui_ex_dock   (drag the panel tabs around)
#include "../dock_helpers.h"
#include "../example_common.h"

static dock_node g_root;
static example_dock::dock_pool g_pool;
static bool g_ready = false;
static uiid g_float_panel = 0;
static rect g_float_bounds = rect::make(340.0f, 150.0f, 260.0f, 170.0f);
static i32 g_stat_count = 0;
static bool g_opt = true;

static const char *panel_name(uiid p)
{
    if (p == "stats"_id) return "Stats";
    if (p == "log"_id) return "Log";
    if (p == "settings"_id) return "Settings";
    return "Panel";
}

static void dock_init()
{
    g_pool.reset();
    g_root = dock_node{};
    dock_node *left = g_pool.alloc();
    dock_node *right = g_pool.alloc();

    *left = dock_node{};
    left->kind = DOCK_LEAF;
    left->panels[0] = "stats"_id;
    left->panel_names[0] = "Stats";
    left->panel_count = 1;

    *right = dock_node{};
    right->kind = DOCK_TABS;
    right->panels[0] = "log"_id;
    right->panel_names[0] = "Log";
    right->panels[1] = "settings"_id;
    right->panel_names[1] = "Settings";
    right->panel_count = 2;
    right->active = 0;

    g_root.kind = DOCK_SPLIT_H;
    g_root.ratio = 0.45f;
    g_root.a = left;
    g_root.b = right;
}

static void panel_content(ui &u, uiid panel, rect pc, const theme &th)
{
    const rect inner = pc.pad(8.0f);
    if (panel == "stats"_id)
    {
        column c(inner, 6.0f);
        u.text(c.next(18.0f), "Stats panel", th.text, ALIGN_LEFT);
        if (u.button(c.next(26.0f), "count", "dk_count"_id)) g_stat_count += 1;
        char line[48];
        std::snprintf(line, sizeof(line), "count: %d", g_stat_count);
        u.text(c.next(18.0f), line, th.text_dim, ALIGN_LEFT);
    }
    else if (panel == "log"_id)
    {
        scroll_view sv = u.scroll(inner, "dk_log_scroll"_id);
        column c(sv.content(), 4.0f);
        for (i32 i = 0; i < 30; ++i)
        {
            char line[48];
            std::snprintf(line, sizeof(line), "log %02d", i + 1);
            u.text(c.next(16.0f), line, th.text_dim, ALIGN_LEFT);
        }
        sv.set_content_height(30.0f * 16.0f + 29.0f * 4.0f);
    }
    else
    {
        column c(inner, 6.0f);
        (void)u.checkbox(c.next(24.0f), "Option", g_opt, "dk_opt"_id);
        u.text(c.next(18.0f), "drag my tab to move me", th.text_dim, ALIGN_LEFT);
        u.text(c.next(18.0f), "center tabs, edges split", th.text_dim, ALIGN_LEFT);
    }
}

static void dock_frame(ui &u, example_app &app)
{
    const theme &th = u.th();
    example_page page =
        example_begin_page(u, app, "Dock", "tabs, splits, drag-to-dock and undocking");
    if (!g_ready)
    {
        dock_init();
        g_ready = true;
    }

    column col(page.content, example_ui::SECTION_GAP);
    const rect body = example_section(
        u, col.next(398.0f), "Dock space",
        "drag a tab: center tabs, edges split, dropping outside undocks", app.font_bold);
    const dock_action act =
        u.dock_space("dk"_id, body, g_root,
                     function_ref<void(uiid, rect, bool)>([&](uiid panel, rect pc, bool)
                                                          { panel_content(u, panel, pc, th); }));
    example_dock::apply(g_root, act, panel_name, g_float_panel, g_pool);

    // An undocked panel floats (the demo supports several; this one tracks one).
    if (g_float_panel)
    {
        panel_scope p = u.panel(panel_name(g_float_panel), g_float_bounds, PANEL_NONE,
                                g_float_panel, panel_name(g_float_panel));
        if (p.close_requested)
        {
            example_dock::add_center(*example_dock::first_leaf(g_root), g_float_panel, panel_name);
            g_float_panel = 0;
        }
        else
        {
            column pc(p.content(), 6.0f);
            u.text(pc.next(18.0f), "undocked: drag me back onto the dock", th.text_dim, ALIGN_LEFT);
        }
    }
}

int main(int argc, char **argv)
{
    return example_run("dock", 760, 520, argc, argv, dock_frame);
}
