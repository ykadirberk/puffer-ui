// todo.cpp - the finished app (chapter 9): a liquid-glass todo list.
//
//   pui_tut_todo                      run it (tasks persist in ./todos.txt)
//   pui_tut_todo --selftest           offscreen, scripted typing, asserts no violations
//   pui_tut_todo --selftest --screenshot shot.bmp
#include "todo_ui.h"
#include "tut.h"

// The scripted input for --selftest (the window is 520x760): type two tasks,
// press Enter after each, tick the first one, hover the second row, then the filter bar.
static void selftest_script(tut_app &app, i32 frame)
{
    window &w = *app.win;
    if (frame == 2) text_input_event(w, "Write the tutorial");
    if (frame == 3)
    {
        key_event(w, key::ENTER, true);
        key_event(w, key::ENTER, false);
    }
    if (frame == 5) text_input_event(w, "Ship it");
    if (frame == 6)
    {
        key_event(w, key::ENTER, true);
        key_event(w, key::ENTER, false);
    }
    if (frame == 9) mouse_move(w, 56.0f, 352.0f); // the first checkbox
    if (frame == 10) mouse_button(w, true);
    if (frame == 11) mouse_button(w, false);
    if (frame >= 14 && frame < 30)
        mouse_move(w, 300.0f, 407.0f);              // the second row: its "x" fades in
    if (frame >= 30) mouse_move(w, 260.0f, 266.0f); // the "Active" segment: hover
}

int main(int argc, char **argv)
{
    tut_app app;
    if (!tut_init(app, "Glass Todo", 520, 760, argc, argv)) return 1;
    set_theme(app.ctx, glass_theme(app.font));
    app.script = selftest_script;

    todo_app todo;
    todo.bold = app.bold;
    if (!app.selftest) // a self-test must not touch the user's list
    {
        todo.save_path = "todos.txt";
        todo_load(todo.list, todo.save_path);
    }

    tut_loop(app, [&](ui &u) { todo_draw(u, *app.win, todo); });
    return tut_shutdown(app);
}
