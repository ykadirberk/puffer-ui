// step01_window.cpp - chapter 1: a window, the frame loop, a titlebar, some text.
//
// The smallest complete program, with the loop written out. From chapter 2 on
// this loop lives in tut.h.
#include <pufferui/pufferui.h>

using namespace pui;

int main()
{
    sdl3_app app;
    if (!sdl3_app_init(app, "Glass Todo", 520, 760, 0, nullptr)) return 1;

    while (sdl3_app_pump(app)) // routes input, keeps the window size current; false = quit
    {
        sdl3_app_tick(app); // advances app.now / app.dt
        begin_frame(app.ctx, *app.win, app.now, app.dt);
        {
            ui u(app.ctx);
            rect page = app.win->area;
            u.draw_rect(page, color{20, 22, 44, 255});      // paint the background
            (void)u.titlebar(*app.win, page, "Glass Todo"); // `page` shrinks by the bar
            u.text(page.pad(20.0f).top_slice(24.0f), "Hello, PufferUI", u.th().text, ALIGN_LEFT);
        }
        end_frame(app.ctx);
        app.surface->present();
    }

    sdl3_app_shutdown(app);
    return 0;
}
