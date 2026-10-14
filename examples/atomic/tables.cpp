#include "../example_common.h"
#include <algorithm>

static comp::table_state state;
static comp::table_column columns[] = {{200}, {120}};
static uiid records[] = {101, 202, 303};
static void frame(ui &u, example_app &app)
{
    auto page = example_begin_page(u, app, "Tables",
                                   "stable record keys, resizable columns, sorting and selection");
    rect body = example_section(u, page.content, "Records",
                                "Click headers to sort; drag column edges", app.font_bold);
    const char *headers[] = {"Record", "Value"};
    auto key_for = [](i32 row) { return records[row]; };
    auto result =
        comp::table(u, body, headers, 3,
                    [](ui &u, rect cell, i32 row, i32 col)
                    {
                        u.textf(cell, u.th().text, ALIGN_LEFT, "%llu",
                                static_cast<unsigned long long>(records[row] * (col ? 10 : 1)));
                    },
                    {.id = "records"_id, .row_key = key_for, .columns = columns, .state = &state});
    if (result.sort_changed)
        std::sort(std::begin(records), std::end(records),
                  [](uiid a, uiid b) { return state.descending ? a > b : a < b; });
}

int main(int argc, char **argv)
{
    return example_run("tables", 700, 420, argc, argv, frame);
}
