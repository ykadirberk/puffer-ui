// pui_counter.cpp - a small, complete state/view example.
//
// Frame shape:
//     state.update_view();                         // 1. derive the view (once)
//     begin_frame(ctx, win, now, dt);              // 2. build
//       draw_counter(u, state.view, state);        //    read the view, write state
//     end_frame(ctx);
//
// Model fields are updated directly; the view is a per-frame snapshot, so every
// component sees one consistent set of derived values.
//
// Build target: pui_counter (SDL3 + the new core). See docs/model_view.md.

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <pufferui/pufferui.h>

#include <cstdio>
#include <string>

using namespace pui;

// ---------------------------------------------------------------- state
// One app-owned object: model fields are written directly, and `update_view()`
// derives the view data once per frame (then it is only read).
struct counter_state
{
    // model
    int count = 0;
    std::string name = "world";
    bool show_count = false;

    // view: derived once per frame
    struct view_t
    {
        std::string greeting;
        std::string count_label;
    } view;

    void update_view()
    {
        view.greeting = "Hello, " + name + "!";
        view.count_label = "count = " + std::to_string(count);
    }
};

// ---------------------------------------------------------------- component
// Reads the view snapshot, writes model fields directly.
static void draw_counter(ui &u, window &w, const counter_state::view_t &v, counter_state &s)
{
    const theme &th = u.th();

    rect chrome = w.area;
    (void)u.titlebar(w, chrome, "PufferUI - counter (model/view example)");

    region root = u.region(chrome, "root"_id);
    u.draw_rect(root.area(), th.bg);

    column col(root.content().pad(16.0f), 10.0f);

    u.text(col.next(24.0f), v.greeting, th.text, ALIGN_LEFT);

    {
        row r(col.next(30.0f), 8.0f);
        if (u.button(r.next(90.0f), "-1", "dec"_id)) s.count -= 1;
        if (u.button(r.next(90.0f), "+1", "inc"_id, "primary"_id)) s.count += 1;
        if (u.button(r.next(150.0f), s.show_count ? "Hide count" : "Show count", "toggle"_id))
            s.show_count = !s.show_count;
    }

    {
        row r(col.next(28.0f), 8.0f);
        u.text(r.next(56.0f), "Name", th.text, ALIGN_LEFT);
        u.text_field(r.next(200.0f), s.name, "name_field"_id); // write-back
    }

    if (s.show_count) u.text(col.next(24.0f), v.count_label, th.accent, ALIGN_LEFT);
}

// ---------------------------------------------------------------- SDL glue
static SDL_Window *g_window = nullptr;
static SDL_Renderer *g_renderer = nullptr;
static render_device *g_device = nullptr;
static render_surface *g_surface = nullptr;
static context *g_ctx = nullptr;
static window *g_win = nullptr;
static u64 g_freq = 1, g_last = 0;
static counter_state g_state;

static rect window_client(SDL_Window *w)
{
    int x = 0, y = 0, ww = 0, wh = 0;
    SDL_GetWindowPosition(w, &x, &y);
    SDL_GetWindowSize(w, &ww, &wh);
    return rect::make(static_cast<f32>(x), static_cast<f32>(y), static_cast<f32>(ww),
                      static_cast<f32>(wh));
}

SDL_AppResult SDL_AppInit(void **, int, char **)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) return SDL_APP_FAILURE;

    g_window = SDL_CreateWindow("PufferUI - counter (model/view example)", 520, 260,
                                SDL_WINDOW_BORDERLESS | SDL_WINDOW_RESIZABLE);
    if (!g_window) return SDL_APP_FAILURE;
    g_renderer = SDL_CreateRenderer(g_window, nullptr);
    if (!g_renderer) return SDL_APP_FAILURE;

    g_device = create_sdl3_device(g_renderer);
    if (!g_device) return SDL_APP_FAILURE;
    g_surface = g_device->create_surface(g_window);

    SDL_StartTextInput(g_window);

    g_ctx = create_context(g_device, g_surface);

    font_handle font = load_font(g_ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
    if (font == FONT_INVALID) font = load_font(g_ctx, "assets/fonts/DejaVuSans.ttf");
    if (font == FONT_INVALID) font = load_font(g_ctx, "C:/Windows/Fonts/segoeui.ttf");
    if (font == FONT_INVALID) font = load_font(g_ctx, "C:/Windows/Fonts/arial.ttf");
    theme t = default_dark();
    t.font = font;
    t.text_size = 16.0f;
    set_button_role(t, "primary"_id,
                    button_override{.bg = some(color{40, 110, 200, 255}),
                                    .hover_bg = some(color{60, 140, 230, 255})});
    set_theme(g_ctx, t);

    g_win = add_window(g_ctx, g_window, g_surface, window_client(g_window));
    if (g_win) install_window_chrome(g_ctx, *g_win); // native drag/resize/snap
    if (!g_win) return SDL_APP_FAILURE;
    focus_window(g_ctx, *g_win);

    g_freq = SDL_GetPerformanceFrequency();
    g_last = SDL_GetPerformanceCounter();
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *, SDL_Event *e)
{
    // The library routes every SDL event to the right window; the demo only
    // decides the quit policy.
    const bool quit = sdl3_route(g_ctx, e);
    if (quit) return SDL_APP_SUCCESS;
    if (e->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) return SDL_APP_SUCCESS;
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *)
{
    const u64 counter = SDL_GetPerformanceCounter();
    const f64 now = static_cast<f64>(counter) / static_cast<f64>(g_freq);
    const f64 dt = static_cast<f64>(counter - g_last) / static_cast<f64>(g_freq);
    g_last = counter;

    set_window_client(*g_win, window_client(g_window)); // keep the desktop rect fresh

    // 1. derive the view once per frame
    g_state.update_view();

    // 2. build: components read the view snapshot and write model fields directly
    begin_frame(g_ctx, *g_win, now, dt);
    {
        ui u(g_ctx);
        draw_counter(u, *g_win, g_state.view, g_state);
    }
    end_frame(g_ctx);

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *, SDL_AppResult)
{
    if (g_ctx) destroy_context(g_ctx);
    if (g_device) destroy_sdl3_device(g_device);
    if (g_renderer) SDL_DestroyRenderer(g_renderer);
    if (g_window) SDL_DestroyWindow(g_window);
}