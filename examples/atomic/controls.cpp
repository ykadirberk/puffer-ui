#include "../example_common.h"

static bool enabled = true;
static std::string name = "Read-only values can still be selected and copied";
static i32 saved = 0;
static void frame(ui &u, example_app &app)
{
    auto page = example_begin_page(u, app, "Control states",
                                   "disabled groups, read-only fields, and Ctrl+S");
    rect body = example_section(u, page.content, "Settings", "Tab through the enabled controls",
                                app.font_bold);
    column rows(body, u.spacing());
    u.checkbox(rows.next(u.control_h()), "Enable Save", enabled, "enabled"_id);
    u.text_field(rows.next(u.control_h()), name, "name"_id, {.read_only = true});
    {
        auto disabled = u.disabled(!enabled);
        if (u.button(rows.next(u.control_h()), "Save", "save"_id) || u.shortcut({key::S, MOD_CTRL}))
            ++saved;
    }
    u.textf(rows.next(u.control_h()), u.th().text, ALIGN_LEFT, "Saved %d times", saved);
}

int main(int argc, char **argv)
{
    return example_run("controls", 700, 420, argc, argv, frame);
}
