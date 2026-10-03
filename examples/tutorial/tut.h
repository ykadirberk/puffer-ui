// tut.h - the frame loop every tutorial step from chapter 2 on shares.
//
// Chapter 1 (step01_window.cpp) writes this loop out by hand; from then on it
// lives here so each step can show only the UI. Nothing in this file is
// special: it is the library's one-call bootstrap (`sdl3_app_*`) plus a few
// flags for testing and screenshots.
//
//     tut_app app;
//     if (!tut_init(app, "title", 520, 760, argc, argv)) return 1;
//     set_theme(app.ctx, my_theme(app.font));        // configure
//     tut_loop(app, [&](ui &u) { /* build one frame */ });
//     return tut_shutdown(app);
//
// Flags: --selftest (offscreen, scripted input, fails on any violation),
//        --frames N (selftest length), --screenshot FILE (BMP of the last frame),
//        --size WxH (initial client size).
#pragma once

#include <pufferui/pufferui.h>

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace pui;

struct tut_app
{
    sdl3_app boot;                   // window, renderer, device, context (the library's)
    context *ctx = nullptr;          // == boot.ctx
    window *win = nullptr;           // == boot.win
    font_handle font = FONT_INVALID; // the regular face
    font_handle bold = FONT_INVALID; // the bold face (falls back to `font`)
    f64 now = 0.0, dt = 1.0 / 60.0;  // the frame clock, in seconds
    i32 frame = 0;                   // frames drawn so far
    const char *title = "";
    bool selftest = false;
    i32 selftest_frames = 12;
    const char *screenshot = nullptr;
    // Optional: scripted input for --selftest, called once per frame before the
    // frame is built (feed events with mouse_move / text_input_event / ...).
    void (*script)(tut_app &app, i32 frame) = nullptr;

    f32 width() const { return win->area.w; } // the live client size
    f32 height() const { return win->area.h; }
};

// Window + renderer + device + context, the bundled fonts, and a base theme.
inline bool tut_init(tut_app &app, const char *title, i32 w, i32 h, int argc, char **argv)
{
    app.title = title;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--selftest") == 0)
            app.selftest = true;
        else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
            app.selftest_frames = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc)
            app.screenshot = argv[++i];
        else if (std::strcmp(argv[i], "--size") == 0 && i + 1 < argc)
        {
            int sw = 0, sh = 0;
            if (SDL_sscanf(argv[++i], "%dx%d", &sw, &sh) == 2 && sw > 0 && sh > 0)
            {
                w = sw;
                h = sh;
            }
        }
    }
    if (app.selftest)
    {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    }

    app.boot.hidden = app.selftest;
    app.boot.renderer_name = app.selftest ? "software" : nullptr;
    app.boot.wait_when_idle = !app.selftest; // sleep while nothing animates
    if (!sdl3_app_init(app.boot, title, w, h, argc, argv))
    {
        std::printf("FAIL %s: could not open a window\n", title);
        return false;
    }
    app.ctx = app.boot.ctx;
    app.win = app.boot.win;

    // the bootstrap loaded the regular face; add the bold one
    app.font = app.boot.font;
    app.bold = load_font(app.ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans-Bold.ttf");
    if (app.bold == FONT_INVALID) app.bold = app.font;
    theme t = default_dark();
    t.font = app.font;
    set_theme(app.ctx, t);
    if (!app.selftest) SDL_StartTextInput(static_cast<SDL_Window *>(app.boot.sdl_window));
    focus_window(app.ctx, *app.win);
    // In a debug build the library stops at the line that broke a rule (when a
    // debugger is attached; otherwise the process traps). While learning, show
    // the problem in the violation overlay instead and keep running.
    set_break_on_violation(app.ctx, false);
    return true;
}

// events -> clock -> begin_frame -> build -> end_frame -> present, until closed.
template <typename FrameFn> void tut_loop(tut_app &app, FrameFn &&build)
{
    while (sdl3_app_pump(app.boot))
    {
        if (app.selftest && app.frame >= app.selftest_frames) break;
        sdl3_app_tick(app.boot);
        app.now = app.boot.now;
        app.dt = app.boot.dt;
        if (app.selftest && app.script) app.script(app, app.frame);

        begin_frame(app.ctx, *app.win, app.now, app.dt);
        {
            ui u(app.ctx);
            build(u);
            // A contract violation (duplicate id, bad slice, ...) is shown on
            // screen instead of only logged: handy while you learn the library.
            if (!app.selftest)
                draw_violation_overlay(u, rect::make(8.0f, app.height() - 40.0f, 360.0f, 32.0f));
        }
        end_frame(app.ctx);
        app.boot.surface->present();
        ++app.frame;
    }
}

// Saves the --screenshot, destroys everything; the return value is main's.
inline int tut_shutdown(tut_app &app)
{
    if (app.screenshot)
    {
        SDL_Surface *img =
            SDL_RenderReadPixels(static_cast<SDL_Renderer *>(app.boot.sdl_renderer), nullptr);
        if (img)
        {
            SDL_SaveBMP(img, app.screenshot);
            SDL_DestroySurface(img);
        }
    }
    const i32 violations = violation_count(app.ctx);
    sdl3_app_shutdown(app.boot);
    if (app.selftest)
    {
        std::printf(violations == 0 ? "ok   %s\n" : "FAIL %s: contract violations\n", app.title);
        return violations == 0 ? 0 : 1;
    }
    return 0;
}
