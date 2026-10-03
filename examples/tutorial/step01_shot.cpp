// step01_shot.cpp - chapter 1's frame, hosted by tut.h so it can take a screenshot.
//
// step01_window.cpp writes the loop out by hand and so has no command-line flags.
// This program draws the same frame through tut.h; tools/tutorial_shots.ps1 uses
// it to produce the chapter 1 picture.
#include "tut.h"

int main(int argc, char **argv)
{
    tut_app app;
    if (!tut_init(app, "Glass Todo", 520, 220, argc, argv)) return 1;
    tut_loop(app,
             [&](ui &u)
             {
                 rect page = app.win->area;
                 u.draw_rect(page, color{20, 22, 44, 255});
                 (void)u.titlebar(*app.win, page, "Glass Todo");
                 u.text(page.pad(20.0f).top_slice(24.0f), "Hello, PufferUI", u.th().text,
                        ALIGN_LEFT);
             });
    return tut_shutdown(app);
}
