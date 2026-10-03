// todo_test.cpp - chapter 10: testing the app without a window.
//
// The UI is a function of the state, and the library ships a renderer that draws
// nothing (`null_device`), so the exact code the app runs can be driven from a
// test: feed events, run frames, look at the state. Contract violations (a
// duplicate id, a bad slice) are counted; a healthy app has none.
//
//   pui_tut_todo_test        prints "all todo tests passed" and exits 0
#include "todo_ui.h"

#include <cstdio>

static int g_failures = 0;
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            std::printf("FAIL: %s  (line %d)\n", #x, __LINE__);                                    \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)

// ---------------------------------------------------------------- the model
static void test_model()
{
    todo_list list;
    CHECK(!todo_add(list, ""));
    CHECK(!todo_add(list, "   \t "));
    CHECK(todo_add(list, "  Buy milk  "));
    CHECK(list.items.size() == 1 && list.items[0].title == "Buy milk"); // trimmed
    CHECK(todo_add(list, "Walk the dog"));
    CHECK(list.items[0].id != list.items[1].id); // ids are unique

    list.items[1].done = true;
    CHECK(todo_count_done(list) == 1);
    CHECK(todo_visible(list.items[0], FILTER_ACTIVE) &&
          !todo_visible(list.items[1], FILTER_ACTIVE));
    CHECK(!todo_visible(list.items[0], FILTER_DONE) && todo_visible(list.items[1], FILTER_DONE));

    // save / load round trip
    const char *path = "todo_test_tmp.txt";
    CHECK(todo_save(list, path));
    todo_list loaded;
    todo_load(loaded, path);
    CHECK(loaded.items.size() == 2);
    CHECK(loaded.items[0].title == "Buy milk" && !loaded.items[0].done);
    CHECK(loaded.items[1].title == "Walk the dog" && loaded.items[1].done);
    std::remove(path);

    todo_clear_done(list);
    CHECK(list.items.size() == 1);
    todo_remove(list, list.items[0].id);
    CHECK(list.items.empty());
}

// ---------------------------------------------------------------- the screen
// A window on a null renderer, with the same theme and font the app uses.
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

static void test_add_button_hover_and_click()
{
    // The pointer over the Add button makes it the hot widget; a press and release
    // on it adds the typed task; moving away un-hovers it.
    headless h;
    h.frame();
    h.frame();
    h.type("Clicked");
    const rect add = h.app.add_button; // where the screen drew it last frame
    CHECK(add.w > 0.0f && add.h > 0.0f);

    mouse_move(*h.win, add.center_x(), add.center_y());
    h.frame();
    CHECK(h.c->hot == ID_ADD);

    mouse_button(*h.win, true);
    h.frame();
    mouse_button(*h.win, false);
    h.frame();
    CHECK(h.app.list.items.size() == 1);
    CHECK(h.app.list.items[0].title == "Clicked");

    mouse_move(*h.win, 5.0f, 700.0f);
    h.frame();
    CHECK(h.c->hot != ID_ADD);
    CHECK(violation_count(h.c) == 0);
}

static void test_empty_input_adds_nothing()
{
    headless h;
    h.frame();
    h.frame();
    h.type("   ");
    h.press(key::ENTER);
    CHECK(h.app.list.items.empty());
    CHECK(violation_count(h.c) == 0);
}

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

static void test_persistence_flag()
{
    headless h;
    h.app.save_path = "todo_test_tmp2.txt";
    h.frame();
    h.frame();
    h.type("Saved on change");
    h.press(key::ENTER);
    todo_list loaded;
    todo_load(loaded, h.app.save_path); // the frame that added it also saved it
    CHECK(loaded.items.size() == 1 && loaded.items[0].title == "Saved on change");
    std::remove(h.app.save_path);
}

int main()
{
    test_model();
    test_add_with_enter();
    test_add_with_button_and_keyboard();
    test_add_button_hover_and_click();
    test_empty_input_adds_nothing();
    test_filters_and_many_rows();
    test_tiny_windows();
    test_persistence_flag();
    if (g_failures == 0)
    {
        std::printf("all todo tests passed\n");
        return 0;
    }
    std::printf("%d todo test check(s) failed\n", g_failures);
    return 1;
}
