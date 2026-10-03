// todo_ui.h - the whole todo screen (chapters 3-7). Header-only so the headless
// test (todo_test.cpp) can run the exact same code the app runs. The look lives
// in glass.h; the data in todo_model.h; this file is only the screen.
//
// One app-owned `todo_app` holds all state; `todo_draw` builds one frame from it.
// Widgets write the state directly (`app.draft`, `app.filter`, `item.done`), and
// the few things the UI cannot do in the middle of a loop (deleting while
// iterating) are recorded and applied after.
#pragma once

#include "glass_widgets.h"
#include "todo_model.h"

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

// Widget ids are explicit: a literal hashed at compile time. Widgets inside a
// loop get a scope (see todo_row) so each row's ids are its own.
inline constexpr uiid ID_DRAFT = "draft"_id;
inline constexpr uiid ID_ADD = "add"_id;

constexpr f32 ROW_H = 54.0f;

// ---------------------------------------------------------------- screen parts
inline void todo_header(ui &u, rect r, const todo_app &app)
{
    const theme &th = u.th();
    glass_card(u, r);
    rect in = r.pad(20.0f);
    const i32 total = static_cast<i32>(app.list.items.size());
    const i32 done = todo_count_done(app.list);
    const rect title_r = in.cut_top(40.0f);
    {
        text_scope ts = u.text_style(32.0f, app.bold);
        u.text(title_r, "Today", th.text, ALIGN_LEFT);
    }
    if (total > 0)
    {
        // A badge with ONE square corner (bottom-left), like a speech bubble's tail:
        // a radius per corner, and a radius of 0 is a square corner.
        const rect chip = rect::make(title_r.right() - 62.0f, title_r.y + 6.0f, 62.0f, 28.0f);
        u.draw_rounded_rect(chip, color{255, 255, 255, 50},
                            corner_radii{.tl = 14.0f, .tr = 14.0f, .br = 14.0f, .bl = 0.0f});
        text_scope small = u.text_style(15.0f, app.bold);
        u.textf(chip, th.text, ALIGN_CENTER, "%d%%", done * 100 / total);
    }
    if (total == 0)
        u.text(in.cut_top(22.0f), "Nothing planned yet", th.text_dim, ALIGN_LEFT);
    else
        u.textf(in.cut_top(22.0f), th.text_dim, ALIGN_LEFT, "%d of %d done", done, total);

    // progress: a pill track and a pill fill whose width springs to the target
    const rect bar = in.bottom_slice(10.0f);
    const f32 frac = total > 0 ? static_cast<f32>(done) / static_cast<f32>(total) : 0.0f;
    const f32 shown = u.animate("progress"_id, frac, spring{170.0f, 0.9f});
    u.draw_rounded_rect(bar, color{255, 255, 255, 46}, bar.h * 0.5f);
    const f32 w = bar.w * clampf(shown, 0.0f, 1.0f);
    if (w > 0.5f)
        u.draw_rounded_rect(rect::make(bar.x, bar.y, max2(w, bar.h), bar.h),
                            color{255, 255, 255, 235}, bar.h * 0.5f);
}

inline void todo_input(ui &u, rect r, todo_app &app)
{
    const theme &th = u.th();
    glass_card(u, r, glass_style{.radii = corner_radii::top(22.0f), .blur = 20.0f});
    rect in = r.pad(8.0f);
    app.add_button = in.cut_right(86.0f);
    (void)in.cut_right(2.0f); // the field and the button sit almost flush: one split control

    // text_field has no "submitted" result: remember whether it had focus
    // before the call, then look for Enter (the field commits on Enter itself).
    const bool was_focused = u.ctx->focus == ID_DRAFT;
    // round only the field's left side; the button below rounds the right
    (void)u.text_field(in, app.draft, ID_DRAFT,
                       field_opts{.radii = some(corner_radii::left(14.0f))});
    if (app.draft.empty() && u.ctx->focus != ID_DRAFT)
        u.text(in.pad(th.padding * 0.5f + 2.0f, 0.0f), "What needs doing?", th.text_dim,
               ALIGN_LEFT);

    bool submit = glass_button(u, app.add_button, "Add", ID_ADD,
                               {.radii = corner_radii::right(14.0f), .accent = true});
    if (was_focused && u.key_pressed(key::ENTER)) submit = true;
    if (submit && todo_add(app.list, app.draft))
    {
        app.draft.clear();
        app.dirty = true;
        u.ctx->focus_request = ID_DRAFT; // keep typing: put the caret back in the field
    }
}

inline void todo_filters(ui &u, rect r, todo_app &app)
{
    glass_card(u, r,
               glass_style{.radii = corner_radii::bottom(22.0f), .blur = 20.0f, .sheen = 0.0f});
    static const char *const labels[] = {"All", "Active", "Done"};
    // The control sits 4 px inside a card with 22 px corners, so its outer corners get
    // radius 18 (concentric); the top ones stay small because they meet the group's seam.
    const opt<corner_radii> outer =
        some(corner_radii{.tl = 10.0f, .tr = 10.0f, .br = 18.0f, .bl = 18.0f});
    (void)comp::segmented(u, r.pad(4.0f), labels, app.filter, {.id = "filter"_id, .radii = outer});
}

// One row. `remove_id` is set (not acted on) when the delete button is pressed:
// erasing from the vector in the middle of the loop would invalidate it.
inline void todo_row(ui &u, rect bounds, todo_item &t, unsigned &remove_id, bool &changed)
{
    id_scope scope = u.scope(t.id); // every id inside is this task's own
    const theme &th = u.th();

    // enter animation: 0 -> 1 once, the first time this task is seen
    const f32 k = u.appear(u.local("in"), 0.32f);
    bounds.y += (1.0f - k) * 10.0f;

    const bool hot = bounds.contains(u.ctx->mouse_x, u.ctx->mouse_y);
    if (hot) u.draw_rounded_rect(bounds.pad(3.0f, 2.0f), color{255, 255, 255, 16}, 14.0f);
    u.draw_line(bounds.x + 18.0f, bounds.bottom() - 0.5f, bounds.right() - 18.0f,
                bounds.bottom() - 0.5f, color{255, 255, 255, static_cast<u8>(22.0f * k)}, 1.0f);

    rect in = bounds.pad(14.0f, 0.0f);
    const rect check_r = in.cut_left(30.0f);
    (void)in.cut_left(12.0f);
    const rect del_r = in.cut_right(30.0f);
    if (glass_check(u, rect::make(check_r.x, check_r.center_y() - 15.0f, 30.0f, 30.0f), t.done,
                    u.local("check"), k))
        changed = true;

    const color text_c =
        u.animate_color(u.local("tc"), t.done ? th.text_dim : th.text, tween{0.2f});
    u.text_ellipsis(in, t.title, fade(text_c, k), ALIGN_LEFT);
    if (t.done) // strike-through, as wide as the text (or the room we have)
    {
        const f32 w = min2(u.text_width(t.title), in.w);
        u.draw_line(in.x, in.center_y() + 1.0f, in.x + w, in.center_y() + 1.0f,
                    fade(th.text_dim, k), 1.5f);
    }
    if (glass_delete(u, del_r, u.local("del"), hot, k)) remove_id = t.id;
}

inline void todo_empty(ui &u, rect r, const todo_app &app)
{
    const theme &th = u.th();
    const vec2 c{r.center_x(), r.center_y() - 34.0f};
    u.draw_arc(c, 27.0f, 2.0f, 0.0f, 2.0f * PI, color{255, 255, 255, 120});
    u.draw_line(c.x - 10.0f, c.y + 1.0f, c.x - 3.0f, c.y + 8.0f, color{255, 255, 255, 170}, 2.6f);
    u.draw_line(c.x - 3.0f, c.y + 8.0f, c.x + 11.0f, c.y - 8.0f, color{255, 255, 255, 170}, 2.6f);
    rect text_r = rect::make(r.x + 20.0f, c.y + 44.0f, r.w - 40.0f, 60.0f);
    {
        text_scope ts = u.text_style(20.0f, app.bold);
        u.text(text_r.cut_top(28.0f), app.filter == FILTER_DONE ? "Nothing done yet" : "All clear",
               th.text, ALIGN_CENTER);
    }
    u.text(text_r.cut_top(22.0f),
           app.list.items.empty() ? "Add a task above to get started."
                                  : "No tasks match this filter.",
           th.text_dim, ALIGN_CENTER);
}

inline void todo_list_view(ui &u, rect r, todo_app &app)
{
    glass_card(u, r);

    // the tasks that pass the filter, as indices into the model
    std::vector<size_t> shown;
    for (size_t i = 0; i < app.list.items.size(); ++i)
        if (todo_visible(app.list.items[i], app.filter)) shown.push_back(i);
    if (shown.empty())
    {
        todo_empty(u, r, app);
        return;
    }

    unsigned remove_id = 0;
    bool changed = false;
    {
        // scroll_view clips to its viewport; virtual_list only builds the rows
        // that are visible and works out the content height itself.
        scroll_view sv = u.scroll(r.pad(6.0f), "list"_id, scroll_options{0.0f, SCROLL_OVERLAY});
        sv.virtual_list(static_cast<i32>(shown.size()), ROW_H,
                        [&](ui &uu, i32 i, rect row)
                        {
                            todo_row(uu, row, app.list.items[shown[static_cast<size_t>(i)]],
                                     remove_id, changed);
                        });
    }
    if (remove_id != 0)
    {
        todo_remove(app.list, remove_id);
        changed = true;
    }
    if (changed) app.dirty = true;
}

inline void todo_footer(ui &u, rect r, todo_app &app)
{
    const theme &th = u.th();
    rect in = r.pad(8.0f, 0.0f);
    const i32 left = static_cast<i32>(app.list.items.size()) - todo_count_done(app.list);
    const bool any_done = todo_count_done(app.list) > 0;
    const rect clear_r = in.cut_right(150.0f);
    u.textf(in, th.text_dim, ALIGN_LEFT, "%d item%s left", left, left == 1 ? "" : "s");
    if (any_done)
    {
        if (glass_button(u, clear_r, "Clear completed", "clear"_id))
        {
            todo_clear_done(app.list);
            app.dirty = true;
        }
    }
}

// ---------------------------------------------------------------- the frame
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
