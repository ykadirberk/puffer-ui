// step03_tasks.cpp - chapter 3: state, a text field, and a list of tasks.
//
// The default dark theme and plain cards: the point here is the data flow.
// Widgets write straight into the app's state; nothing is "dispatched".
#include "todo_model.h"
#include "tut.h"

struct app_state
{
    todo_list list;    // the tasks (todo_model.h)
    std::string draft; // what is typed in the input field
};

constexpr f32 ROW_H = 44.0f;

static void draw_input(ui &u, rect r, app_state &s)
{
    u.card(r);
    rect in = r.pad(8.0f);
    const rect add_r = in.cut_right(80.0f);
    (void)in.cut_right(8.0f);

    // text_field edits `s.draft` in place. It has no "submitted" result, so
    // remember whether it had focus before the call and look for Enter after.
    const bool was_focused = u.ctx->focus == "draft"_id;
    (void)u.text_field(in, s.draft, "draft"_id);
    bool submit = u.button(add_r, "Add", "add"_id);
    if (was_focused && u.key_pressed(key::ENTER)) submit = true;

    if (submit && todo_add(s.list, s.draft))
    {
        s.draft.clear();
        u.ctx->focus_request = "draft"_id; // put the caret back in the field
    }
}

static void draw_tasks(ui &u, rect r, app_state &s)
{
    u.card(r);
    unsigned remove_id = 0; // deleting inside the loop would invalidate it

    {
        // A scroll_view clips to its viewport and scrolls its content;
        // virtual_list only builds the rows that are on screen.
        scroll_view sv = u.scroll(r.pad(6.0f), "list"_id);
        sv.virtual_list(static_cast<i32>(s.list.items.size()), ROW_H,
                        [&](ui &uu, i32 i, rect row)
                        {
                            todo_item &t = s.list.items[static_cast<size_t>(i)];
                            // Widget ids are explicit hashes. Inside a loop, a scope
                            // makes "check" and "del" unique per task.
                            id_scope scope = uu.scope(t.id);
                            rect in = row.pad(6.0f, 4.0f);
                            const rect del_r = in.cut_right(32.0f);
                            (void)uu.checkbox(in, t.title, t.done, uu.local("check"));
                            if (uu.button(del_r, "x", uu.local("del"))) remove_id = t.id;
                        });
    }
    if (remove_id != 0) todo_remove(s.list, remove_id);
}

static void draw_page(ui &u, tut_app &app, app_state &s)
{
    rect page = app.win->area;
    u.draw_rect(page, u.th().bg);
    (void)u.titlebar(*app.win, page, "Glass Todo");

    const f32 w = min2(page.w - 32.0f, 480.0f);
    rect content =
        rect::make(page.x + (page.w - w) * 0.5f, page.y + 12.0f, w, max2(0.0f, page.h - 24.0f));
    column col(content, 14.0f);
    draw_input(u, col.next(56.0f), s);
    draw_tasks(u, col.remaining(), s);
}

int main(int argc, char **argv)
{
    tut_app app;
    if (!tut_init(app, "Glass Todo", 520, 760, argc, argv)) return 1;

    app_state state;
    todo_add(state.list, "Read chapter 3");
    todo_add(state.list, "Add a task of my own");

    tut_loop(app, [&](ui &u) { draw_page(u, app, state); });
    return tut_shutdown(app);
}
