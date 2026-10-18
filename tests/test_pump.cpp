// SDL event-pump tests: the library's sdl3_pump/sdl3_route must route every
// event type to the right window (mouse, buttons, wheel, keys, text, IME,
// focus, geometry) and surface close requests, using the SDL offscreen video
// driver + synthetic SDL_PushEvent input. Headless: no display needed.
//
//   pui_pump_tests   (exit 0 = all passed)
#include <pufferui/pufferui.h>

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>
#include <string>

using namespace pui;

static int g_failures = 0;

#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            std::printf("FAIL: %s  (%s:%d)\n", #x, __FILE__, __LINE__);                            \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)

static int vsync_of(SDL_Renderer *r)
{
    int v = -1;
    SDL_GetRenderVSync(r, &v);
    return v;
}

int main()
{
    SDL_SetLogPriorities(SDL_LOG_PRIORITY_ERROR);
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::printf("FAIL: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    CHECK(c != nullptr);

    SDL_Window *w1 = SDL_CreateWindow("pump one", 300, 200, SDL_WINDOW_BORDERLESS);
    SDL_Window *w2 = SDL_CreateWindow("pump two", 300, 200, SDL_WINDOW_BORDERLESS);
    CHECK(w1 && w2);
    window *a = add_window(c, w1, nullptr, rect::make(0, 0, 300, 200));
    window *b = add_window(c, w2, nullptr, rect::make(300, 0, 300, 200));
    CHECK(a && b);

    // The pump installs the SDL3 window host on the first routed event
    // (custom titlebars work with zero setup).

    // Mouse motion + left press over window A; the pump routes it there.
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.windowID = SDL_GetWindowID(w1);
    e.motion.x = 50.0f;
    e.motion.y = 40.0f;
    SDL_PushEvent(&e);
    e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    e.button.windowID = SDL_GetWindowID(w1);
    e.button.button = SDL_BUTTON_LEFT;
    e.button.x = 50.0f;
    e.button.y = 40.0f;
    SDL_PushEvent(&e);
    const bool quit = sdl3_pump(c);
    CHECK(!quit);
    CHECK(window_host_of(c) != nullptr); // installed by the routing above
    // ...together with click-through: SDL's default swallows the first click
    // on an unfocused window, so widgets there ignored it.
    CHECK(SDL_GetHintBoolean(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, false));
    {
        // DEFAULT priority: an app's own choice survives a later device.
        SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "0");
        render_device *dev = create_sdl3_device(nullptr);
        CHECK(!SDL_GetHintBoolean(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, true));
        destroy_sdl3_device(dev);
        SDL_ResetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH);
        // apps on their own SDL setup get it from the device
        dev = create_sdl3_device(nullptr);
        CHECK(SDL_GetHintBoolean(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, false));
        destroy_sdl3_device(dev);
    }
    CHECK(a->close_requested == false);
    begin_frame(c, *a, 0.0, 0.016);
    {
        ui u(c);
        const interaction in = u.interact("pump_hit"_id, rect::make(30, 30, 60, 40));
        CHECK(in.hovered);
        CHECK(c->mouse_down); // the press is global
    }
    end_frame(c);

    // A right press over window 2 routes to window 2's queue.
    mouse_button(*a, false); // release the left hold first
    SDL_Event m{};
    m.type = SDL_EVENT_MOUSE_MOTION;
    m.motion.windowID = SDL_GetWindowID(w2);
    m.motion.x = 80.0f;
    m.motion.y = 50.0f;
    SDL_PushEvent(&m);
    SDL_Event r{};
    r.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    r.button.windowID = SDL_GetWindowID(w2);
    r.button.button = SDL_BUTTON_RIGHT;
    r.button.x = 80.0f;
    r.button.y = 50.0f;
    SDL_PushEvent(&r);
    (void)sdl3_pump(c);
    begin_frame(c, *b, 0.016, 0.016);
    {
        ui u(c);
        const interaction in = u.interact("pump_r"_id, rect::make(0, 0, 200, 100));
        CHECK(in.right_clicked); // right-press edge routed (context menus)
    }
    end_frame(c);

    // Keys: A with ctrl routes to the focused window; mods queue into it and
    // land on the context when its frame opens.
    focus_window(c, *b);
    SDL_Event k{};
    k.type = SDL_EVENT_KEY_DOWN;
    k.key.windowID = SDL_GetWindowID(w2);
    k.key.key = SDLK_A;
    k.key.mod = SDL_KMOD_CTRL;
    SDL_PushEvent(&k);
    (void)sdl3_pump(c);
    begin_frame(c, *b, 0.024, 0.016);
    {
        ui u(c);
        CHECK(u.key_down(key::A));
        CHECK(c->ctrl_down);
    }
    end_frame(c);

    // Text input routes verbatim.
    SDL_Event t{};
    t.type = SDL_EVENT_TEXT_INPUT;
    t.text.windowID = SDL_GetWindowID(w2);
    t.text.text = const_cast<char *>("hi");
    SDL_PushEvent(&t);
    begin_frame(c, *b, 0.032, 0.016);
    {
        ui u(c);
        std::string value;
        (void)u.text_field(rect::make(10, 10, 120, 26), value, "pump_field"_id);
        CHECK(c->text_len == 0); // typed during a frame with no field focused
    }
    end_frame(c);
    // Focus the field, then the queued... (text goes to the CURRENT window's
    // frame: type between frames)
    begin_frame(c, *b, 0.048, 0.016);
    {
        ui u(c);
        std::string value;
        mouse_move(*b, 20, 20);
        mouse_button(*b, true);
        (void)u.text_field(rect::make(10, 10, 120, 26), value, "pump_field2"_id);
    }
    end_frame(c);
    mouse_button(*b, false);
    SDL_Event t2{};
    t2.type = SDL_EVENT_TEXT_INPUT;
    t2.text.windowID = SDL_GetWindowID(w2);
    t2.text.text = const_cast<char *>("ok");
    SDL_PushEvent(&t2);
    (void)sdl3_pump(c);
    begin_frame(c, *b, 0.064, 0.016);
    {
        ui u(c);
        std::string value = "x";
        (void)u.text_field(rect::make(10, 10, 120, 26), value, "pump_field3"_id);
        // The focused field's buffer is authoritative: the pump-fed text lands
        // in the focused field's edit state.
        CHECK(value.size() >= 1);
    }
    end_frame(c);

    // Navigation shortcuts through the real SDL path: Ctrl+Shift+Home selects to the
    // start of a focused field, so the next typed character replaces the selection.
    {
        std::string v = "hello world";
        auto field_frame = [&](f64 t)
        {
            begin_frame(c, *b, t, 0.016);
            {
                ui u(c);
                (void)u.text_field(rect::make(10, 10, 120, 26), v, "pump_nav"_id);
            }
            end_frame(c);
        };
        auto key_ev = [&](SDL_Keycode key, SDL_Keymod mod, bool down)
        {
            SDL_Event ke{};
            ke.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            ke.key.windowID = SDL_GetWindowID(w2);
            ke.key.key = key;
            ke.key.mod = mod;
            SDL_PushEvent(&ke);
            (void)sdl3_pump(c);
        };
        auto text_ev = [&](const char *txt)
        {
            SDL_Event te{};
            te.type = SDL_EVENT_TEXT_INPUT;
            te.text.windowID = SDL_GetWindowID(w2);
            te.text.text = const_cast<char *>(txt);
            SDL_PushEvent(&te);
            (void)sdl3_pump(c);
        };
        field_frame(0.100);
        mouse_move(*b, 20, 20);
        mouse_button(*b, true);
        field_frame(0.116);
        mouse_button(*b, false);
        field_frame(0.132); // focused
        const SDL_Keymod cs = static_cast<SDL_Keymod>(SDL_KMOD_CTRL | SDL_KMOD_SHIFT);
        key_ev(SDLK_END, SDL_KMOD_CTRL, true); // Ctrl+End: caret to the end
        field_frame(0.148);
        key_ev(SDLK_END, SDL_KMOD_CTRL, false);
        key_ev(SDLK_HOME, cs, true); // Ctrl+Shift+Home: select to the start
        field_frame(0.164);
        key_ev(SDLK_HOME, cs, false);
        text_ev("Z");
        field_frame(0.180);
        CHECK(v == "Z");
    }

    // Wheel routes.
    SDL_Event wh{};
    wh.type = SDL_EVENT_MOUSE_WHEEL;
    wh.wheel.windowID = SDL_GetWindowID(w1);
    wh.wheel.y = -1.0f;
    SDL_PushEvent(&wh);
    (void)sdl3_pump(c);

    // Geometry sync: resizing an SDL window updates the core window's client.
    SDL_SetWindowSize(w1, 400, 260);
    SDL_Event rs{};
    rs.type = SDL_EVENT_WINDOW_RESIZED;
    rs.window.windowID = SDL_GetWindowID(w1);
    rs.window.data1 = 400;
    rs.window.data2 = 260;
    SDL_PushEvent(&rs);
    (void)sdl3_pump(c);
    CHECK(a->client.w == 400.0f && a->client.h == 260.0f);

    // Close request: the pump only sets the flag (the app decides).
    SDL_Event cl{};
    cl.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
    cl.window.windowID = SDL_GetWindowID(w2);
    SDL_PushEvent(&cl);
    (void)sdl3_pump(c);
    CHECK(b->close_requested);
    CHECK(!a->close_requested);

    // Expanded keys and all modifiers route to the correct window.
    SDL_Event shortcut{};
    shortcut.type = SDL_EVENT_KEY_DOWN;
    shortcut.key.windowID = SDL_GetWindowID(w1);
    shortcut.key.key = SDLK_K;
    shortcut.key.mod = SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI;
    SDL_PushEvent(&shortcut);
    (void)sdl3_pump(c);
    begin_frame(c, *a, 4.0, 0.016);
    {
        ui u(c);
        CHECK(u.shortcut({key::K, MOD_CTRL | MOD_ALT | MOD_SUPER}));
        CHECK(!u.shortcut({key::K, MOD_CTRL | MOD_ALT | MOD_SUPER}));
    }
    end_frame(c);

    // Quit surfaces as the pump's return value.
    SDL_Event q{};
    q.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&q);
    CHECK(sdl3_pump(c));

    destroy_context(c);

    // VSync paces the loop on ONE window: the loop presents every window in
    // turn, so per-window vsync made two windows run at half the refresh rate.
    {
        SDL_Renderer *r1 = SDL_CreateRenderer(w1, "software");
        CHECK(r1 != nullptr);
        render_device *dev = create_sdl3_device(r1);
        render_surface *s1 = dev->create_surface(w1);
        render_surface *s2 = dev->create_surface(w2);
        CHECK(s1 && s2 && s1 != s2);
        SDL_Renderer *r2 = SDL_GetRenderer(w2);
        CHECK(r2 != nullptr && r2 != r1);
        SDL_ShowWindow(w1);
        SDL_ShowWindow(w2);
        s1->make_current(*dev);
        s1->present();
        s2->make_current(*dev);
        s2->present();
        CHECK(vsync_of(r1) == 1); // the first window on screen waits
        CHECK(vsync_of(r2) == 0); // the other one does not
        // A hidden (or minimized) pacer hands the wait over on the next present.
        SDL_HideWindow(w1);
        s2->make_current(*dev);
        s2->present();
        CHECK(vsync_of(r1) == 0);
        CHECK(vsync_of(r2) == 1);
        // Off means off for every window.
        dev->set_vsync(false);
        CHECK(vsync_of(r1) == 0);
        CHECK(vsync_of(r2) == 0);
        destroy_sdl3_device(dev); // destroys r2 (device-owned)
        SDL_DestroyRenderer(r1);
    }

    SDL_DestroyWindow(w1);
    SDL_DestroyWindow(w2);
    SDL_Quit();

    // sdl3_app_open_window / close_window: a complete extra window in one
    // call, framed like the primary; pointers stay valid across a close.
    {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen"); // SDL_Quit reset the hints
        sdl3_app app;
        app.hidden = true;
        app.renderer_name = "software";
        CHECK(sdl3_app_init(app, "primary", 320, 200, 0, nullptr));
        {
            // The primary's client is in desktop coordinates from the start
            // (it was registered at the origin, so a window placed relative to
            // it before the first move event landed in the wrong spot).
            int x = 0, y = 0;
            SDL_GetWindowPosition(static_cast<SDL_Window *>(app.sdl_window), &x, &y);
            CHECK(app.win->client.x == static_cast<f32>(x) &&
                  app.win->client.y == static_cast<f32>(y));
        }
        window *placed = sdl3_app_open_window(app, "placed", 200, 100, {.x = 300, .y = 220});
        CHECK(placed != nullptr);
        if (placed)
        {
            int x = 0, y = 0;
            SDL_GetWindowPosition(static_cast<SDL_Window *>(placed->handle), &x, &y);
            CHECK(placed->client.x == static_cast<f32>(x) &&
                  placed->client.y == static_cast<f32>(y));
            sdl3_app_close_window(app, *placed);
        }
        window *tools = sdl3_app_open_window(app, "tools", 240, 160);
        window *log = sdl3_app_open_window(app, "log", 240, 160);
        CHECK(tools && log && tools != app.win && log != tools);
        CHECK(app.ctx->window_count == 3);
        CHECK(window_at(app.ctx, tools->handle) == tools);
        CHECK(tools->area.w == 240.0f && tools->surface != app.surface);
        for (window *w : {app.win, tools, log})
        {
            begin_frame(app.ctx, *w, 0.0, 0.016);
            {
                ui u(app.ctx);
                rect page = w->area;
                (void)u.titlebar(*w, page, "t");
            }
            end_frame(app.ctx);
        }
        sdl3_app_close_window(app, *tools); // the close-glyph workflow
        CHECK(app.ctx->window_count == 2);
        CHECK(window_at(app.ctx, log->handle) == log); // untouched by the close
        begin_frame(app.ctx, *log, 0.016, 0.016);
        end_frame(app.ctx);
        // r100: OS drops route to the window they happened over.
        {
            const std::string path = "C:/drop/me.png"; // SDL keeps the pointer, not a copy
            SDL_Event d{};
            d.drop.windowID = SDL_GetWindowID(static_cast<SDL_Window *>(log->handle));
            d.drop.x = 20.0f;
            d.drop.y = 30.0f;
            d.type = SDL_EVENT_DROP_BEGIN;
            sdl3_route(app.ctx, &d);
            d.type = SDL_EVENT_DROP_FILE;
            d.drop.data = path.c_str();
            sdl3_route(app.ctx, &d);
            d.type = SDL_EVENT_DROP_COMPLETE;
            d.drop.data = nullptr;
            sdl3_route(app.ctx, &d);
            drop_result got{};
            begin_frame(app.ctx, *log, 0.032, 0.016);
            {
                ui u(app.ctx);
                got = u.drop_target(log->area, "pump_drop"_id);
            }
            end_frame(app.ctx);
            CHECK(got.dropped && got.count == 1 && got.is_file(0));
            CHECK(std::strcmp(got.item(0), path.c_str()) == 0);
        }
        // r100: BMP/PNG/JPEG through SDL into a device image.
        {
            SDL_Surface *s = SDL_CreateSurface(3, 2, SDL_PIXELFORMAT_RGBA32);
            CHECK(s != nullptr);
            const char *tmp = "pui_pump_test_image.bmp";
            CHECK(s && SDL_SaveBMP(s, tmp));
            SDL_DestroySurface(s);
            image_handle img = sdl3_load_image(app.ctx, tmp);
            CHECK(img.valid() && img.w == 3 && img.h == 2);
            destroy_image(app.ctx, img);
            SDL_RemovePath(tmp);
            CHECK(!sdl3_load_image(app.ctx, "no/such/file.png").valid());
        }
        // r100: a saved state from a monitor that is gone lands on a display.
        {
            sdl3_apply_window_state(*log, {.x = -50000, .y = -50000, .w = 200, .h = 120});
            SDL_DisplayID d = SDL_GetPrimaryDisplay();
            SDL_Rect b{};
            CHECK(SDL_GetDisplayUsableBounds(d, &b));
            const rect cl = log->client;
            CHECK(cl.x >= static_cast<f32>(b.x) && cl.y >= static_cast<f32>(b.y));
            CHECK(cl.w == 200.0f && cl.h == 120.0f);
            const std::string saved = save_window_state(get_window_state(*log));
            window_state back{};
            CHECK(load_window_state(saved, back) && back.w == 200);
        }
        // r101: the offscreen driver has no popup windows. The host notices
        // (popup_bounds probes once) and a combo at the bottom of a small
        // window stays inside it (above the header, or clamped) instead of
        // drawing below the window where nothing would show. (The popup-window
        // path itself is covered by tests/test_platform.cpp with a recording
        // host and checked with real input on Windows.)
        {
            static const char *items[3] = {"Alpha", "Beta", "Gamma"};
            i32 sel = 0;
            const rect cb = rect::make(20, log->area.h - 40.0f, 150, 28);
            const auto frame = [&](f64 t)
            {
                begin_frame(app.ctx, *log, t, 0.016);
                {
                    ui u(app.ctx);
                    (void)u.combo(cb, "", items, 3, sel, "pump_cb"_id);
                }
                end_frame(app.ctx);
            };
            const auto button = [&](f32 x, f32 y, bool down)
            {
                SDL_Window *lw = static_cast<SDL_Window *>(log->handle);
                SDL_Event m{};
                m.type = SDL_EVENT_MOUSE_MOTION;
                m.motion.windowID = SDL_GetWindowID(lw);
                m.motion.x = x;
                m.motion.y = y;
                sdl3_route(app.ctx, &m);
                SDL_Event b{};
                b.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
                b.button.windowID = SDL_GetWindowID(lw);
                b.button.button = SDL_BUTTON_LEFT;
                b.button.x = x;
                b.button.y = y;
                sdl3_route(app.ctx, &b);
            };
            int before = 0;
            SDL_free(SDL_GetWindows(&before));
            button(cb.x + 20.0f, cb.y + 10.0f, true);
            frame(0.10);
            button(cb.x + 20.0f, cb.y + 10.0f, false);
            frame(0.12);
            int after = 0;
            SDL_free(SDL_GetWindows(&after));
            CHECK(after == before); // no popup window on this driver
            const uiid drop = id_child("pump_cb"_id, "dropdown"_id);
            rect area{};
            for (i32 i = 0; i < app.ctx->popup_depth; ++i)
                if (app.ctx->popups[i].id == drop) area = app.ctx->popups[i].area;
            CHECK(area.h > 0.0f && area.y >= 0.0f && area.bottom() <= log->area.h); // inside
            const f32 beta_y = area.y + 6.0f + 26.0f + 2.0f + 13.0f;
            button(area.x + 30.0f, beta_y, true);
            frame(0.14);
            button(area.x + 30.0f, beta_y, false);
            frame(0.16);
            frame(0.18);
            CHECK(sel == 1);
        }
        // r106: the offscreen driver has no native window: the UIA adapter
        // declines quietly (the bootstrap already tried) and recording stays off.
        CHECK(!sdl3_enable_uia(*log));
        CHECK(!app.ctx->a11y_enabled);
        sdl3_app_close_window(app, *app.win); // the primary is not closed this way
        CHECK(app.ctx->window_count == 2);
        CHECK(violation_count(app.ctx) == 0);
        sdl3_app_shutdown(app); // closes `log` too
        CHECK(!app.inited);
    }

    if (g_failures == 0)
    {
        std::printf("all pump tests passed\n");
        return 0;
    }
    std::printf("%d pump test(s) failed\n", g_failures);
    return 1;
}
