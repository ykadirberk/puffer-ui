// step02_layout.cpp - chapter 2: lay a page out by cutting rectangles.
//
// No widgets yet: every region is a labelled placeholder so you can see where
// the cuts landed. Resize the window - the layout follows.
#include "tut.h"

// A labelled placeholder: a translucent rounded rect and a caption.
static void block(ui &u, rect r, const char *label)
{
    u.draw_rounded_rect(r, color{255, 255, 255, 30}, 14.0f);
    u.text(r.pad(14.0f, 0.0f), label, u.th().text, ALIGN_LEFT);
}

static void draw_page(ui &u, tut_app &app)
{
    rect page = app.win->area; // the whole client area: {0, 0, width, height}
    u.draw_rect(page, color{22, 24, 52, 255});
    (void)u.titlebar(*app.win, page, "Glass Todo"); // cuts the titlebar off `page`

    // One centered column, at most 480 px wide.
    const f32 w = min2(page.w - 32.0f, 480.0f);
    rect content =
        rect::make(page.x + (page.w - w) * 0.5f, page.y + 12.0f, w, max2(0.0f, page.h - 24.0f));

    // A `column` hands out slices from the top, leaving a 14 px gap between them.
    column col(content, 14.0f);
    block(u, col.next(128.0f), "header");

    // The input row: take the button off the right edge, the field gets the rest.
    const rect input = col.next(56.0f);
    block(u, input, "");
    rect in = input.pad(8.0f);
    const rect add_button = in.cut_right(86.0f);
    (void)in.cut_right(8.0f);
    block(u, in, "text field");
    block(u, add_button, "Add");

    block(u, col.next(44.0f), "filters");
    const rect footer = col.cut_bottom(40.0f); // take the footer off the bottom ...
    block(u, col.remaining(), "list");         // ... and give the list everything left
    block(u, footer, "footer");
}

int main(int argc, char **argv)
{
    tut_app app;
    if (!tut_init(app, "Glass Todo", 520, 760, argc, argv)) return 1;
    tut_loop(app, [&](ui &u) { draw_page(u, app); });
    return tut_shutdown(app);
}
