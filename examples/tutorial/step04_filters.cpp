// step04_filters.cpp - chapter 4: filters, counts, a footer, an empty state.
//
// Still the default theme. New here: a view derived from the state each frame
// (which tasks are visible), a header that reads the counts, `comp::segmented`
// for the filter, text styles (bold, bigger), and keyboard-friendly details.
#include "todo_model.h"
#include "tut.h"

struct app_state
{
    todo_list list;
    std::string draft;
    i32 filter = FILTER_ALL; // which tasks the list shows
    font_handle bold = FONT_INVALID;
};

constexpr f32 ROW_H = 44.0f;

static void draw_header(ui &u, rect r, const app_state &s)
{
    const theme &th = u.th();
    u.card(r);
    rect in = r.pad(18.0f);
    {
        // text_style changes the font and size for text drawn while it is alive
        text_scope big = u.text_style(32.0f, s.bold);
        u.text(in.cut_top(40.0f), "Today", th.text, ALIGN_LEFT);
    }
    const i32 total = static_cast<i32>(s.list.items.size());
    if (total == 0)
        u.text(in.cut_top(22.0f), "Nothing planned yet", th.text_dim, ALIGN_LEFT);
    else
        u.textf(in.cut_top(22.0f), th.text_dim, ALIGN_LEFT, "%d of %d done",
                todo_count_done(s.list), total);
    const f32 frac =
        total ? static_cast<f32>(todo_count_done(s.list)) / static_cast<f32>(total) : 0;
    const rect bar = in.bottom_slice(8.0f);
    u.draw_rounded_rect(bar, th.widget_bg, 4.0f);
    if (frac > 0.0f)
        u.draw_rounded_rect(rect::make(bar.x, bar.y, max2(bar.h, bar.w * frac), bar.h), th.accent,
                            4.0f);
}

static void draw_input(ui &u, rect r, app_state &s)
{
    u.card(r);
    rect in = r.pad(8.0f);
    const rect add_r = in.cut_right(80.0f);
    (void)in.cut_right(8.0f);
    const bool was_focused = u.ctx->focus == "draft"_id;
    (void)u.text_field(in, s.draft, "draft"_id);
    if (s.draft.empty() && u.ctx->focus != "draft"_id) // a placeholder, drawn by hand
        u.text(in.pad(u.th().padding * 0.5f + 2.0f, 0.0f), "What needs doing?", u.th().text_dim,
               ALIGN_LEFT);
    bool submit = u.button(add_r, "Add", "add"_id, "primary"_id);
    if (was_focused && u.key_pressed(key::ENTER)) submit = true;
    if (submit && todo_add(s.list, s.draft))
    {
        s.draft.clear();
        u.ctx->focus_request = "draft"_id;
    }
}

static void draw_filters(ui &u, rect r, app_state &s)
{
    static const char *const labels[] = {"All", "Active", "Done"};
    // comp::segmented is one of the library's ready-made components: it reads
    // and writes `s.filter` and needs an explicit id like every widget.
    (void)comp::segmented(u, r, labels, s.filter, {.id = "filter"_id});
}

static void draw_tasks(ui &u, rect r, app_state &s)
{
    const theme &th = u.th();
    u.card(r);

    // The view: indices of the tasks the filter lets through, rebuilt each frame.
    std::vector<size_t> shown;
    for (size_t i = 0; i < s.list.items.size(); ++i)
        if (todo_visible(s.list.items[i], s.filter)) shown.push_back(i);

    if (shown.empty()) // the empty state
    {
        rect text_r = rect::make(r.x + 20.0f, r.center_y() - 24.0f, r.w - 40.0f, 60.0f);
        {
            text_scope ts = u.text_style(20.0f, s.bold);
            u.text(text_r.cut_top(28.0f), "All clear", th.text, ALIGN_CENTER);
        }
        u.text(text_r.cut_top(22.0f),
               s.list.items.empty() ? "Add a task above to get started."
                                    : "No tasks match this filter.",
               th.text_dim, ALIGN_CENTER);
        return;
    }

    unsigned remove_id = 0;
    {
        scroll_view sv = u.scroll(r.pad(6.0f), "list"_id);
        sv.virtual_list(static_cast<i32>(shown.size()), ROW_H,
                        [&](ui &uu, i32 i, rect row)
                        {
                            todo_item &t = s.list.items[shown[static_cast<size_t>(i)]];
                            id_scope scope = uu.scope(t.id);
                            rect in = row.pad(6.0f, 4.0f);
                            const rect del_r = in.cut_right(32.0f);
                            (void)uu.checkbox(in, t.title, t.done, uu.local("check"));
                            if (uu.button(del_r, "x", uu.local("del"))) remove_id = t.id;
                        });
    }
    if (remove_id != 0) todo_remove(s.list, remove_id);
}

static void draw_footer(ui &u, rect r, app_state &s)
{
    rect in = r.pad(8.0f, 0.0f);
    const i32 left = static_cast<i32>(s.list.items.size()) - todo_count_done(s.list);
    const rect clear_r = in.cut_right(150.0f);
    u.textf(in, u.th().text_dim, ALIGN_LEFT, "%d item%s left", left, left == 1 ? "" : "s");
    if (todo_count_done(s.list) > 0 && u.button(clear_r, "Clear completed", "clear"_id))
        todo_clear_done(s.list);
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
    draw_header(u, col.next(128.0f), s);
    draw_input(u, col.next(56.0f), s);
    draw_filters(u, col.next(44.0f), s);
    const rect footer = col.cut_bottom(40.0f);
    draw_tasks(u, col.remaining(), s);
    draw_footer(u, footer, s);
}

int main(int argc, char **argv)
{
    tut_app app;
    if (!tut_init(app, "Glass Todo", 520, 760, argc, argv)) return 1;

    // A "primary" button role: a named style that widgets opt into by id.
    theme t = default_dark();
    t.font = app.font;
    set_button_role(t, "primary"_id,
                    button_override{.bg = some(color{58, 116, 220, 255}),
                                    .hover_bg = some(color{82, 140, 240, 255})});
    set_theme(app.ctx, t);

    app_state state;
    state.bold = app.bold;
    todo_add(state.list, "Read chapter 4");
    todo_add(state.list, "Filter the list");
    state.list.items[0].done = true;

    tut_loop(app, [&](ui &u) { draw_page(u, app, state); });
    return tut_shutdown(app);
}
