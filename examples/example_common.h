// Shared SDL3 bootstrap for the atomic examples (examples/atomic/*.cpp).
//
// An example is a standalone program:
//
//     #include "../example_common.h"
//
//     int main(int argc, char **argv)
//     {
//         return example_run("hello", 640, 360, argc, argv,
//                            [](ui &u, example_app &app) { ... });
//     }
//
// `example_run` opens a window, pumps SDL input into the core, and calls the
// frame callback between `begin_frame` / `end_frame`; it returns 0 when the user
// closes the window.
//
// Flags:
//   --selftest    run offscreen with scripted input, assert no violations, exit
//   --frames N    selftest frame count (default 6; the script cycles)
//   --size WxH    client size (overrides the size passed to example_run)
//
// `--selftest` needs no display and is what CI runs for every example. It prints
// `ok <name>` (or `FAIL <name>: ...`) and returns a non-zero exit code on any
// core violation (clip imbalance, duplicate ids, invalid slices, ...).
#pragma once

#include <pufferui/pufferui.h>

#include <SDL3/SDL.h>
#if defined(PUFFERUI_ENABLE_VULKAN)
#include <pufferui/vulkan.h>
#include <vector>
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace pui;

#ifndef PUFFERUI_ASSET_DIR
#define PUFFERUI_ASSET_DIR "assets"
#endif

// One frame of the scripted selftest input: move to (xf, yf) in client fractions,
// press (1) / release (0) / leave (-1) the left button, optionally wheel, Tab or
// type text. Kept generic on purpose: sweeping the pointer and clicking across a
// UI is enough to trip layout/clip/id violations in every example.
struct example_script_step
{
    f32 xf = 0.5f;
    f32 yf = 0.5f;
    i32 button = -1;
    f32 wheel = 0.0f;
    bool tab = false;
    const char *text = nullptr;
    i32 right = -1; // right button: press (1) / release (0) / leave (-1)
};

inline const example_script_step *example_script_steps(i32 &count)
{
    static const example_script_step steps[] = {
        {0.25f, 0.30f, -1, 0.0f, false, nullptr},    // hover
        {0.50f, 0.30f, 1, 0.0f, false, nullptr},     // press
        {0.50f, 0.30f, 1, -1.0f, true, nullptr},     // hold + wheel + tab
        {0.50f, 0.30f, 0, 0.0f, false, "ab"},        // release + type
        {0.72f, 0.70f, -1, 0.0f, false, nullptr, 1}, // right-press (context menus)
        {0.72f, 0.70f, 1, 0.0f, false, nullptr, 0},  // right-release + left press
        {0.72f, 0.70f, 0, 0.0f, false, nullptr},     // release
    };
    count = static_cast<i32>(sizeof(steps) / sizeof(steps[0]));
    return steps;
}

// ---------------------------------------------------------------- look & feel
// One visual language for every example: a page with a bold title, an optional
// subtitle, a divider and a content rect on the theme's rhythm. Examples should
// lay out from `content` and use cards/sections for grouping.
namespace example_ui
{
inline constexpr f32 PAGE_PAD = 16.0f;    // page margin
inline constexpr f32 SECTION_GAP = 12.0f; // between sections
inline constexpr f32 ROW_H = 28.0f;       // standard control height
inline constexpr f32 TITLE_SIZE = 20.0f;
inline constexpr f32 SECTION_SIZE = 15.0f;
inline constexpr f32 CAPTION_SIZE = 14.0f;
// Page header rhythm (example_begin_page).
inline constexpr f32 HEAD_H = 14.0f;     // header band without a subtitle
inline constexpr f32 HEAD_H_SUB = 34.0f; // ... with one
inline constexpr f32 SUBTITLE_H = 20.0f;
inline constexpr f32 DIVIDER_GAP = 6.0f; // header band -> divider line
inline constexpr f32 BODY_GAP = 14.0f;   // divider -> page content
} // namespace example_ui

struct example_page
{
    rect content{}; // below the header, inside the page padding
};

// A card section: bold title, dim caption, divider, then the content rect
// (the library's comp::section, with the example font).
inline rect example_section(ui &u, rect r, const char *title, const char *caption = nullptr,
                            font_handle bold = FONT_INVALID)
{
    return comp::section(u, r, title, caption ? caption : "", {.title_font = bold});
}

// A dim caption line (label for a control row).
inline void example_caption(ui &u, rect r, const char *text)
{
    u.text(r, text, u.th().text_dim, ALIGN_LEFT);
}
struct example_window
{
    SDL_Window *sdl = nullptr;
    render_surface *surface = nullptr;
    window *win = nullptr;

    explicit operator bool() const { return win != nullptr; }
};

// Optional timing for an actual interactive run. Disabled examples do not read
// a clock here. The recorder owns enabling it and the retained output storage.
struct example_frame_diagnostics
{
    enum stage
    {
        PUMP,
        BEGIN,
        END,
        COUNT
    };
    bool enabled = false;
    u64 ticks[COUNT]{};
    bool valid[COUNT]{};
    template <typename Fn> void measure(stage part, Fn &&fn)
    {
        if (!enabled)
        {
            fn();
            return;
        }
        const u64 start = SDL_GetPerformanceCounter();
        fn();
        ticks[part] = SDL_GetPerformanceCounter() - start;
        valid[part] = true;
    }
};
struct example_app
{
    const char *name = "example";
    // The prebuilt library does not inherit this executable's asset macro.
    // Pass it across the bootstrap boundary so the working directory cannot
    // silently select a different system font.
    sdl3_app boot{.asset_dir = PUFFERUI_ASSET_DIR};
    SDL_Window *sdl_window = nullptr;
    SDL_Renderer *renderer = nullptr;
    render_device *device = nullptr;
    render_surface *surface = nullptr;
    context *ctx = nullptr;
    window *win = nullptr;

    font_handle font = FONT_INVALID;
    font_handle font_bold = FONT_INVALID;
    font_handle font_oblique = FONT_INVALID;

    // The overlay default: interactive runs watch for contract violations,
    // selftests assert zero of them (so the overlay can never show there).
    bool selftest = false;
    bool show_violations = false;
    i32 selftest_frames = 6;
    i32 frame_index = 0;
    i32 width = 640;
    i32 height = 360;
    bool running = true;
    example_frame_diagnostics diagnostics;
    f64 now = 0.0;
    f64 dt = 1.0 / 60.0;

    // extra windows (events are routed to them by SDL window id)
    static constexpr i32 MAX_EXTRAS = 4;
    example_window extras[MAX_EXTRAS]{};
    i32 extra_count = 0;

    // Optional: `example_run` frames each extra window with this callback
    // (index is 1-based: 1 is the first extra window).
    void (*extra_frame)(ui &, example_app &, i32 index) = nullptr;
    // App-owned native resources must be released before the bootstrap device.
    void (*before_shutdown)(example_app &, void *user) = nullptr;
    void *shutdown_user = nullptr;

    // `--screenshot <file>`: save a frame as a BMP (with --selftest: the last
    // frame; otherwise after a few frames).
    const char *screenshot = nullptr;
    bool help = false;
    // `--no-script`: a selftest without the scripted input (clean screenshots:
    // nothing pressed, scrolled or typed; the pointer stays at the corner).
    bool no_script = false;
    // `--resize WxH`: resize the window after the first selftest frame, so the
    // re-layout path is exercised (and screenshotted) headlessly.
    i32 resize_w = 0, resize_h = 0;
};

// Client rect of an SDL window in desktop coordinates (what `add_window` wants).
inline rect example_client(SDL_Window *w)
{
    int x = 0, y = 0, ww = 0, wh = 0;
    SDL_GetWindowPosition(w, &x, &y);
    SDL_GetWindowSize(w, &ww, &wh);
    return rect::make(static_cast<f32>(x), static_cast<f32>(y), static_cast<f32>(ww),
                      static_cast<f32>(wh));
}

// Keep `example_app::width/height` (what examples lay out with) and the core
// window's client in sync with the real window. Called on resize/move events, so
// examples re-layout instead of keeping their startup size.
inline void example_sync_client(example_app &app)
{
    if (app.sdl_window)
    {
        int ww = 0, wh = 0;
        SDL_GetWindowSize(app.sdl_window, &ww, &wh);
        if (ww > 0 && wh > 0)
        {
            app.width = ww;
            app.height = wh;
        }
        if (app.win)
        {
            set_window_client(*app.win, example_client(app.sdl_window));
            app.width = static_cast<i32>(app.win->area.w);
            app.height = static_cast<i32>(app.win->area.h);
        }
    }
    for (i32 i = 0; i < app.extra_count; ++i)
    {
        example_window &ew = app.extras[i];
        if (ew.sdl && ew.win) set_window_client(*ew.win, example_client(ew.sdl));
    }
}

// Page chrome: call at the top of a frame, then lay out from `page.content`.
inline example_page example_begin_page(ui &u, example_app &app, const char *title,
                                       const char *subtitle = nullptr)
{
    const theme &th = u.th();
    const rect client =
        rect::make(0.0f, 0.0f, static_cast<f32>(app.width), static_cast<f32>(app.height));
    u.draw_rect(client, th.bg);

    example_page page;

    // Window chrome: the custom titlebar (drag, min/max/close) replaces the OS
    // decoration on borderless windows, plus the invisible edge-resize strips.
    // The close request is handled by the run loop (primary quits, extras are
    // removed), so the page does not care which window this is.
    window &w = *u.current_window();
    rect chrome = client;
    (void)u.titlebar(w, chrome, title);

    rect area = chrome.pad(example_ui::PAGE_PAD);
    rect head = area.cut_top(subtitle ? example_ui::HEAD_H_SUB : example_ui::HEAD_H);
    if (subtitle) u.text(head.cut_top(example_ui::SUBTITLE_H), subtitle, th.text_dim, ALIGN_LEFT);
    const f32 line_y = head.bottom() + example_ui::DIVIDER_GAP;
    u.draw_line(area.left(), line_y, area.right(), line_y, th.border, 1.0f);
    (void)area.cut_top(example_ui::BODY_GAP);
    page.content = area;
    return page;
}

inline bool example_environment_flag(const char *value)
{
    return value && value[0] && SDL_strcasecmp(value, "0") != 0 &&
           SDL_strcasecmp(value, "false") != 0 && SDL_strcasecmp(value, "off") != 0 &&
           SDL_strcasecmp(value, "no") != 0;
}

inline bool example_init(example_app &app, const char *title, i32 w, i32 h, int argc, char **argv)
{
    app.name = title;
    app.width = w;
    app.height = h;
    app.show_violations = true; // interactive dev runs watch; selftests clear it
#if defined(PUFFERUI_ENABLE_VULKAN)
    bool explicit_painter = false;
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], "--vulkan-painter") == 0) explicit_painter = true;
    if (explicit_painter)
    {
        // A diagnostic override starts from independent features. Preserve
        // unrelated caller policy such as budgets, uploads, blur and validation.
        app.boot.vulkan.tile_cache = app.boot.vulkan.whole_program_cache = false;
        app.boot.vulkan.vector_text = app.boot.vulkan.adaptive_compute = false;
    }
#endif
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--selftest") == 0)
        {
            app.selftest = true;
            app.show_violations = false; // asserts zero violations: nothing to show
        }
        else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
            app.selftest_frames = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--size") == 0 && i + 1 < argc)
        {
            int sw = 0, sh = 0;
            if (SDL_sscanf(argv[++i], "%dx%d", &sw, &sh) == 2 && sw > 0 && sh > 0)
            {
                app.width = sw;
                app.height = sh;
            }
        }
        else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc)
            app.screenshot = argv[++i];
        else if (std::strcmp(argv[i], "--resize") == 0 && i + 1 < argc)
        {
            int rw = 0, rh = 0;
            if (SDL_sscanf(argv[++i], "%dx%d", &rw, &rh) == 2 && rw > 0 && rh > 0)
            {
                app.resize_w = rw;
                app.resize_h = rh;
            }
        }
        else if (std::strcmp(argv[i], "--violations") == 0)
            app.show_violations = true;
        else if (std::strcmp(argv[i], "--no-script") == 0)
            app.no_script = true;
        else if (std::strcmp(argv[i], "--help") == 0)
            app.help = true;
#if defined(PUFFERUI_ENABLE_VULKAN)
        else if (std::strcmp(argv[i], "--vulkan-painter") == 0 && i + 1 < argc)
        {
            const char *mode = argv[++i];
            app.boot.vulkan.adaptive_compute = false;
            if (std::strcmp(mode, "reference") == 0)
                app.boot.vulkan.painter = vulkan_painter_mode::REFERENCE;
            else if (std::strcmp(mode, "sdf") == 0)
                app.boot.vulkan.painter = vulkan_painter_mode::SDF_RASTER;
            else if (std::strcmp(mode, "compute") == 0)
                app.boot.vulkan.painter = vulkan_painter_mode::COMPUTE_SDF;
            else if (std::strcmp(mode, "integrated") == 0)
            {
                app.boot.vulkan.painter = vulkan_painter_mode::INTEGRATED;
                app.boot.vulkan.tile_cache = true;
#if defined(PUFFERUI_ENABLE_HARFBUZZ)
                app.boot.vulkan.vector_text = true;
#endif
            }
            else if (std::strcmp(mode, "adaptive") == 0)
            {
                app.boot.vulkan.painter = vulkan_painter_mode::COMPUTE_SDF;
                app.boot.vulkan.tile_cache = app.boot.vulkan.adaptive_compute = true;
            }
            else
            {
                std::fprintf(stderr, "Unknown Vulkan painter: %s\n", mode);
                return false;
            }
            app.boot.vulkan.whole_program_cache =
                (app.boot.vulkan.painter == vulkan_painter_mode::SDF_RASTER ||
                 app.boot.vulkan.painter == vulkan_painter_mode::COMPUTE_SDF ||
                 app.boot.vulkan.painter == vulkan_painter_mode::INTEGRATED) &&
                !app.boot.vulkan.adaptive_compute;
        }
        else if (std::strcmp(argv[i], "--vector-text") == 0)
            app.boot.vulkan.vector_text = true;
        else if (std::strcmp(argv[i], "--vulkan-blur") == 0 && i + 1 < argc)
        {
            const char *mode = argv[++i];
            if (std::strcmp(mode, "kawase") == 0)
                app.boot.vulkan.blur = vulkan_blur_mode::KAWASE;
            else if (std::strcmp(mode, "legacy") == 0)
                app.boot.vulkan.blur = vulkan_blur_mode::LEGACY;
            else
                return false;
        }
#endif
    }

    if (app.help)
    {
        std::printf(
            "%s - a PufferUI example\n"
            "  --selftest            run offscreen with scripted input, assert no violations\n"
            "  --frames N            selftest frame count (default 6)\n"
            "  --size WxH            client size\n"
            "  --resize WxH          resize mid-selftest (exercises re-layout)\n"
            "  --screenshot FILE     save a frame as a BMP\n"
            "  --violations          draw the violation overlay each frame\n"
            "  --no-script           selftest without the scripted input (screenshots)\n",
            title);
#if defined(PUFFERUI_ENABLE_VULKAN)
        std::printf("  --vulkan-painter reference|sdf|compute|adaptive|integrated  select the "
                    "native painter\n"
                    "  --vector-text         HarfBuzz shaping and Slug GPU text (optional build)\n"
                    "  --vulkan-blur legacy|kawase  select native blur filtering\n");
#endif
        return false;
    }

#if !defined(PUFFERUI_ENABLE_VULKAN)
    if (app.selftest)
    {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    }
#endif
    SDL_SetLogPriorities(SDL_LOG_PRIORITY_ERROR);

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::printf("FAIL %s: SDL_Init: %s\n", app.name, SDL_GetError());
        return false;
    }

    // Library bootstrap: SDL window + renderer + device + surface + context +
    // window with the native chrome. Fonts and the theme stay harness-side.
    sdl3_app &boot = app.boot;
    boot.hidden = app.selftest;
    boot.renderer_name = app.selftest ? "software" : nullptr;
#if defined(PUFFERUI_ENABLE_VULKAN)
    boot.use_vulkan = true;
    boot.vulkan.validation = example_environment_flag(std::getenv("PUFFERUI_VULKAN_VALIDATION"));
#endif
    if (!sdl3_app_init(boot, title, app.width, app.height, 0, nullptr))
    {
        std::printf("FAIL %s: sdl3_app_init\n", app.name);
        return false;
    }
    app.sdl_window = static_cast<SDL_Window *>(boot.sdl_window);
    app.renderer = static_cast<SDL_Renderer *>(boot.sdl_renderer);
    app.device = boot.device;
    app.surface = boot.surface;
    app.ctx = boot.ctx;
    app.win = boot.win;
    app.font = boot.font;     // the bootstrap loaded the bundled (or fallback) font
    example_sync_client(app); // adopt the window's real client size
    // Interactive runs keep the device default (vsync on); an offscreen
    // selftest has no display to wait for, so it runs uncapped.
    if (app.selftest) app.device->set_vsync(false);

    // Bold/oblique keep examples (and screenshots) deterministic; they reuse
    // the base font when the bundled variants are missing.
    app.font_bold = load_font(app.ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans-Bold.ttf");
    app.font_oblique = load_font(app.ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans-Oblique.ttf");
    if (app.font_bold == FONT_INVALID) app.font_bold = app.font;
    if (app.font_oblique == FONT_INVALID) app.font_oblique = app.font;

    // The harness's shared look over the bootstrap's base theme.
    theme t = default_dark();
    t.font = app.font;
    t.text_size = 15.0f;
    set_button_role(t, "primary"_id,
                    button_override{.bg = some(color{40, 110, 200, 255}),
                                    .hover_bg = some(color{60, 140, 230, 255})});
    set_button_role(t, "danger"_id,
                    button_override{.bg = some(color{180, 50, 50, 255}),
                                    .hover_bg = some(color{210, 70, 70, 255})});
    t.button.transition = transition{.duration = 0.10f, .curve = easing::EASE_OUT};
    set_theme(app.ctx, t);
    focus_window(app.ctx, *app.win);
    if (!app.selftest) SDL_StartTextInput(app.sdl_window);
    return true;
}

// An extra window over the same device (the `windows` example): the library's
// sdl3_app_open_window (SDL window + surface + core window + chrome + text
// input), registered here so the run loop frames it and closes it on request.
inline example_window *example_add_window(example_app &app, const char *title, i32 w, i32 h)
{
    if (app.extra_count >= example_app::MAX_EXTRAS) return nullptr;
    window *win = sdl3_app_open_window(app.boot, title, w, h);
    if (!win)
    {
        std::printf("note: %s: second window unavailable: %s\n", app.name, SDL_GetError());
        return nullptr;
    }
    example_window &ew = app.extras[app.extra_count++];
    ew.sdl = static_cast<SDL_Window *>(win->handle);
    ew.surface = win->surface;
    ew.win = win; // stays valid until the window is closed
    return &ew;
}

// Closes an extra window (between frames). The slot stays, emptied.
inline void example_remove_window(example_app &app, example_window *ew)
{
    if (!ew || !ew->win) return;
    sdl3_app_close_window(app.boot, *ew->win);
    if (window_at(app.ctx, ew->sdl)) return; // refused: a frame is open
    *ew = example_window{};
}

// Core window for an SDL window id (primary or extra).
inline window *example_window_for(example_app &app, u32 sdl_window_id)
{
    if (app.sdl_window && SDL_GetWindowID(app.sdl_window) == sdl_window_id) return app.win;
    for (i32 i = 0; i < app.extra_count; ++i)
    {
        example_window &ew = app.extras[i];
        if (ew.sdl && SDL_GetWindowID(ew.sdl) == sdl_window_id) return ew.win;
    }
    return nullptr;
}

// Event pump: the library's SDL3 glue routes every event (mouse, buttons,
// wheel, keys, text, IME, focus, move/resize) to the right window; the close
// policy stays app-side: the primary window's close ends the run, an extra
// window's close removes it.
inline void example_pump(example_app &app)
{
    const bool quit = sdl3_pump(app.ctx);
    if (quit)
    {
        app.running = false;
        return;
    }
    for (i32 i = 0; i < app.extra_count; ++i)
    {
        example_window &ew = app.extras[i];
        if (ew.win && ew.win->close_requested) example_remove_window(app, &ew);
    }
    if (app.win && app.win->close_requested) app.running = false;

    // Window size changes (drag-resize through the chrome, maximize) must
    // reach the examples' layout inputs.
    int ww = 0, wh = 0;
    SDL_GetWindowSize(app.sdl_window, &ww, &wh);
    if (static_cast<f32>(ww) != app.win->client.w || static_cast<f32>(wh) != app.win->client.h)
        example_sync_client(app);
    app.width = static_cast<i32>(app.win->area.w);
    app.height = static_cast<i32>(app.win->area.h);
    for (i32 i = 0; i < app.extra_count; ++i)
    {
        example_window &ew = app.extras[i];
        if (!ew.sdl || !ew.win) continue;
        int ew_w = 0, ew_h = 0;
        SDL_GetWindowSize(ew.sdl, &ew_w, &ew_h);
        if (static_cast<f32>(ew_w) != ew.win->client.w ||
            static_cast<f32>(ew_h) != ew.win->client.h)
            set_window_client(*ew.win, example_client(ew.sdl));
    }
}

// Scripted input for --selftest (no-op otherwise).
inline void example_script(example_app &app)
{
    if (!app.selftest || app.no_script) return;
    i32 count = 0;
    const example_script_step *steps = example_script_steps(count);
    const example_script_step &s = steps[app.frame_index % count];
    mouse_move(*app.win, s.xf * static_cast<f32>(app.width), s.yf * static_cast<f32>(app.height));
    if (s.button == 1)
        mouse_button(*app.win, true);
    else if (s.button == 0)
        mouse_button(*app.win, false);
    if (s.right >= 0) mouse_button(*app.win, pointer_button::RIGHT, s.right == 1);
    if (s.wheel != 0.0f) mouse_wheel(*app.win, 0.0f, s.wheel);
    if (s.tab)
    {
        key_event(*app.win, key::TAB, true);
        key_event(*app.win, key::TAB, false);
    }
    if (s.text) text_input_event(*app.win, s.text);
}

inline int example_shutdown(example_app &app)
{
    if (app.before_shutdown)
    {
        auto cleanup = app.before_shutdown;
        app.before_shutdown = nullptr;
        cleanup(app, app.shutdown_user);
    }
    i32 violations = app.ctx ? violation_count(app.ctx) : 0;
#if defined(PUFFERUI_ENABLE_VULKAN)
    if (is_vulkan_device(app.device))
    {
        (void)vulkan_wait_idle(app.device);
        violations += static_cast<i32>(vulkan_statistics(app.device).validation_errors);
        if (vulkan_last_result(app.device) != VK_SUCCESS) ++violations;
    }
#endif
    if (app.boot.inited)
        sdl3_app_shutdown(app.boot); // every window, the device, the renderer, SDL
    else
        SDL_Quit();
    app.ctx = nullptr;
    app.surface = nullptr;
    app.device = nullptr;
    app.renderer = nullptr;
    app.sdl_window = nullptr;
    app.win = nullptr;
    app.extra_count = 0;

    if (app.selftest)
    {
        if (violations == 0)
        {
            std::printf("ok   %s\n", app.name);
            return 0;
        }
        std::printf("FAIL %s: %d violation(s)\n", app.name, violations);
        return 1;
    }
    return violations == 0 ? 0 : 1;
}

// Save a window's current frame to a BMP (best effort; for docs/debugging).
inline void example_screenshot_window(SDL_Window *w, const char *path)
{
    SDL_Renderer *ren = w ? SDL_GetRenderer(w) : nullptr;
    if (!ren || !path) return;
    SDL_Surface *shot = SDL_RenderReadPixels(ren, nullptr);
    if (!shot) return;
    SDL_SaveBMP(shot, path);
    SDL_DestroySurface(shot);
}
inline void example_screenshot(example_app &app, const char *path)
{
#if defined(PUFFERUI_ENABLE_VULKAN)
    if (is_vulkan_device(app.device) && app.surface && path)
    {
        i32 w = 0, h = 0;
        app.surface->output_size(w, h);
        if (w <= 0 || h <= 0) return;
        std::vector<u8> rgba(static_cast<usize>(w) * static_cast<usize>(h) * 4);
        if (!app.device->read_pixels(app.surface->scene_target(), 0, 0, w, h, rgba.data())) return;
        SDL_Surface *shot = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, rgba.data(), w * 4);
        if (shot)
        {
            SDL_SaveBMP(shot, path);
            SDL_DestroySurface(shot);
        }
        return;
    }
#endif
    example_screenshot_window(app.sdl_window, path);
}

// The frame clock, for example_run and hand-written loops (call it at the top
// of every frame). Interactive runs measure real time, so animations take the
// same wall time at 30, 60 or 144 Hz; a long stall (window drag, breakpoint)
// counts as a quarter second so nothing jumps. Selftests step a fixed 1/60 s
// (deterministic screenshots and goldens).
struct example_clock
{
    u64 last = SDL_GetPerformanceCounter();
    f64 freq = static_cast<f64>(SDL_GetPerformanceFrequency());
};
inline void example_tick(example_app &app, example_clock &clk)
{
    if (!app.selftest)
    {
        const u64 ticks = SDL_GetPerformanceCounter();
        const f64 elapsed = static_cast<f64>(ticks - clk.last) / clk.freq;
        clk.last = ticks;
        app.dt = elapsed > 0.0 ? (elapsed < 0.25 ? elapsed : 0.25) : 1.0 / 60.0;
    }
    if (app.frame_index > 0) app.now += app.dt;
}

// Single-window run loop. `frame` is called as `frame(ui&, example_app&)`.
template <typename FrameFn>
int example_run(const char *title, i32 w, i32 h, int argc, char **argv, FrameFn &&frame)
{
    example_app app;
    if (!example_init(app, title, w, h, argc, argv))
    {
        // Cleanup cannot turn failed initialization into a successful selftest.
        app.selftest = false;
        (void)example_shutdown(app);
        return app.help ? 0 : 1;
    }

    example_clock clock;
    while (app.running)
    {
        if (app.selftest && app.frame_index >= app.selftest_frames) break;
        example_tick(app, clock);
        app.diagnostics.measure(example_frame_diagnostics::PUMP, [&] { example_pump(app); });
        if (app.selftest && app.resize_w > 0 && app.frame_index == 1 && app.sdl_window)
        {
            SDL_SetWindowSize(app.sdl_window, app.resize_w, app.resize_h);
            example_sync_client(app); // examples lay out from app.width/height
        }
        example_script(app);
        app.diagnostics.measure(example_frame_diagnostics::BEGIN,
                                [&] { begin_frame(app.ctx, *app.win, app.now, app.dt); });
        {
            ui u(app.ctx);
            frame(u, app);
            if (app.show_violations) // drawn last so nothing covers it
                draw_violation_overlay(u, rect::make(8.0f, app.win->area.h - 40.0f, 360.0f, 32.0f));
        }
        // Presents: a second present() would wait another vblank.
        app.diagnostics.measure(example_frame_diagnostics::END, [&] { end_frame(app.ctx); });
        if (app.screenshot &&
            (app.selftest ? (app.frame_index + 1 >= app.selftest_frames) : (app.frame_index == 60)))
            example_screenshot(app, app.screenshot);

        if (app.extra_frame)
        {
            for (i32 i = 0; i < app.extra_count; ++i)
            {
                example_window &ew = app.extras[i];
                if (!ew.win) continue;
                begin_frame(app.ctx, *ew.win, app.now, app.dt);
                {
                    ui u(app.ctx);
                    app.extra_frame(u, app, i + 1);
                }
                end_frame(app.ctx);
            }
        }

        app.frame_index += 1;
    }
    return example_shutdown(app);
}
