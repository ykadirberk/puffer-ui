#include "../example_common.h"
#include <algorithm>

static comp::table_state state;
static comp::table_column columns[] = {{140}, {120}, {160}, {260}, {200}};
static i32 order[] = {0, 1, 2, 3, 4}; // display order: drag a header to change it
static uiid records[] = {101, 202, 303, 404, 505, 606};
static const char *owners[] = {"Ada", "Grace", "Linus", "Margaret", "Ken", "Barbara"};
static void frame(ui &u, example_app &app)
{
    auto page = example_begin_page(u, app, "Tables",
                                   "record keys, sorting, column order, visibility and scrolling");
    rect body = example_section(
        u, page.content, "Records",
        "Sort: click a header. Reorder: drag one. Hide or show: right-click. Shift+wheel scrolls",
        app.font_bold);
    const char *headers[] = {"Record", "Value", "Owner", "Notes", "Status"};
    auto key_for = [](i32 row) { return records[row]; };
    auto result =
        comp::table(u, body, headers, 6,
                    [](ui &u, rect cell, i32 row, i32 col)
                    {
                        const unsigned long long id = records[row];
                        if (col == 0) u.textf(cell, u.th().text, ALIGN_LEFT, "#%llu", id);
                        if (col == 1) u.textf(cell, u.th().text, ALIGN_LEFT, "%llu", id * 10);
                        if (col == 2) u.text(cell, owners[id / 101 - 1], u.th().text, ALIGN_LEFT);
                        if (col == 3)
                            u.text(cell, "free text that makes the table wider", u.th().text_dim);
                        if (col == 4)
                            u.text(cell, id % 202 == 0 ? "archived" : "active", u.th().text_dim);
                    },
                    {.id = "records"_id,
                     .row_key = key_for,
                     .columns = columns,
                     .state = &state,
                     .order = order,
                     .horizontal = true,
                     .sticky_first = true});
    if (result.sort_changed)
        std::sort(std::begin(records), std::end(records),
                  [](uiid a, uiid b) { return state.descending ? a > b : a < b; });
}

int main(int argc, char **argv)
{
    return example_run("tables", 700, 420, argc, argv, frame);
}
