// bootstrap — the library's one-call bootstrap, without the example harness.
//
// Shows: sdl3_app_init/pump/tick/shutdown (SDL window + renderer + device +
// surface + context + window in one call), the raw frame loop, the custom
// titlebar with the native chrome (system caption drag + Aero Snap + native
// edge/corner resize through install_window_chrome), a `column` cursor, a
// button counter, and the development violation overlay.
//
// This is the "start here" example: no harness, just the library.
//
//   pui_ex_bootstrap
//   pui_ex_bootstrap --selftest
#include <pufferui/pufferui.h>
#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>

using namespace pui;

// App state: model fields the widgets write directly (the state/view rule).
static i32 g_clicks = 0;

// What every frame draws. Reads state, writes model fields from widgets.
static void draw_page(ui &u, const char *subtitle)
{
    window &w = *u.ctx->current_window;
    const theme &th = u.th();
    const rect client = rect::make(0.0f, 0.0f, w.area.w, w.area.h);
    u.draw_rect(client, th.bg);

    // The custom titlebar on a borderless window: paints the bar and the
    // min/max/close buttons. Native dragging, the system's Aero Snap and
    // edge/corner resizing come from install_window_chrome (done by the
    // bootstrap).
    rect chrome = client;
    (void)u.titlebar(w, chrome, "PufferUI bootstrap");

    rect area = chrome.pad(16.0f);
    if (subtitle) u.text(area.cut_top(18.0f), subtitle, th.text_dim, ALIGN_LEFT);
    area.cut_top(8.0f);

    column col(area, 8.0f);

    // A plain button with a click counter: press + release inside the button.
    if (u.button(col.next(30.0f), "Click me", "boot_btn"_id, "primary"_id)) g_clicks += 1;

    char line[64];
    std::snprintf(line, sizeof(line), "clicks: %d", g_clicks);
    u.text(col.next(20.0f), line, th.text, ALIGN_LEFT);

    u.text_wrapped(col.next(64.0f),
                   "Drag the titlebar (the system's snap preview "
                   "appears near the edges), resize from any edge "
                   "or corner, and double-click the bar to maximize.",
                   th.text_dim);
}

int main(int argc, char **argv)
{
    bool selftest = false;
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--selftest") == 0) selftest = true;

    if (selftest)
    {
        // Offscreen + software renderer: deterministic frames without a window.
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    }
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    SDL_SetLogPriorities(SDL_LOG_PRIORITY_ERROR);

    sdl3_app app;
    if (!sdl3_app_init(app, "bootstrap", 480, 320, 0, nullptr)) return 1;

    // sdl3_app_init already loaded the bundled font (a.font) and installed a
    // working base theme; an app that wants a different look calls set_theme
    // here. This example runs as-is.
    if (selftest)
    {
        // Scripted input: hover the button, press, release — the counter moves.
        for (i32 f = 0; f < 6; ++f)
        {
            if (!sdl3_app_pump(app)) break;
            sdl3_app_tick(app);
            begin_frame(app.ctx, *app.win, app.now, app.dt);
            {
                ui u(app.ctx);
                if (f == 2) mouse_move(*app.win, 240.0f, 91.0f); // the button's center
                if (f == 3) mouse_button(*app.win, true);
                if (f == 4) mouse_button(*app.win, false);
                draw_page(u, "selftest (scripted input)");
            }
            end_frame(app.ctx);
        }
        const i32 violations = violation_count(app.ctx);
        if (violations != 0)
        {
            std::printf("FAIL bootstrap: %d violation(s) (last: %s)\n", violations,
                        violation_last(app.ctx));
            sdl3_app_shutdown(app);
            return 1;
        }
        std::printf("ok   bootstrap (clicks: %d)\n", g_clicks);
        sdl3_app_shutdown(app);
        return 0;
    }

    bool running = true;
    while (running)
    {
        if (!sdl3_app_pump(app)) running = false;
        sdl3_app_tick(app);
        begin_frame(app.ctx, *app.win, app.now, app.dt);
        {
            ui u(app.ctx);
            draw_page(u, nullptr);
            // Development overlay: shows only when the context has violations
            // (drawn last so nothing covers it).
            draw_violation_overlay(u, rect::make(8.0f, app.win->area.h - 40.0f, 360.0f, 32.0f));
        }
        end_frame(app.ctx);
        app.surface->present();
    }
    sdl3_app_shutdown(app);
    return 0;
}
