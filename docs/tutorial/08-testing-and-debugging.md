# 8. Testing and debugging

Goal: find your own mistakes quickly, and prove the app works without ever
opening a window.

← [7. Polish and structure](07-polish-and-structure.md) · [Tutorial index](README.md) · next: [9. Where next](09-where-next.md)

Program: `todo_test.cpp`. Run `pui_tut_todo_test`.

## Contract violations: the library tells you when you broke a rule

PufferUI has rules ("every cut must be valid", "an id names one widget",
"scopes close before the frame ends"). Instead of silently drawing garbage when you
break one, the library records a **violation**: a numbered, named, human-readable
report. You meet them in four places:

| Where | How |
|---|---|
| **On screen** | `draw_violation_overlay(u, rect)` draws a strip with the count and the last message — `tut_loop` calls it for you in interactive runs |
| **Standard error** | printed when no handler is installed |
| **A handler** | `set_violation_handler(ctx, fn, user)` routes every report to your function (tests use this to assert) |
| **Counters** | `violation_count(ctx)`, `violation_last(ctx)` (message), `violation_last_code(ctx)` + `violation_code_name(code)` |

The most common ones when learning:

| Code | Means | Usual cause |
|---|---|---|
| `VIOL_DUP_WIDGET_ID` | the same id was used at two different places in a frame (debug builds) | a widget in a loop without `u.scope`, or a copy-pasted id |
| `VIOL_DUP_REGION_ID` | two sibling regions/scopes with the same key | the same, for `region`/`scope` |
| `VIOL_INVALID_SLICE` / `VIOL_INVALID_RECT` / `VIOL_INVALID_AREA` | a cut, a draw call or a viewport got a non-finite or negative rect | NaN from a division by zero in your layout math |
| `VIOL_CLIP_UNBALANCED` | a clip-pushing scope was still alive at `end_frame` | a `scroll_view` / `region` not closed with `{ }` |
| `VIOL_NO_FONT` | text was drawn with no font in the theme | `load_font` failed and the theme has no font |
| `VIOL_INPUT_OVERFLOW` | more text arrived in one frame than the buffer holds | a huge paste |
| `VIOL_LAYOUT_CLAMPED` | a slice you asked for did not fit (opt-in) | see below |

### Make one happen

In `step03_tasks.cpp`, replace `uu.local("del")` with the literal `"del"_id` in the
row's delete button and run it with at least two tasks. Both delete buttons now
claim the same id. The overlay appears at the bottom of the window with
*duplicate widget id among siblings*. (Debug builds only: the check costs a hash
lookup per widget, so release builds skip it.)

The overlay is *loud on purpose*. A clean run shows nothing. That is also why
every program in the tutorial's tests asserts `violation_count == 0`.

### Debug builds stop at the line

In a debug build the library by default **breaks into the debugger at the line that
broke the rule** (when no debugger is attached, the process traps). That is the
fastest way to find the call, with a call stack. `tut_init` turns it off with
`set_break_on_violation(ctx, false)` so a beginner sees the overlay instead of a
crash; in your own app, leave it on while developing under a debugger.

### See clamped layouts

Cuts clamp silently (that is what keeps a tiny window from breaking). If you *want*
to know when a request did not fit, ask:

```cpp
set_report_layout_overflow(app.ctx, true);   // once, at startup
```

From then on a `column::next(100)` with only 60 px left reports
`VIOL_LAYOUT_CLAMPED` ("column slice clamped: requested 100 px > remaining 60 px"). It
is off by default because clamping is normal in a resizable window; turn it on in
tests that use a *fixed* size.

## Debugging by looking

Because layout is just rectangles, the best debugger is to **draw them**:

```cpp
u.draw_rounded_rect(suspect_rect, color{255, 0, 0, 70}, 0);   // a translucent red box
```

and to print a few values on screen with `u.textf`. Other handy facts:

* `u.hot_id()` — the id of the widget the pointer is over this frame.
* `u.ctx->focus` — the id that has keyboard focus.
* `u.ctx->mouse_x / mouse_y` — the pointer, in window coordinates.
* A widget that "does nothing" is usually **overlapped by another** that claims the
  press (the topmost, last-drawn one wins), or **disabled by a popup above it**
  (`in.blocked`).

## Self-tests: the same program, offscreen

Every tutorial program takes `--selftest`: it opens a *hidden* window with the
software renderer, runs a scripted sequence (the finished app types two tasks,
presses Enter, ticks one, hovers one), and exits non-zero if any violation
happened. No display is needed, so it works in CI.

```sh
pui_tut_todo --selftest --frames 40 --screenshot shot.bmp
```

`--screenshot` writes the last frame, which is how this tutorial's pictures were
made (`tools/tutorial_shots.ps1` regenerates them all).

The script is ordinary code feeding the same event functions the real window
does:

<!-- src: examples/tutorial/todo.cpp -->
```cpp
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
```

## Unit tests: no window at all

A self-test needs a (hidden) window. Often you want something faster and sharper:
*type this, press that, check the state*. Two facts make that easy:

1. The UI is a function of your state (`todo_draw(u, win, app)`).
2. The library ships a renderer that draws nothing — **`null_device`** — so the
   *exact* code the app runs can be driven from a test.

`todo_test.cpp` links the core library (no SDL at all) and builds a small fixture:
a context on the null renderer, one window, the same theme and font as the app:

<!-- src: examples/tutorial/todo_test.cpp -->
```cpp
struct headless
{
    null_device dev;
    context *c = nullptr;
    window *win = nullptr;
    int native = 0; // any address will do as the "native window" handle
    todo_app app;
    f64 now = 0.0;

    headless(f32 w = 520.0f, f32 h = 760.0f)
    {
        c = create_context(&dev, dev.create_surface());
        win = add_window(c, &native, c->surface, rect::make(0, 0, w, h));
        focus_window(c, *win);
        const font_handle font = load_font(c, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
        set_theme(c, glass_theme(font));
        app.bold = font;
    }
    ~headless() { destroy_context(c); }

    void frame()
    {
        now += 0.016;
        begin_frame(c, *win, now, 0.016);
        {
            ui u(c);
            todo_draw(u, *win, app);
        }
        end_frame(c);
    }
    void press(key k)
    {
        key_event(*win, k, true);
        frame();
        key_event(*win, k, false);
        frame();
    }
    void type(const char *text)
    {
        text_input_event(*win, text);
        frame();
    }
};
```

* `frame()` is `begin_frame` / your screen / `end_frame` — one frame.
* `type(...)` feeds text exactly as the OS would (`text_input_event`).
* `press(key)` sends a key down, a frame, a key up, a frame: the two frames matter,
  because a key press is an *edge* that exists for one frame.

Then the tests read like user stories:

<!-- src: examples/tutorial/todo_test.cpp -->
```cpp
static void test_add_with_enter()
{
    headless h;
    h.frame(); // the first frame asks for focus on the input ...
    h.frame(); // ... and the second one has it
    CHECK(h.c->focus == ID_DRAFT);

    h.type("Write the tutorial");
    CHECK(h.app.draft == "Write the tutorial");
    h.press(key::ENTER);
    CHECK(h.app.list.items.size() == 1);
    CHECK(h.app.list.items[0].title == "Write the tutorial");
    CHECK(h.app.draft.empty()); // cleared ...
    h.frame();
    CHECK(h.c->focus == ID_DRAFT); // ... and focus returned to the field

    h.type("Ship it");
    h.press(key::ENTER);
    CHECK(h.app.list.items.size() == 2);
    CHECK(violation_count(h.c) == 0);
}
```

Walk through that test: the first frame asks for focus on the input and the second
has it (`h.c->focus == ID_DRAFT`); typing fills `app.draft`; Enter adds the task and
clears the draft; one more frame and the field has focus again — all asserted. The
end check, `violation_count(h.c) == 0`, makes every test a contract test as well.

The keyboard-only path is just as direct. Tab moves focus from the field to the
Add button; Space activates it:

<!-- src: examples/tutorial/todo_test.cpp -->
```cpp
static void test_add_with_button_and_keyboard()
{
    headless h;
    h.frame();
    h.frame();
    h.type("Via the button");
    h.press(key::TAB);   // focus moves to the next widget in the ring: "Add"
    h.press(key::SPACE); // Space activates a focused button
    CHECK(h.app.list.items.size() == 1);
    CHECK(h.app.list.items[0].title == "Via the button");
    CHECK(violation_count(h.c) == 0);
}
```

And the stress cases that are miserable to test by hand — hundreds of rows, every
filter, a window 40 pixels wide:

<!-- src: examples/tutorial/todo_test.cpp -->
```cpp
static void test_filters_and_many_rows()
{
    headless h;
    // Many tasks: every row scopes its ids, so none may collide; the list is
    // virtualized, so only the visible rows are built.
    for (int i = 0; i < 300; ++i) todo_add(h.app.list, "Task " + std::to_string(i));
    for (size_t i = 0; i < h.app.list.items.size(); i += 3) h.app.list.items[i].done = true;
    for (const i32 f : {FILTER_ALL, FILTER_ACTIVE, FILTER_DONE})
    {
        h.app.filter = f;
        h.frame();
        h.frame();
    }
    CHECK(violation_count(h.c) == 0);

    h.app.filter = FILTER_DONE; // no matches: the empty state
    h.app.list.items.clear();
    h.frame();
    CHECK(violation_count(h.c) == 0);
}
```

<!-- src: examples/tutorial/todo_test.cpp -->
```cpp
static void test_tiny_windows()
{
    // The layout clamps instead of producing invalid rectangles.
    for (const f32 w : {40.0f, 160.0f, 520.0f, 2400.0f})
    {
        headless h(w, w < 100.0f ? 60.0f : 300.0f);
        todo_add(h.app.list, "A task in a small window");
        h.frame();
        h.frame();
        CHECK(violation_count(h.c) == 0);
    }
}
```

The 300-row test passing with zero violations means every row's ids really are
scoped (a collision would report `VIOL_DUP_WIDGET_ID`), and the tiny-window test
means the layout's clamping holds up.

Build and run it like any program (it prints `all todo tests passed`):

```sh
cmake --build out/build/x64-release --target pui_tut_todo_test
out/build/x64-release/Release/pui_tut_todo_test.exe
```

The model has its own test (`test_model`: trimming, ids, filtering, a save/load
round trip). Because `todo_model.h` has no UI in it, it needs no fixture at all —
the strongest argument for the split in chapter 7.

### Where a test belongs

| You want to check | Test it with |
|---|---|
| a rule of the data (`todo_add` trims) | a plain function call on the model |
| a flow (type, Enter, item appears) | the headless fixture: events in, state out |
| the app does not break contracts at any size | frames at odd sizes + `violation_count == 0` |
| what it *looks like* | `--screenshot`, and by eye; the library's own pixel tests (`pui_golden_tests`) compare against committed images, a good fit for a frozen reference scene |

## A quick pitfall table

| What you see | Probably | Fix |
|---|---|---|
| Widget flickers or "forgets" focus | its id changes between frames | give it a stable id |
| Two widgets act as one | they share an id | scope, or distinct ids |
| A list row's button is dead | another widget's rect overlaps and claims the press | make sibling hit rects disjoint |
| Everything after a scroll view is clipped | the scope was not closed before drawing more | wrap the scroll in `{ }` |
| Compile error on `x.cut_top(h)` | you cut a temporary (a function result) | store the rect in a variable first |
| Compile warning about discarded return of a cut | you meant to cut and drop it | `(void)r.cut_top(h);` |
| A drag does nothing | you re-captured the anchor every frame | capture on `in.activated`, not `in.held` |
| A text field resets while typing | you rewrote its `std::string` from the model each frame while focused | let the field own the string while focused |
| Blur shows through nothing | blur drawn before the background, or a flat scene | draw the scene first |

## Try it

1. Add a test: adding the same title twice creates two tasks with different ids.
2. Add a test that deletes the first of three tasks and checks the other two
   keep their ids.
3. Write a test that types 600 characters in a single `text_input_event`. The
   per-frame text buffer is 511 bytes: what does the library report
   (`VIOL_INPUT_OVERFLOW`), and how much of the text survives?
4. Turn on `set_report_layout_overflow(h.c, true)` in a test at 520×760 and see
   whether the screen ever asks for more than it has.
5. Run `pui_tut_todo --selftest --frames 40` with `--size 360x500`. Does it still
   pass?
