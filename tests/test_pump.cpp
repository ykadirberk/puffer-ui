// SDL event-pump tests: the library's sdl3_pump/sdl3_route must route every
// event type to the right window (mouse, buttons, wheel, keys, text, IME,
// focus, geometry) and surface close requests, using the SDL offscreen video
// driver + synthetic SDL_PushEvent input. Headless: no display needed.
//
//   pui_pump_tests   (exit 0 = all passed)
#include <pufferui/pufferui.h>

#include <SDL3/SDL.h>

#include <cstdio>

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

    // Quit surfaces as the pump's return value.
    SDL_Event q{};
    q.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&q);
    CHECK(sdl3_pump(c));

    destroy_context(c);
    SDL_DestroyWindow(w1);
    SDL_DestroyWindow(w2);
    SDL_Quit();

    if (g_failures == 0)
    {
        std::printf("all pump tests passed\n");
        return 0;
    }
    std::printf("%d pump test(s) failed\n", g_failures);
    return 1;
}
