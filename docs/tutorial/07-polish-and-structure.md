# 7. Polish and structure

Goal: turn the working program into a finished one. Split it into modules that
each do one job, make the tasks persist between runs, and sweep up the details
that make an app feel right: focus, keyboard, cursors, resizing, idle behavior.

← [6. Motion and custom widgets](06-motion-and-custom-widgets.md) · [Tutorial index](README.md) · next: [8. Testing and debugging](08-testing-and-debugging.md)

Program: `todo.cpp` (with `todo_model.h`, `glass.h`, `glass_widgets.h`,
`todo_ui.h`). Run `pui_tut_todo`.

![The finished app](img/final.png)

## One file becomes five

`step06_motion.cpp` is one long file. It works, but it mixes four unrelated
things. The finished program separates them, and the separation is what lets
chapter 8 test it:

| File | Knows about | Does not know about |
|---|---|---|
| `todo_model.h` | tasks: add, remove, filter, save, load | the screen, the library, drawing |
| `glass.h` | how glass *looks*: background, card, rim, theme | tasks |
| `glass_widgets.h` | glass-styled widgets: check mark, delete "x", button | tasks (a bool and an id are all they take) |
| `todo_ui.h` | the screen: how state becomes widgets | windows, SDL, the main loop |
| `todo.cpp` | opening a window, the loop, wiring the above together | everything else |

The dependency arrows only point *down* that table: the screen uses the look and
the model; the model uses nothing. Notice what *didn't* happen: no base classes, no
interfaces, no framework layers. Plain functions in headers, grouped by what they
are about.

`todo.cpp` is now small enough to show whole:

<!-- src: examples/tutorial/todo.cpp -->
```cpp
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
```

Its three jobs: **configure** (a glass theme, a script for self-tests), **load**
the saved tasks, **run** the loop with one line that calls the screen
(`todo_draw`). Anything else you wanted — a second screen, a settings window — is
another function called from that lambda.

## The finished state struct

One new idea per field:

<!-- src: examples/tutorial/todo_ui.h -->
```cpp
struct todo_app
{
    todo_list list;
    std::string draft;       // the text in the input field
    i32 filter = FILTER_ALL; // which tasks the list shows
    font_handle bold = FONT_INVALID;
    const char *save_path = nullptr; // null = do not persist (tests)
    bool dirty = false;              // the model changed: save at the end of the frame
    bool autofocus = true;           // focus the input on the first frame
    rect add_button{};               // where the Add button was drawn last frame (tests click it)
};
```

* **`save_path`** — where to persist; `nullptr` means "don't" (the tests rely on
  that).
* **`dirty`** — the model changed this frame, so save it. (Next section.)
* **`autofocus`** — focus the input on the first frame.

## Persistence

Saving after *every* change keeps the code obvious and the data safe, and a todo
list is tiny, so we do exactly that — but only when something changed. The model
has two plain functions, `todo_save` and `todo_load`; the file is one line per task:

```
0 Buy milk
1 Write the tutorial
```

(`0`/`1` for done, a space, the title.) The UI's only job is to say *when*.

**Mark the change.** Every place the model changes sets the flag — adding a task
(shown), toggling a checkbox, deleting, clearing (the same one line each):

<!-- src: examples/tutorial/todo_ui.h -->
```cpp
if (submit && todo_add(app.list, app.draft))
{
    app.draft.clear();
    app.dirty = true;
    u.ctx->focus_request = ID_DRAFT; // keep typing: put the caret back in the field
}
```

**Save once per frame, at the end,** however many changes happened:

<!-- src: examples/tutorial/todo_ui.h -->
```cpp
if (app.dirty && app.save_path)
{
    (void)todo_save(app.list, app.save_path);
    app.dirty = false;
}
```

**Load at startup,** before the loop:

<!-- src: examples/tutorial/todo.cpp -->
```cpp
todo_app todo;
todo.bold = app.bold;
if (!app.selftest) // a self-test must not touch the user's list
{
    todo.save_path = "todos.txt";
    todo_load(todo.list, todo.save_path);
}
```

Two honest limits: `todo_save` returns `false` if the file could not be written
and this code ignores it (a real app would show a message — exercise 4), and it
rewrites the whole file each time, which is fine for a list but not for a
database.

## Focus: the app should be ready to type

When you start the app the cursor should already be in the input. That is
`focus_request`, applied at the start of the *next* frame, issued on the first
frame:

<!-- src: examples/tutorial/todo_ui.h -->
```cpp
if (app.autofocus)
{
    u.ctx->focus_request = ID_DRAFT;
    app.autofocus = false;
}
```

You have seen the second use in `todo_input`: after adding a task, ask for focus
back so typing continues. Together they make "type, Enter, type, Enter" work
without touching the mouse.

## What the keyboard does, for free

You wrote no keyboard code in this app apart from Enter-to-add. Still:

| Keys | Effect |
|---|---|
| **Tab / Shift+Tab** | move focus through every widget, in the order they were drawn |
| **Space / Enter** | activate the focused button, checkbox or delete "x" |
| **Enter** in the field | add the task (our code), and the field commits |
| **Escape** in the field | cancel editing |
| **Ctrl+A/C/X/V/Z/Y**, arrows, Home/End, Shift-select, double-click-word | the text field's editing model |
| mouse wheel, scrollbar drag | the list |

Two of these are worth a closer look. The Tab order is just **the order widgets
call `interact`** — the same as the order you wrote them. That is why the layout
code decides the keyboard flow, and why a *custom* widget (the check mark, the
delete "x") joins it for free the moment it calls `interact`. The focused widget
draws its own ring: that is the `if (in.focused)` line in each custom widget.

## Cursors and feedback

Hovering something clickable should say so. The library's own widgets already set
the hand cursor over buttons and checkboxes and the I-beam over fields; custom
widgets do it themselves with one call (`u.set_cursor(CURSOR_HAND)` in
`glass_check` and `glass_delete`). Only the window under the pointer applies its
cursor, so several windows never fight over it.

## Resizing

The window is resizable and the layout has no fixed size: every rect comes from
cutting `win.area`. Wider than 512 px the column stays 480 wide and centers;
narrower, it shrinks with a 16 px margin. Tall windows give the list more rows;
short ones scroll. And because every cut clamps, an absurdly small window draws
fewer things rather than breaking: chapter 8's tests run the screen at 40 px wide.

## Idle, and what it costs

An immediate-mode loop *can* redraw 60 times a second forever, but doesn't have
to. `tut_init` turns on `wait_when_idle`: between frames the loop sleeps until an
event arrives, **unless** something needs frames — an unsettled animation, a
focused text field (the caret blinks), or a `request_redraw()`. This app's drifting
scene calls it every frame, so *this* app is never idle; a window with a static
background would use almost no CPU while you read it.

Where the time goes in a frame:

* **Drawing is batched.** Solid shapes and text share one texture, so a whole
  screen is a couple of draw calls.
* **Blur is the expensive layer.** Each `glass_card` ends the current batch and
  does several small copies. Four cards is nothing; this is why glass is for
  *chrome and panels*, not for every row.
* **The list is virtualized.** 300 tasks cost the same as 10 (chapter 8's test
  adds 300).

To measure rather than guess, the repository has a benchmark (`pui_bench`)
that records frame time and draw calls on a headless renderer.

## The finished screen

For reference, the whole frame function — every chapter's idea in one place:

<!-- src: examples/tutorial/todo_ui.h -->
```cpp
inline void todo_draw(ui &u, window &win, todo_app &app)
{
    rect page = win.area;
    glass_background(u, page, u.ctx->now);     // the scene drifts with the frame clock ...
    u.request_redraw();                        // ... so keep frames coming even when idle
    (void)u.titlebar(win, page, "Glass Todo"); // `page` shrinks by the bar's height

    if (app.autofocus)
    {
        u.ctx->focus_request = ID_DRAFT;
        app.autofocus = false;
    }

    // one centered column, at most 480 wide
    const f32 w = min2(page.w - 32.0f, 480.0f);
    rect content =
        rect::make(page.x + (page.w - w) * 0.5f, page.y + 12.0f, w, max2(0.0f, page.h - 24.0f));
    column col(content, 14.0f);
    todo_header(u, col.next(128.0f), app);
    // The input and the filter bar are one glass group: cut the group out first,
    // then cut the input off its top. Rounded at the top and bottom, square where
    // they meet.
    rect group = col.next(56.0f + 44.0f);
    const rect input_r = group.cut_top(56.0f);
    todo_input(u, input_r, app);
    todo_filters(u, group, app);
    const rect footer = col.cut_bottom(40.0f);
    todo_list_view(u, col.remaining(), app);
    todo_footer(u, footer, app);

    if (app.dirty && app.save_path)
    {
        (void)todo_save(app.list, app.save_path);
        app.dirty = false;
    }
}
```

## What you learned

| Idea | One line |
|---|---|
| Modules | model / look / widgets / screen / main; plain functions in headers |
| Persistence | a `dirty` flag, one save per frame, one load at startup |
| Focus | `focus_request` on the first frame, and after each add |
| Keyboard | free: the Tab ring is the order of `interact` calls |
| Idle | sleep unless animating; `request_redraw` for ambient motion |

## Try it

1. Add a **keyboard shortcut**: Ctrl+Shift+Delete clears completed tasks. Check
   `u.key_pressed(key::DEL)`, with `u.ctx->ctrl_down` and `u.ctx->shift_down` for the
   modifiers. Where does the call belong — the UI, or the model? (The *key check* is
   UI; the clearing is `todo_clear_done`, which already exists.)
2. Save to a path from the command line: `todos.txt` is hard-coded in `main`.
   Read `argv[1]` if present.
3. Count how many frames per second the app draws, using `app.dt` — and see it drop
   to near zero when you remove `u.request_redraw()` and leave the window alone.
4. `todo_save` returns `bool`. When it fails, show a dim message in the footer for
   three seconds. (You will need a timestamp in `todo_app` and `ctx->now`.)
5. Make the window remember its size: save it next to the tasks and pass it to
   `tut_init` on the next run.
