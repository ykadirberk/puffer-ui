// windows — several windows over one device.
//
// Shows: `add_window`/`remove_window`, one shared `render_device`, per-window
// frames and input queues, and a model shared across windows.
//
//   pui_ex_windows   (click in either window; the counter is shared)
#include "../example_common.h"

static i32 g_clicks = 0;

static void window_content(ui &u, i32 index, window &w)
{
    const theme &th = u.th();
    const rect client = rect::make(0.0f, 0.0f, w.area.w, w.area.h);
    u.draw_rect(client, index == 0 ? th.bg : th.panel_bg);
    rect chrome = client;
    (void)u.titlebar(w, chrome, index == 0 ? "windows - primary" : "windows - second");
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

    // The second window needs backend_caps::SHARED_DEVICE (the SDL3 device reports
    // it). If the platform refuses it, the example still runs with one window.
    example_window *second = example_add_window(app, "windows - second", 440, 320);

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
        end_frame(app.ctx);
        app.surface->present();

        if (second && second->win)
        {
            begin_frame(app.ctx, *second->win, app.now, app.dt);
            {
                ui u(app.ctx);
                window_content(u, 1, *second->win);
            }
            end_frame(app.ctx);
            second->surface->present();
        }

        // --screenshot saves both windows: FILE and FILE.second.bmp
        const bool last =
            app.selftest ? app.frame_index + 1 >= app.selftest_frames : app.frame_index == 60;
        if (app.screenshot && last)
        {
            example_screenshot(app, app.screenshot);
            if (second && second->sdl)
            {
                char path[1024];
                std::snprintf(path, sizeof(path), "%s.second.bmp", app.screenshot);
                example_screenshot_window(second->sdl, path);
            }
        }
        app.frame_index += 1;
    }

    example_remove_window(app, second);
    return example_shutdown(app);
}