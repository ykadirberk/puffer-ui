// windows — several windows over one device.
//
// Shows: `sdl3_app_open_window` / `sdl3_app_close_window` (a complete extra
// window in one call), a button on the titlebar (`titlebar_result::free`),
// one shared `render_device`, per-window frames and input queues, and a model
// shared across windows.
//
//   pui_ex_windows   (click in either window; the counter is shared)
#include "../example_common.h"

static i32 g_clicks = 0;
static window *g_second = nullptr; // valid while the window is open
static bool g_toggle = false;      // the titlebar button asked to open/close it

static void window_content(ui &u, i32 index, window &w)
{
    const theme &th = u.th();
    const rect client = rect::make(0.0f, 0.0f, w.area.w, w.area.h);
    u.draw_rect(client, index == 0 ? th.bg : th.panel_bg);
    rect chrome = client;
    const titlebar_result tb =
        u.titlebar(w, chrome, index == 0 ? "windows - primary" : "windows - second");
    if (index == 0)
    {
        // A widget on the bar keeps its clicks; the rest of the bar still drags.
        rect slot = tb.free;
        const char *label = g_second ? "Close second" : "Open second";
        const rect open_r = slot.cut_right(u.button_size(label).x).pad(0.0f, 4.0f);
        if (u.button(open_r, label, "w_toggle"_id))
            g_toggle = true; // windows open and close between frames
    }
    column col(chrome.pad(16.0f), 10.0f);

    char line[128];
    std::snprintf(line, sizeof(line), "window %d  (client %.0f x %.0f)", index + 1,
                  static_cast<double>(client.w), static_cast<double>(client.h));
    u.text(col.next(24.0f), line, th.text, ALIGN_LEFT);

    if (u.button(col.next(30.0f), "Click me", index == 0 ? "w1_btn"_id : "w2_btn"_id, "primary"_id))
        g_clicks += 1;

    std::snprintf(line, sizeof(line), "shared clicks: %d", g_clicks);
    u.text(col.next(20.0f), line, th.text_dim, ALIGN_LEFT);

    u.text(col.next(18.0f), "One device owns textures; each window has its own", th.text_dim,
           ALIGN_LEFT);
    u.text(col.next(18.0f), "surface, input queue, focus list and widget state.", th.text_dim,
           ALIGN_LEFT);
    u.text(col.next(18.0f), "The pointer and held button are global.", th.text_dim, ALIGN_LEFT);
}

int main(int argc, char **argv)
{
    example_app app;
    if (!example_init(app, "windows", 520, 380, argc, argv)) return example_shutdown(app);

    // An extra window needs backend_caps::SHARED_DEVICE (the SDL3 device reports
    // it). If the platform refuses it, the example still runs with one window.
    g_second = sdl3_app_open_window(app.boot, "windows - second", 440, 320);

    example_clock clock;
    while (app.running)
    {
        if (app.selftest && app.frame_index >= app.selftest_frames) break;
        example_tick(app, clock); // real time when interactive
        example_pump(app);
        example_script(app); // --selftest input goes to the primary window

        begin_frame(app.ctx, *app.win, app.now, app.dt);
        {
            ui u(app.ctx);
            window_content(u, 0, *app.win);
        }
        end_frame(app.ctx); // presents the primary window

        if (g_second)
        {
            begin_frame(app.ctx, *g_second, app.now, app.dt);
            {
                ui u(app.ctx);
                window_content(u, 1, *g_second);
            }
            end_frame(app.ctx); // presents the second window
        }

        // Between frames: the second window's close glyph, the titlebar button.
        if (g_second && (g_second->close_requested || g_toggle))
        {
            sdl3_app_close_window(app.boot, *g_second);
            g_second = nullptr;
        }
        else if (!g_second && g_toggle)
        {
            g_second = sdl3_app_open_window(app.boot, "windows - second", 440, 320);
        }
        g_toggle = false;

        // --screenshot saves both windows: FILE and FILE.second.bmp
        const bool last =
            app.selftest ? app.frame_index + 1 >= app.selftest_frames : app.frame_index == 60;
        if (app.screenshot && last)
        {
            example_screenshot(app, app.screenshot);
            if (g_second)
            {
                char path[1024];
                std::snprintf(path, sizeof(path), "%s.second.bmp", app.screenshot);
                example_screenshot_window(static_cast<SDL_Window *>(g_second->handle), path);
            }
        }
        app.frame_index += 1;
    }
    return example_shutdown(app); // closes the second window too
}
