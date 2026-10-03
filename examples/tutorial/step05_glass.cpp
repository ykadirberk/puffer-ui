// step05_glass.cpp - chapter 5: the liquid-glass look.
//
// The same app as step04; what changes is how it is painted:
//   * glass_background(): a gradient, glows and crisp orbs behind everything
//   * glass_card():       blur the backdrop, translucent tint, sheen, rim
//   * glass_theme():      translucent whites for fields, buttons, the titlebar
// The widgets themselves (text_field, checkbox, comp::segmented) are untouched:
// they read their colors from the theme.
#include "glass.h"
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
    glass_card(u, r);
    rect in = r.pad(20.0f);
    const rect title_r = in.cut_top(40.0f);
    {
        // text_style changes the font and size for text drawn while it is alive
        text_scope big = u.text_style(32.0f, s.bold);
        u.text(title_r, "Today", th.text, ALIGN_LEFT);
    }
    const i32 total = static_cast<i32>(s.list.items.size());
    if (total > 0)
    {
        // A badge with ONE square corner (bottom-left), like a speech bubble's tail:
        // a radius per corner, and a radius of 0 is a square corner.
        const rect chip = rect::make(title_r.right() - 62.0f, title_r.y + 6.0f, 62.0f, 28.0f);
        u.draw_rounded_rect(chip, color{255, 255, 255, 50},
                            corner_radii{.tl = 14.0f, .tr = 14.0f, .br = 14.0f, .bl = 0.0f});
        text_scope small = u.text_style(15.0f, s.bold);
        u.textf(chip, th.text, ALIGN_CENTER, "%d%%", todo_count_done(s.list) * 100 / total);
    }
    if (total == 0)
        u.text(in.cut_top(22.0f), "Nothing planned yet", th.text_dim, ALIGN_LEFT);
    else
        u.textf(in.cut_top(22.0f), th.text_dim, ALIGN_LEFT, "%d of %d done",
                todo_count_done(s.list), total);
    const f32 frac =
        total ? static_cast<f32>(todo_count_done(s.list)) / static_cast<f32>(total) : 0;
    const rect bar = in.bottom_slice(10.0f);
    u.draw_rounded_rect(bar, color{255, 255, 255, 46}, bar.h * 0.5f);
    if (frac > 0.0f)
        u.draw_rounded_rect(rect::make(bar.x, bar.y, max2(bar.h, bar.w * frac), bar.h),
                            color{255, 255, 255, 235}, bar.h * 0.5f);
}

static void draw_input(ui &u, rect r, app_state &s)
{
    glass_card(u, r, glass_style{.radii = corner_radii::top(22.0f), .blur = 20.0f});
    rect in = r.pad(8.0f);
    const rect add_r = in.cut_right(86.0f);
    (void)in.cut_right(2.0f); // the field and the button sit almost flush: one split control
    const bool was_focused = u.ctx->focus == "draft"_id;
    // round only the field's left side; the button below rounds the right
    (void)u.text_field(in, s.draft, "draft"_id,
                       field_opts{.radii = some(corner_radii::left(14.0f))});
    if (s.draft.empty() && u.ctx->focus != "draft"_id) // a placeholder, drawn by hand
        u.text(in.pad(u.th().padding * 0.5f + 2.0f, 0.0f), "What needs doing?", u.th().text_dim,
               ALIGN_LEFT);
    // the built-in button takes per-corner radii through its override struct
    bool submit = u.button(add_r, "Add", "add"_id, "accent"_id,
                           button_override{.radii = some(corner_radii::right(14.0f))});
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
    glass_card(u, r,
               glass_style{.radii = corner_radii::bottom(22.0f), .blur = 20.0f, .sheen = 0.0f});
    // The control sits 4 px inside a card with 22 px corners, so its outer corners get
    // radius 18 (concentric); the top ones stay small because they meet the group's seam.
    const opt<corner_radii> outer =
        some(corner_radii{.tl = 10.0f, .tr = 10.0f, .br = 18.0f, .bl = 18.0f});
    (void)comp::segmented(u, r.pad(4.0f), labels, s.filter, {.id = "filter"_id, .radii = outer});
}

static void draw_tasks(ui &u, rect r, app_state &s)
{
    const theme &th = u.th();
    glass_card(u, r);

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
    glass_background(u, page, 0.0); // a constant time: the scene holds still (chapter 6 moves it)
    (void)u.titlebar(*app.win, page, "Glass Todo");

    const f32 w = min2(page.w - 32.0f, 480.0f);
    rect content =
        rect::make(page.x + (page.w - w) * 0.5f, page.y + 12.0f, w, max2(0.0f, page.h - 24.0f));
    column col(content, 14.0f);
    draw_header(u, col.next(128.0f), s);
    // The input and the filter bar are one glass group: cut the group out first,
    // then cut the input off its top. Rounded at the top and bottom, square where
    // they meet.
    rect group = col.next(56.0f + 44.0f);
    const rect input_r = group.cut_top(56.0f);
    draw_input(u, input_r, s);
    draw_filters(u, group, s);
    const rect footer = col.cut_bottom(40.0f);
    draw_tasks(u, col.remaining(), s);
    draw_footer(u, footer, s);
}

int main(int argc, char **argv)
{
    tut_app app;
    if (!tut_init(app, "Glass Todo", 520, 760, argc, argv)) return 1;

    set_theme(app.ctx, glass_theme(app.font)); // translucent whites, a frosted "accent" button

    app_state state;
    state.bold = app.bold;
    todo_add(state.list, "Read chapter 5");
    todo_add(state.list, "Make it glass");
    state.list.items[0].done = true;

    tut_loop(app, [&](ui &u) { draw_page(u, app, state); });
    return tut_shutdown(app);
}
