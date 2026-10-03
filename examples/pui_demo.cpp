// PufferUI (next) — Phase 1 demo: a window using the redesigned core.
// Demonstrates region slicing, the ID stack, interaction, the draw list +
// batching, and the minimal theme. No text yet (that is Phase 3), so this is
// deliberately shape/colour based.
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <pufferui/pufferui.h>

#include <cstdio>
#include <string>

using namespace pui;

// --- state / view ----------------------------------------------------------
// One app-owned state object. Components read the per-frame view snapshot
// (`update_view()`) for derived display data and write model fields directly.
// Carve-out: the framework adjusts dock-tree ratios/active tabs and floating
// panel geometry while interacting, and reports structural dock moves through
// `dock_action`, which the app applies after the frames.
static const char *demo_dock_name(uiid p)
{
    if (p == "stats"_id) return "Stats";
    if (p == "log"_id) return "Log";
    if (p == "notes"_id) return "Notes";
    return "panel";
}

struct demo_state
{
    // model: components write directly
    i32 clicks = 0;
    std::string name = "PufferUI";
    std::string name2 = "second window";
    f32 amount = 3.50f;
    bool popup_open = false;
    bool inspector_open = true;
    uiid float_panel = 0;
    rect float_bounds{620.0f, 110.0f, 260.0f, 170.0f};    // desktop space
    rect inspector_bounds{560.0f, 90.0f, 280.0f, 170.0f}; // desktop space
    rect float_home{620.0f, 110.0f, 260.0f, 170.0f};      // undock anchor
    rect inspector_home{560.0f, 90.0f, 280.0f, 170.0f};
    dock_node dock_root{};
    dock_node dock_second{};
    std::string layout_saved; // dock_save_tree output (Load button restores it)

    // view: derived once per frame, then only read
    struct view_t
    {
        i32 clicks = 0; // snapshot for consistent display
        char clicks_label[32]{};
        const char *float_name = "";
    } view;

    void update_view()
    {
        view.clicks = clicks;
        std::snprintf(view.clicks_label, sizeof(view.clicks_label), "Clicks: %d", clicks);
        view.float_name = demo_dock_name(float_panel);
    }
};

static demo_state g_state;

static dock_node g_dock_a, g_dock_b; // window 1 tree storage
static dock_node g_dock_pool[16];
static i32 g_dock_pool_used = 0;

static dock_node *demo_dock_alloc()
{
    if (g_dock_pool_used >= 16) return nullptr;
    dock_node *n = &g_dock_pool[g_dock_pool_used++];
    *n = dock_node{};
    return n;
}

static bool demo_dock_empty(const dock_node &n)
{
    if (n.kind == DOCK_SPLIT_H || n.kind == DOCK_SPLIT_V)
        return (!n.a || demo_dock_empty(*n.a)) && (!n.b || demo_dock_empty(*n.b));
    return n.panel_count == 0;
}

static void demo_dock_collapse(dock_node &n)
{
    if (n.kind != DOCK_SPLIT_H && n.kind != DOCK_SPLIT_V) return;
    if (n.a && demo_dock_empty(*n.a))
    {
        if (n.b) n = *n.b;
        return;
    }
    if (n.b && demo_dock_empty(*n.b))
    {
        if (n.a) n = *n.a;
        return;
    }
    if (n.a) demo_dock_collapse(*n.a);
    if (n.b) demo_dock_collapse(*n.b);
}

static void demo_dock_remove(dock_node &n, uiid p)
{
    for (i32 i = 0; i < n.panel_count; ++i)
    {
        if (n.panels[i] != p) continue;
        for (i32 j = i; j < n.panel_count - 1; ++j)
        {
            n.panels[j] = n.panels[j + 1];
            n.panel_names[j] = n.panel_names[j + 1];
        }
        n.panel_count -= 1;
        if (n.active >= n.panel_count) n.active = (n.panel_count > 0) ? n.panel_count - 1 : 0;
        return;
    }
    if (n.a) demo_dock_remove(*n.a, p);
    if (n.b) demo_dock_remove(*n.b, p);
}

static bool demo_dock_contains(const dock_node &n, const dock_node *target)
{
    if (&n == target) return true;
    if (n.a && demo_dock_contains(*n.a, target)) return true;
    if (n.b && demo_dock_contains(*n.b, target)) return true;
    return false;
}

static dock_node *demo_dock_first_leaf(dock_node &n)
{
    if (n.kind != DOCK_SPLIT_H && n.kind != DOCK_SPLIT_V) return &n;
    if (n.a)
    {
        dock_node *r = demo_dock_first_leaf(*n.a);
        if (r) return r;
    }
    if (n.b)
    {
        dock_node *r = demo_dock_first_leaf(*n.b);
        if (r) return r;
    }
    return nullptr;
}

static void demo_dock_add_center(dock_node &t, uiid panel)
{
    if (t.panel_count >= MAX_DOCK_PANELS) return;
    t.panels[t.panel_count] = panel;
    t.panel_names[t.panel_count] = demo_dock_name(panel);
    t.panel_count += 1;
    t.active = t.panel_count - 1;
    if (t.panel_count > 1) t.kind = DOCK_TABS;
}

static void demo_dock_split_at(dock_node &target, uiid panel, i32 zone)
{
    dock_node *new_leaf = demo_dock_alloc();
    dock_node *old_copy = demo_dock_alloc();
    if (!new_leaf || !old_copy) return;

    new_leaf->kind = DOCK_LEAF;
    new_leaf->panels[0] = panel;
    new_leaf->panel_names[0] = demo_dock_name(panel);
    new_leaf->panel_count = 1;
    new_leaf->active = 0;

    *old_copy = target; // keep the existing subtree
    const bool first = (zone == DOCK_ZONE_LEFT || zone == DOCK_ZONE_TOP);
    target.kind = (zone == DOCK_ZONE_LEFT || zone == DOCK_ZONE_RIGHT) ? DOCK_SPLIT_H : DOCK_SPLIT_V;
    target.ratio = first ? 0.35f : 0.65f;
    target.a = first ? new_leaf : old_copy;
    target.b = first ? old_copy : new_leaf;
    target.panel_count = 0;
    target.active = 0;
}

static void demo_dock_apply(demo_state &s, const dock_action &act)
{
    if (!act.active) return;

    demo_dock_remove(s.dock_root, act.panel);
    demo_dock_remove(s.dock_second, act.panel);
    demo_dock_collapse(s.dock_root);
    demo_dock_collapse(s.dock_second);

    if (!act.target)
    { // undock -> floating window
        if (s.float_panel != act.panel)
        {
            s.float_panel = act.panel;
            s.float_bounds = s.float_home;
        }
        return;
    }
    if (s.float_panel == act.panel) s.float_panel = 0; // re-docked

    // The drop target may live in either window's tree; fall back to window 1.
    dock_node *target = act.target;
    if (!demo_dock_contains(s.dock_root, target) && !demo_dock_contains(s.dock_second, target))
        target = demo_dock_first_leaf(s.dock_root);
    if (!target) return;

    if (act.zone == DOCK_ZONE_CENTER)
        demo_dock_add_center(*target, act.panel);
    else
        demo_dock_split_at(*target, act.panel, act.zone);
}

static void demo_dock_init(demo_state &s)
{
    g_dock_a = dock_node{};
    g_dock_a.kind = DOCK_LEAF;
    g_dock_a.panels[0] = "stats"_id;
    g_dock_a.panel_names[0] = "Stats";
    g_dock_a.panel_count = 1;

    g_dock_b = dock_node{};
    g_dock_b.kind = DOCK_LEAF;
    g_dock_b.panels[0] = "log"_id;
    g_dock_b.panel_names[0] = "Log";
    g_dock_b.panel_count = 1;

    s.dock_root = dock_node{};
    s.dock_root.kind = DOCK_SPLIT_H;
    s.dock_root.a = &g_dock_a;
    s.dock_root.b = &g_dock_b;
    s.dock_root.ratio = 0.5f;

    s.dock_second = dock_node{};
    s.dock_second.kind = DOCK_LEAF;
    s.dock_second.panels[0] = "notes"_id;
    s.dock_second.panel_names[0] = "Notes";
    s.dock_second.panel_count = 1;

    g_dock_pool_used = 0;
    s.float_panel = 0;
}

// --- pure projection + reducer --------------------------------------------
static SDL_Window *g_window = nullptr;
static SDL_Window *g_window2 = nullptr;
static SDL_Renderer *g_renderer = nullptr;
static render_device *g_device = nullptr;
static render_surface *g_surface = nullptr;
static render_surface *g_surface2 = nullptr;
static context *g_ctx = nullptr;
static font_handle g_font_bold = FONT_INVALID;
static pui::window *g_win1 = nullptr;
static pui::window *g_win2 = nullptr;
static u64 g_freq = 1;
static u64 g_last = 0;
static int g_hover = -1;
static bool g_vsync = false;     // perf measurement defaults to no vsync
static std::string g_title_last; // only call SDL_SetWindowTitle on change

static rect demo_window_client(SDL_Window *w)
{
    int x = 0, y = 0, ww = 0, wh = 0;
    SDL_GetWindowPosition(w, &x, &y);
    SDL_GetWindowSize(w, &ww, &wh);
    return rect::make(static_cast<f32>(x), static_cast<f32>(y), static_cast<f32>(ww),
                      static_cast<f32>(wh));
}

// --- frame-time HUD: one sample per outer frame, drawn in every window ---
static constexpr i32 FT_SAMPLES = 120;
static f32 g_ft_ms[FT_SAMPLES]{};
static i32 g_ft_head = 0, g_ft_count = 0;
static f32 g_ft_avg = 0.0f, g_ft_max = 0.0f, g_ft_fps = 0.0f;
static f32 g_ft_ui = 0.0f;     // smoothed ms spent inside the UI frames (incl. present)
static f32 g_ft_sdl = 0.0f;    // smoothed ms spent outside them (SDL pump + callbacks)
static f32 g_ft_events = 0.0f; // smoothed mouse-motion events per frame
static i32 g_ev_motion = 0;    // motion events since the last outer frame

static void demo_record_frame_time(f64 dt, f64 ui_ms)
{
    const f32 ms = static_cast<f32>(dt * 1000.0);
    if (ms > 0.0f && ms < 1000.0f)
    { // ignore pauses/minimize stalls
        g_ft_ms[g_ft_head] = ms;
        g_ft_head = (g_ft_head + 1) % FT_SAMPLES;
        if (g_ft_count < FT_SAMPLES) g_ft_count += 1;
    }
    f32 sum = 0.0f, mx = 0.0f;
    for (i32 i = 0; i < g_ft_count; ++i)
    {
        const f32 v = g_ft_ms[i];
        sum += v;
        if (v > mx) mx = v;
    }
    g_ft_avg = g_ft_count > 0 ? sum / static_cast<f32>(g_ft_count) : 0.0f;
    g_ft_max = mx;
    g_ft_fps = g_ft_avg > 0.001f ? 1000.0f / g_ft_avg : 0.0f;

    // Slow moving averages so the breakdown is readable frame to frame.
    const f32 k = 0.05f;
    g_ft_ui += (static_cast<f32>(ui_ms) - g_ft_ui) * k;
    const f32 sdl_ms = max2(0.0f, ms - static_cast<f32>(ui_ms));
    g_ft_sdl += (sdl_ms - g_ft_sdl) * k;
    g_ft_events += (static_cast<f32>(g_ev_motion) - g_ft_events) * k;
}

static void demo_draw_frame_hud(ui &u, const pui::window &w)
{
    const theme &th = u.th();
    const f32 hud_w = 200.0f, hud_h = 88.0f;
    const rect hud = {w.area.right() - hud_w - 12.0f, w.area.bottom() - hud_h - 12.0f, hud_w,
                      hud_h};
    if (hud.x < 4.0f || hud.y < 4.0f || hud.right() > w.area.right() ||
        hud.bottom() > w.area.bottom())
        return; // window too small

    u.draw_rounded_rect(hud, color{10, 12, 18, 215}, 6.0f);
    const color state = g_ft_avg <= 16.8f   ? color{80, 200, 130, 255}
                        : g_ft_avg <= 33.3f ? color{230, 200, 90, 255}
                                            : color{230, 90, 90, 255};
    u.draw_rect(rect::make(hud.x + 6.0f, hud.y + 7.0f, 2.0f, hud.h - 14.0f), state);

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.1f FPS", static_cast<double>(g_ft_fps));
    u.text(rect::make(hud.x + 14.0f, hud.y + 4.0f, hud.w - 20.0f, 18.0f), buf, th.text, ALIGN_LEFT);
    std::snprintf(buf, sizeof(buf), "%.1f ms  max %.1f", static_cast<double>(g_ft_avg),
                  static_cast<double>(g_ft_max));
    u.text(rect::make(hud.x + 14.0f, hud.y + 4.0f, hud.w - 20.0f, 18.0f), buf, th.text_dim,
           ALIGN_RIGHT);

    // The graph auto-scales so frame times well under 16.7 ms stay readable.
    const rect graph = {hud.x + 10.0f, hud.y + 26.0f, hud.w - 20.0f, 22.0f};
    u.draw_rect(graph, color{0, 0, 0, 110});
    const f32 scale_ms = max2(4.0f, g_ft_max * 1.25f);
    const f32 scale = graph.h / scale_ms;
    const f32 bar_w = graph.w / static_cast<f32>(FT_SAMPLES);
    for (i32 i = 0; i < g_ft_count; ++i)
    {
        const i32 idx = (g_ft_head - g_ft_count + i + FT_SAMPLES * 2) % FT_SAMPLES;
        const f32 ms = g_ft_ms[idx];
        f32 bh = ms * scale;
        if (bh > graph.h) bh = graph.h;
        if (bh < 1.0f) bh = 1.0f;
        const color c = ms <= 16.8f   ? color{80, 200, 130, 220}
                        : ms <= 33.3f ? color{230, 200, 90, 220}
                                      : color{230, 90, 90, 230};
        u.draw_rect(rect::make(graph.x + static_cast<f32>(i) * bar_w, graph.bottom() - bh,
                               max2(1.0f, bar_w - 0.5f), bh),
                    c);
    }
    const f32 ref_y = graph.bottom() - 16.667f * scale; // 60 Hz reference
    if (ref_y > graph.y)
        u.draw_line(graph.x, ref_y, graph.right(), ref_y, color{255, 255, 255, 60}, 1.0f);

    // Where the frame time goes: inside the UI frames (including present) vs the
    // rest of the loop (SDL event pump/callbacks), plus motion events per frame.
    std::snprintf(buf, sizeof(buf), "ui %.1f  sdl %.1f  ev %.0f%s", static_cast<double>(g_ft_ui),
                  static_cast<double>(g_ft_sdl), static_cast<double>(g_ft_events),
                  u.animations_active() ? "  anim" : "");
    u.text(rect::make(hud.x + 14.0f, hud.y + 51.0f, hud.w - 20.0f, 14.0f), buf,
           g_ft_sdl > g_ft_ui * 0.5f ? color{230, 200, 90, 255} : th.text_dim, ALIGN_LEFT);

    // vsync toggle on the footer row
    const rect tog = {hud.x + 8.0f, hud.y + hud.h - 22.0f, hud.w - 16.0f, 16.0f};
    interaction it = u.interact(id_child(w.root, "hud_vsync"_id), tog);
    if (it.hovered) u.set_cursor(pui::CURSOR_HAND);
    const color vs_col = g_vsync ? color{80, 200, 130, 255} : color{230, 140, 90, 255};
    std::snprintf(buf, sizeof(buf), "vsync %s", g_vsync ? "ON" : "OFF");
    u.text(tog, buf, it.hovered ? th.text : vs_col, ALIGN_LEFT);
    u.text(tog, "click to toggle", th.text_dim, ALIGN_RIGHT);
    if (it.clicked)
    {
        g_vsync = !g_vsync;
        if (g_ctx->device) g_ctx->device->set_vsync(g_vsync);
    }
}

SDL_AppResult SDL_AppInit(void **, int, char **)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) return SDL_APP_FAILURE;

    // Interacting with a second window must not cost an extra click: with the
    // default, the first click on an unfocused window is consumed by activation.
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");

    g_window = SDL_CreateWindow("PufferUI (next) - window 1", 900, 600,
                                SDL_WINDOW_BORDERLESS | SDL_WINDOW_RESIZABLE);
    if (!g_window) return SDL_APP_FAILURE;

    g_renderer = SDL_CreateRenderer(g_window, nullptr);
    if (!g_renderer) return SDL_APP_FAILURE;

    g_device = create_sdl3_device(g_renderer);
    if (!g_device) return SDL_APP_FAILURE;
    g_device->set_vsync(g_vsync); // the HUD toggles this at runtime
    g_surface = g_device->create_surface(g_window);

    // A real second window over the same device: the device creates its renderer
    // and mirrors the shared textures (SDL textures are renderer-scoped).
    // g_window2 = SDL_CreateWindow("PufferUI (next) - window 2", 520, 420, SDL_WINDOW_RESIZABLE);
    if (g_window2)
    {
        const rect c1 = demo_window_client(g_window);
        SDL_SetWindowPosition(g_window2, static_cast<int>(c1.right()) + 16,
                              static_cast<int>(c1.y) + 40);
        g_surface2 = g_device->create_surface(g_window2);
    }

    SDL_StartTextInput(g_window);
    if (g_window2) SDL_StartTextInput(g_window2);

    g_ctx = create_context(g_device, g_surface);

    font_handle font = load_font(g_ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
    if (font == FONT_INVALID) font = load_font(g_ctx, "assets/fonts/DejaVuSans.ttf");
    if (font == FONT_INVALID) font = load_font(g_ctx, "C:/Windows/Fonts/segoeui.ttf");
    if (font == FONT_INVALID) font = load_font(g_ctx, "C:/Windows/Fonts/arial.ttf");
    g_font_bold = load_font(g_ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans-Bold.ttf");
    if (g_font_bold == FONT_INVALID) g_font_bold = font;

    theme t = default_dark();
    t.font = font;
    t.text_size = 15.0f;
    set_button_role(t, "primary"_id,
                    button_override{.bg = some(color{40, 110, 200, 255}),
                                    .hover_bg = some(color{60, 140, 230, 255})});
    set_button_role(t, "danger"_id,
                    button_override{.bg = some(color{180, 50, 50, 255}),
                                    .hover_bg = some(color{210, 70, 70, 255})});
    t.button.transition = transition{.duration = 0.10f, .curve = easing::EASE_OUT};
    set_theme(g_ctx, t);
    set_clipboard(g_ctx, sdl3_system_clipboard());
    demo_dock_init(g_state);

    g_win1 = add_window(g_ctx, g_window, g_surface, demo_window_client(g_window));
    if (g_win1) install_window_chrome(g_ctx, *g_win1); // native drag/resize/snap
    if (g_window2 && g_surface2)
        g_win2 = add_window(g_ctx, g_window2, g_surface2, demo_window_client(g_window2));
    if (g_win2) install_window_chrome(g_ctx, *g_win2);
    if (g_win1) focus_window(g_ctx, *g_win1);

    // Anchor the floating panels inside window 1's client area.
    if (g_win1)
    {
        g_state.float_home =
            rect::make(g_win1->client.x + 620.0f, g_win1->client.y + 110.0f, 260.0f, 170.0f);
        g_state.inspector_home =
            rect::make(g_win1->client.x + 560.0f, g_win1->client.y + 90.0f, 280.0f, 170.0f);
        g_state.float_bounds = g_state.float_home;
        g_state.inspector_bounds = g_state.inspector_home;
    }

    g_freq = SDL_GetPerformanceFrequency();
    g_last = SDL_GetPerformanceCounter();
    return SDL_APP_CONTINUE;
}

static pui::window *demo_core_window(SDL_WindowID id)
{
    SDL_Window *w = SDL_GetWindowFromID(id);
    return w ? window_at(g_ctx, w) : nullptr;
}

SDL_AppResult SDL_AppEvent(void *, SDL_Event *e)
{
    // The library routes every SDL event to the right window: pointer, left +
    // right buttons, wheel, keys, text input, IME preedit, focus and window
    // geometry. The demo only decides the quit policy and counts for the HUD.
    if (e->type == SDL_EVENT_MOUSE_MOTION) ++g_ev_motion;
    const bool quit = sdl3_route(g_ctx, e);
    if (quit) return SDL_APP_SUCCESS;
    if (e->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) return SDL_APP_SUCCESS;
    return SDL_APP_CONTINUE;
}
static void demo_refresh_client(pui::window &w)
{
    if (w.handle) set_window_client(w, demo_window_client(static_cast<SDL_Window *>(w.handle)));
}

static rect demo_local_rect(const pui::window &w, rect desktop)
{
    return rect::make(desktop.x - w.client.x, desktop.y - w.client.y, desktop.w, desktop.h);
}

static rect demo_desktop_rect(const pui::window &w, rect local)
{
    return rect::make(local.x + w.client.x, local.y + w.client.y, local.w, local.h);
}

static bool demo_intersects(const pui::window &w, rect desktop)
{
    const rect l = demo_local_rect(w, desktop);
    return l.right() > 0.0f && l.x < w.area.w && l.bottom() > 0.0f && l.y < w.area.h;
}

static void demo_draw_panel_content(ui &u, uiid panel, rect pc, bool /*active*/,
                                    const demo_state::view_t &v, demo_state &s)
{
    const theme &th = u.th();
    const rect inner = pc.pad(8.0f);
    if (panel == "stats"_id)
    {
        column sc(inner, 4.0f);
        u.text(sc.next(18.0f), v.clicks_label, th.text, ALIGN_LEFT);
        // Dock layout persistence: Save serializes the tree, Load restores it.
        row lr(sc.remaining(), 6.0f);
        if (u.button(lr.next(80.0f), "Save", "dock_save"_id))
            s.layout_saved = dock_save_tree(s.dock_root);
        if (u.button(lr.next(80.0f), "Load", "dock_load"_id) && !s.layout_saved.empty())
        {
            static char pool_storage[sizeof(dock_node) * 16];
            dock_node *nodes = reinterpret_cast<dock_node *>(pool_storage);
            std::string names;
            dock_node *root = nullptr;
            if (dock_restore_tree(s.layout_saved, nodes, 16, names, root) && root)
                s.dock_root = *root;
        }
        const char *note = "Drag a tab onto another panel to dock it there.";
        const measure_size m = u.measure_text(note, inner.w);
        u.text_fit(sc.next(m.height), note, th.text_dim);
    }
    else if (panel == "log"_id)
    {
        // scrollable log list (wheel + draggable bar)
        scroll_view sv =
            u.scroll(inner, "log_scroll"_id, scroll_options{4.0f, SCROLL_ALWAYS_RESERVE_BAR});
        column lc(sv.content(), 4.0f);
        char line[64];
        for (i32 i = 0; i < 24; ++i)
        {
            std::snprintf(line, sizeof(line), "log line %02d - dock, drag, wheel", i + 1);
            u.text(lc.next(16.0f), line, i == 0 ? th.text : th.text_dim, ALIGN_LEFT);
        }
        sv.set_content_height(24.0f * 16.0f + 23.0f * 4.0f);
    }
    else
    {
        const char *notes = "Window 2's dock leaf. Tabs dragged here stay here.";
        const measure_size m = u.measure_text(notes, inner.w);
        u.text_fit(rect::make(inner.x, inner.y, inner.w, m.height), notes, th.text_dim);
    }
}

// Floating panels live in desktop space and are drawn by whichever window they
// overlap; dragging their titlebar moves them from one window to the other. The
// panel writes the model bounds directly (write-back); the view's derived name is
// read for the title.
static void demo_draw_float_panels(ui &u, pui::window &w, const demo_state::view_t &v,
                                   demo_state &s)
{
    const theme &th = u.th();

    if (s.float_panel != 0 && demo_intersects(w, s.float_bounds))
    {
        rect local = demo_local_rect(w, s.float_bounds);
        panel_scope fp = u.panel("Undocked", local, PANEL_NONE, s.float_panel, v.float_name);
        if (fp.close_requested)
        {
            s.float_panel = 0;
        }
        else
        {
            column fc(fp.content(), 4.0f);
            u.text(fc.next(18.0f), v.float_name, th.text, ALIGN_LEFT);
            u.text(fc.next(16.0f), "Dropped outside the dock area.", th.text_dim, ALIGN_LEFT);
        }
        s.float_bounds = demo_desktop_rect(w, local);
    }

    // The inspector slides/fades with a spring; the slide is visual only, so the
    // model keeps the fully-open bounds.
    const f32 open_t = u.animate("inspector_t"_id, s.inspector_open ? 1.0f : 0.0f,
                                 spring{.stiffness = 260.0f, .damping_ratio = 0.7f});
    if (open_t > 0.01f && demo_intersects(w, s.inspector_bounds))
    {
        rect local = demo_local_rect(w, s.inspector_bounds);
        const f32 slide = (1.0f - open_t) * 26.0f;
        // alpha = 1 when the inspector is open: the blurred copy replaces the
        // content behind it (a lower alpha would leave it partially readable).
        u.blur(rect::make(local.x, local.y - slide, local.w, local.h), 12.0f, 6.0f, open_t);
        rect shown = local;
        shown.y -= slide;
        pui::panel_scope p =
            u.panel("Inspector", shown,
                    {.id = "inspector"_id,
                     .style = panel_override{.bg = some(color{28, 32, 40, 150}),
                                             .titlebar_bg = some(color{20, 24, 30, 170})}});
        if (p.close_requested)
        {
            s.inspector_open = false;
        }
        else
        {
            pui::column pc(p.content(), 6.0f);
            u.text(pc.next(18.0f), "Draggable inspector", th.text, ALIGN_LEFT);
            if (u.button(pc.next(28.0f), "Do thing", "panel_btn"_id, "primary"_id)) s.clicks += 1;
            u.number_field(pc.next(26.0f), s.amount, "panel_amount"_id, "%.2f");
        }
        shown.y += slide;
        s.inspector_bounds = demo_desktop_rect(w, shown);
    }
}

static dock_action demo_frame_main(pui::window &w, const demo_state::view_t &v, demo_state &s,
                                   f64 now, f64 dt)
{
    dock_action out;
    begin_frame(g_ctx, w, now, dt);
    {
        ui u(g_ctx);
        const theme &th = u.th();
        const rect screen = w.area;

        // Window chrome for the borderless window: custom titlebar + edges.
        rect chrome = screen;
        (void)u.titlebar(w, chrome, "PufferUI (next) - window 1");

        region root = u.region(chrome, "root"_id);
        u.draw_rect(root.area(), th.bg);

        // A local content rect that the cuts below actually mutate. (Cutting a
        // temporary returned by content() would silently discard the cut.)
        rect content = root.content();

        // header: title on the left, actions pinned right — they cannot overlap
        rect header = content.cut_top(46.0f);
        u.draw_rect(header, th.panel_bg);
        u.draw_line(header.left(), header.bottom(), header.right(), header.bottom(), th.border,
                    1.0f);
        {
            row header_row(header.pad(th.padding, 0.0f), 8.0f);

            rect mbtn = header_row.cut_right(108.0f);
            if (u.button(mbtn, "Menu", "demo_menu"_id)) s.popup_open = true;

            rect ibtn = header_row.cut_right(108.0f);
            if (u.button(ibtn, "Inspector", "demo_inspector"_id))
                s.inspector_open = !s.inspector_open;

            rect dbtn = header_row.cut_right(108.0f);
            if (u.button(dbtn, "Danger", "demo_danger"_id, "danger"_id)) s.clicks += 1;

            rect btn = header_row.cut_right(116.0f);
            if (u.button(btn, "Click me", "demo_button"_id, "primary"_id)) s.clicks += 1;

            {
                text_scope title = u.text_style(17.0f, g_font_bold);
                u.text_ellipsis(header_row.remaining(),
                                "PufferUI (next) - text phase, trimmed when narrow", th.text,
                                ALIGN_LEFT);
            }
        }

        // a tab row
        rect body = content.pad(th.padding);
        column col(body, th.spacing);

        {
            static const char *tab_names[4] = {"File", "Edit", "View", "Help"};
            row tabs(col.next(30.0f), 6.0f);
            for (i32 i = 0; i < 4; ++i)
            {
                rect r = tabs.next(110.0f);
                uiid id = id_child(root.id(), 1000ull + static_cast<uiid>(i));
                interaction in = u.interact(id, r);
                color c = in.held ? th.widget_active : in.hovered ? th.accent : th.panel_bg;
                u.draw_rect(r, c);
                u.text(r, tab_names[i], in.hovered || in.held ? th.bg : th.text, ALIGN_CENTER);
                if (in.hovered) g_hover = i;
                if (in.clicked) s.clicks += 1;
            }
        }

        // typed inputs: write-back straight into the model (track-sized row)
        {
            track_row form(col.next(30.0f),
                           {track_size::fixed(64.0f), track_size::flex(2.0f),
                            track_size::fixed(70.0f), track_size::fixed(120.0f)},
                           8.0f);
            u.text(form.next(), "Name", th.text, ALIGN_LEFT);
            u.text_field(form.next(), s.name, "name_field"_id);
            u.text(form.next(), "Amount", th.text, ALIGN_LEFT);
            u.number_field(form.next(), s.amount, "amount_field"_id, "%.2f");
        }

        // parity widgets: checkbox and slider write straight into the model
        {
            row form(col.next(30.0f), 8.0f);
            (void)u.checkbox(form.next(150.0f), "Inspector", s.inspector_open, "demo_cb"_id);
            u.slider_float(form.next(200.0f), "Amount", s.amount, 0.0f, 10.0f, "demo_slider"_id,
                           "%.2f");
        }

        // a responsive grid of interactive cards (auto-fit: reflows with width)
        {
            const i32 count = 9;
            const f32 gap = 8.0f;
            const f32 cell_h = 78.0f;
            const grid_cursor probe =
                auto_fit_grid(rect::make(body.x, body.y, body.w, 0.0f), count, 150.0f, cell_h, gap);
            const f32 grid_h =
                static_cast<f32>(probe.rows()) * cell_h + static_cast<f32>(probe.rows() - 1) * gap;
            grid_cursor g = auto_fit_grid(col.next(grid_h), count, 150.0f, cell_h, gap);
            for (i32 i = 0; i < g.count(); ++i)
            {
                const rect cell = g.next();
                uiid id = id_child(root.id(), 2000ull + static_cast<uiid>(i));
                interaction in = u.interact(id, cell);
                color c = in.held ? th.widget_active : in.hovered ? th.widget_hover : th.panel_bg;
                u.draw_rect(cell, c);
                // accent stripe that lights up on hover
                if (in.hovered || in.held)
                    u.draw_rect(rect::make(cell.x, cell.y, cell.w, 4.0f), th.accent);

                char label[24];
                std::snprintf(label, sizeof(label), "Card %d", i + 1);
                u.text(cell.pad(8.0f, 6.0f), label, th.text, ALIGN_LEFT);

                if (in.clicked) s.clicks += 1;
            }
        }

        // a progress bar fed by clicks, drawn in the remaining space
        {
            col.space();
            rect bar = col.next(20.0f);
            const f32 frac = static_cast<f32>(v.clicks % 20) / 20.0f;
            u.progress_bar(bar, frac, th.accent, th.widget_bg);
        }

        // docked workspace in the space left below the grid; a structural move is
        // reported as a dock_action and applied after the frames
        {
            col.space(6.0f);
            rect dock_area = col.remaining();
            if (dock_area.h > 80.0f)
            {
                dock_action act =
                    u.dock_space("demo_dock"_id, dock_area, s.dock_root,
                                 function_ref<void(uiid, rect, bool)>(
                                     [&](uiid panel, rect pc, bool active)
                                     { demo_draw_panel_content(u, panel, pc, active, v, s); }));
                if (act.active) out = act;
            }
        }

        // floating panels (undocked tabs + the inspector), drawn before the popup
        demo_draw_float_panels(u, w, v, s);

        // popup overlay: drawn last so it sits on top; input capture uses the
        // previous frame's popup (one-frame latency, documented).
        if (s.popup_open)
        {
            rect menu = {screen.center_x() - 110.0f, screen.center_y() - 70.0f, 220.0f, 150.0f};
            popup_scope p = u.popup("demo_popup"_id, menu,
                                    POPUP_CLOSE_ON_CLICK_OUTSIDE | POPUP_CLOSE_ON_ESCAPE);
            if (p.close_requested)
            {
                s.popup_open = false;
            }
            else
            {
                // scoped restyle: square corners for everything in the popup
                style_scope sc(u, button_override{.radius = some(0.0f)});
                u.draw_rounded_rect(menu, th.panel_bg, 6.0f);
                u.draw_rect(rect::make(menu.x, menu.y, menu.w, 2.0f), th.accent);
                u.text(rect::make(menu.x, menu.y + 6.0f, menu.w, 22.0f), "Menu", th.text,
                       ALIGN_CENTER);

                column items(menu.pad(12.0f, 32.0f, 12.0f, 12.0f), 6.0f);
                if (u.button(items.next(28.0f), "Action one", "popup_a"_id, "primary"_id))
                {
                    s.clicks += 1;
                    s.popup_open = false;
                }
                if (u.button(items.next(28.0f), "Action two", "popup_b"_id))
                {
                    s.clicks += 1;
                    s.popup_open = false;
                }
                if (u.button(items.next(28.0f), "Close", "popup_c"_id, "danger"_id))
                    s.popup_open = false;
            }
        }

        // performance HUD, drawn on top of everything in this window
        demo_draw_frame_hud(u, w);
    }
    end_frame(g_ctx);
    return out;
}

// Window 2: its own root region, focus list, dock tree and text field; the
// device and the glyph atlas are shared with window 1.
static dock_action demo_frame_second(pui::window &w, const demo_state::view_t &v, demo_state &s,
                                     f64 now, f64 dt)
{
    dock_action out;
    begin_frame(g_ctx, w, now, dt);
    {
        ui u(g_ctx);
        const theme &th = u.th();
        const rect screen = w.area;

        rect chrome = screen;
        (void)u.titlebar(w, chrome, "PufferUI (next) - window 2");

        region root = u.region(chrome, id_child(w.root, "window2"_id));
        u.draw_rect(root.area(), th.bg);

        rect content = root.content();
        rect header = content.cut_top(42.0f);
        u.draw_rect(header, th.panel_bg);
        u.text(header.pad(th.padding, 0.0f), "Window 2 - shared device, shared glyph atlas",
               th.text, ALIGN_LEFT);
        u.draw_line(header.left(), header.bottom(), header.right(), header.bottom(), th.border,
                    1.0f);

        rect body = content.pad(th.padding);
        column col(body, th.spacing);

        u.text_fit(col.next(34.0f),
                   "Drag the floating panel (or a dock tab) from window 1 over here.", th.text_dim);

        if (u.button(col.next(28.0f), "Add score", id_child(w.root, "second_btn"_id), "primary"_id))
            s.clicks += 1;

        {
            row form(col.next(30.0f), 8.0f);
            u.text(form.next(70.0f), "Name", th.text, ALIGN_LEFT);
            u.text_field(form.next(180.0f), s.name2, id_child(w.root, "second_name"_id));
        }

        col.space(6.0f);
        rect dock_area = col.remaining();
        if (dock_area.h > 60.0f)
        {
            dock_action act =
                u.dock_space(id_child(w.root, "dock"_id), dock_area, s.dock_second,
                             function_ref<void(uiid, rect, bool)>(
                                 [&](uiid panel, rect pc, bool active)
                                 { demo_draw_panel_content(u, panel, pc, active, v, s); }));
            if (act.active) out = act;
        }

        demo_draw_float_panels(u, w, v, s);
        demo_draw_frame_hud(u, w);
    }
    end_frame(g_ctx);
    return out;
}

SDL_AppResult SDL_AppIterate(void *)
{
    const u64 counter = SDL_GetPerformanceCounter();
    const f64 now = static_cast<f64>(counter) / static_cast<f64>(g_freq);
    const f64 dt = static_cast<f64>(counter - g_last) / static_cast<f64>(g_freq);
    g_last = counter;

    if (!g_win1) return SDL_APP_SUCCESS;

    // Windows may have moved/resized; refresh the desktop client rects.
    demo_refresh_client(*g_win1);
    if (g_win2) demo_refresh_client(*g_win2);

    // Pointer motion is fed from window-local mouse events (see SDL_AppEvent):
    // SDL event/window coordinates share one scaled space, while
    // SDL_GetGlobalMouseState returns raw unscaled screen pixels and would
    // disagree on displays with a content scale != 1.

    // Frame shape: state.update_view(); draw(ui, view, state). Components read the
    // per-frame view snapshot for display data and write state fields directly.
    g_state.update_view();

    const u64 ui_start = SDL_GetPerformanceCounter();
    dock_action dock1 = demo_frame_main(*g_win1, g_state.view, g_state, now, dt);
    dock_action dock2{};
    if (g_win2) dock2 = demo_frame_second(*g_win2, g_state.view, g_state, now, dt);
    const u64 ui_end = SDL_GetPerformanceCounter();
    const f64 ui_ms = static_cast<f64>(ui_end - ui_start) * 1000.0 / static_cast<f64>(g_freq);
    demo_record_frame_time(dt, ui_ms);
    g_ev_motion = 0;

    // Structural dock moves reported during the frames are applied here.
    if (dock1.active) demo_dock_apply(g_state, dock1);
    if (dock2.active) demo_dock_apply(g_state, dock2);

    // The custom titlebar's close button only sets the window's close request;
    // the run loop turns it into action (mirrors the OS chrome's policy):
    // closing window 1 quits, closing window 2 removes it from the dock set.
    // remove_window refuses while a frame is open; both frames are closed here.
    if (g_win1->close_requested) return SDL_APP_SUCCESS;
    if (g_win2 && g_win2->close_requested)
    {
        remove_window(g_ctx, *g_win2);
        g_win2 = nullptr;
        // remove_window compacts the context's window array: re-fetch window 1
        // (the pointer may have shifted), and hide the SDL window - its SDL
        // resources are freed by SDL_AppQuit.
        g_win1 = g_window ? window_at(g_ctx, g_window) : nullptr;
        if (g_window2) SDL_HideWindow(g_window2);
    }

    // Only touch the window title when it actually changes: SetWindowText is a
    // per-frame OS call once the condition is true, and it shows up at high
    // frame rates.
    char title[128];
    if (g_state.clicks != 0)
        std::snprintf(title, sizeof(title), "PufferUI (next) - clicks: %d%s", g_state.clicks,
                      g_hover >= 0 ? "  [tab]" : "");
    else
        std::snprintf(title, sizeof(title), "PufferUI (next) - window 1");
    if (g_title_last != title)
    {
        g_title_last = title;
        SDL_SetWindowTitle(g_window, title);
    }

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *, SDL_AppResult)
{
    if (g_ctx) destroy_context(g_ctx);
    if (g_device) destroy_sdl3_device(g_device); // frees surfaces + window-2 renderer
    if (g_renderer) SDL_DestroyRenderer(g_renderer);
    if (g_window2) SDL_DestroyWindow(g_window2);
    if (g_window) SDL_DestroyWindow(g_window);
}