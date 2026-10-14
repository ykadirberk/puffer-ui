#include "../example_common.h"

static bool open = true;
static uiid selected = 0;
static void frame(ui &u, example_app &app)
{
    auto page =
        example_begin_page(u, app, "Tree and selection", "app-owned expansion and stable IDs");
    rect body = example_section(u, page.content, "Project",
                                "Tab focuses; Left/Right collapse or expand", app.font_bold);
    column rows(body, u.spacing());
    if (comp::tree_node(u, rows.next(u.control_h()), "Assets", open,
                        {.id = "assets"_id, .selected = selected == "assets"_id}))
        selected = "assets"_id;
    if (open)
        for (i32 i = 0; i < 3; ++i)
        {
            const uiid id = id_child("asset"_id, i);
            bool leaf_open = false;
            if (comp::tree_node(u, rows.next(u.control_h()),
                                i == 0   ? "Texture"
                                : i == 1 ? "Mesh"
                                         : "Material",
                                leaf_open,
                                {.id = id, .leaf = true, .selected = selected == id, .depth = 1}))
                selected = id;
        }
    if (comp::selectable(u, rows.next(u.control_h()), "Settings",
                         {.id = "settings"_id, .selected = selected == "settings"_id}))
        selected = "settings"_id;
}

int main(int argc, char **argv)
{
    return example_run("tree", 700, 420, argc, argv, frame);
}
