// button_states.cpp - chapter 6's picture: one glass button in each of its states.
//
// glass_button_paint draws a button for given hover / press / focus amounts, with
// no input involved, so a gallery of states is just a few calls with fixed numbers.
// (The live glass_button eases these amounts with `smooth` and feeds them in.)
#include "glass_widgets.h"
#include "tut.h"

int main(int argc, char **argv)
{
    tut_app app;
    if (!tut_init(app, "Button states", 560, 250, argc, argv)) return 1;
    set_theme(app.ctx, glass_theme(app.font));

    struct state
    {
        const char *name;
        f32 hover, press;
        bool focused;
    };
    static const state states[4] = {
        {"normal", 0.0f, 0.0f, false},
        {"hover", 1.0f, 0.0f, false},
        {"pressed", 1.0f, 1.0f, false},
        {"focused", 0.0f, 0.0f, true},
    };

    tut_loop(app,
             [&](ui &u)
             {
                 rect page = app.win->area;
                 glass_background(u, page, 3.0);
                 (void)u.titlebar(*app.win, page, "Button states");
                 const rect card = page.pad(14.0f);
                 glass_card(u, card);

                 column rows(card.pad(22.0f, 18.0f), 10.0f);
                 for (const bool accent : {false, true})
                 {
                     row cells(rows.next(76.0f), 14.0f);
                     for (const state &s : states)
                     {
                         rect cell = cells.next(110.0f);
                         const rect button = cell.cut_top(40.0f);
                         glass_button_paint(u, button, accent ? "Add" : "Clear", {.accent = accent},
                                            s.hover, s.press, s.focused);
                         u.text(cell.pad(0.0f, 6.0f), s.name, u.th().text_dim, ALIGN_CENTER);
                     }
                 }
             });
    return tut_shutdown(app);
}
