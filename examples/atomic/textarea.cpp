// textarea — multi-line editing: notes and a read-only log.
//
// Shows: `ui::text_area` with soft wrap (notes) and without (a log with line
// numbers, read-only), `u.editor_info` for a "Ln, Col" status, the shared
// editing keys (Enter, Up/Down, PageUp/PageDown, Home/End per line, Ctrl+Home
// and Ctrl+End, triple-click a line, undo/redo, clipboard).
//
//   pui_ex_textarea   (type in the notes; the log grows every half second)
#include "../example_common.h"

#include <string>

static std::string g_notes = "Shopping\n- oat milk\n- coffee beans\n\nA long line wraps at the "
                             "width of the area instead of running off to the right, and the "
                             "caret keeps its column when you move up and down.";
static std::string g_log;
static i32 g_log_lines = 0;
static f64 g_next_log = 0.0;

static void frame(ui &u, example_app &app)
{
    const theme &th = u.th();
    auto page = example_begin_page(u, app, "Text area", "Multi-line editing: notes and a log");
    rect content = page.content;

    // The log grows on its own clock (an app-side model).
    while (app.now >= g_next_log && g_log_lines < 2000)
    {
        char line[96];
        std::snprintf(line, sizeof(line), "%s12:00:%02d.%03d  info  worker %d finished job #%d",
                      g_log.empty() ? "" : "\n", (g_log_lines / 2) % 60, (g_log_lines % 2) * 500,
                      g_log_lines % 4, g_log_lines);
        g_log += line;
        g_log_lines += 1;
        g_next_log += 0.5;
    }

    rect left = content.cut_left(content.w * 0.5f - example_ui::SECTION_GAP * 0.5f);
    (void)content.cut_left(example_ui::SECTION_GAP);

    rect notes =
        example_section(u, left, "Notes", "wrap on; Enter starts a new line", app.font_bold);
    rect status = notes.cut_bottom(u.control_h());
    (void)u.text_area(notes, g_notes, "notes"_id);
    const editor_info at = u.editor_info("notes"_id);
    char line[64];
    std::snprintf(line, sizeof(line), "Ln %d, Col %d%s", at.line + 1, at.column + 1,
                  at.focused ? "" : "   (click to edit)");
    u.text(status, line, th.text_dim, ALIGN_LEFT);

    rect log =
        example_section(u, content, "Log", "read-only, no wrap, line numbers", app.font_bold);
    (void)u.text_area(log, g_log, "log"_id,
                      {.wrap = false, .read_only = true, .line_numbers = true});
}

int main(int argc, char **argv)
{
    return example_run("textarea", 860, 520, argc, argv, frame);
}
