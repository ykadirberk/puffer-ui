// inputs — text and number fields.
//
// Shows: `text_field`, `number_field`, focus (click / Tab), the app clipboard,
// key polling (`key_pressed`, `key_down`) and the IME hook.
//
//   pui_ex_inputs   (click a field, type, press Tab / Enter)
#include "../example_common.h"

static std::string g_name = "PufferUI";
static f32 g_amount = 12.5f;
static char g_status[96] = "press Enter to submit";

static void inputs_frame(ui &u, example_app &app)
{
    const theme &th = u.th();
    example_page page =
        example_begin_page(u, app, "Inputs", "text and number fields, focus and the clipboard");
    column col(page.content, example_ui::SECTION_GAP);

    {
        const rect body = example_section(u, col.next(124.0f), "Text and number fields",
                                          "fields write straight into app-owned state (no getters)",
                                          app.font_bold);
        column c(body, 8.0f);
        track_row form(c.next(30.0f),
                       {track_size::fixed(70.0f), track_size::flex(), track_size::fixed(80.0f),
                        track_size::fixed(110.0f)},
                       8.0f);
        u.text(form.next(), "Name", th.text, ALIGN_LEFT);
        u.text_field(form.next(), g_name, "in_name"_id);
        u.text(form.next(), "Amount", th.text, ALIGN_LEFT);
        u.number_field(form.next(), g_amount, "in_amount"_id, "%.2f");

        // Enter submits; `key_pressed` is true only on the press frame.
        if (u.key_pressed(key::ENTER))
            std::snprintf(g_status, sizeof(g_status), "submitted: %s = %.2f", g_name.c_str(),
                          static_cast<double>(g_amount));
        u.text(c.next(18.0f), g_status, th.text_dim, ALIGN_LEFT);
    }

    {
        const rect body = example_section(u, col.next(158.0f), "Editing keys and the clipboard",
                                          "Tab / Escape / Enter, word-wise editing, undo, dragging",
                                          app.font_bold);
        column c(body, 6.0f);
        u.text(c.next(18.0f),
               "Tab focuses the next field (selecting it), Escape clears focus, Enter commits",
               th.text_dim, ALIGN_LEFT);
        u.text(
            c.next(18.0f),
            "Ctrl+Left/Right jump words, Ctrl+Backspace/Delete delete them, Ctrl+Z / Ctrl+Y undo",
            th.text_dim, ALIGN_LEFT);
        u.text(c.next(18.0f),
               "drag to select, double-click a word, drag a selection to move it (Ctrl copies)",
               th.text_dim, ALIGN_LEFT);

        char line[128];
        std::snprintf(line, sizeof(line), "key_down(LEFT) = %s   key_pressed(TAB) = %s",
                      u.key_down(key::LEFT) ? "yes" : "no", u.key_pressed(key::TAB) ? "yes" : "no");
        u.text(c.next(18.0f), line, th.text_dim, ALIGN_LEFT);
    }

    // A second form with different widths (tracks make alignment easy).
    {
        const rect body =
            example_section(u, col.next(124.0f), "A second form",
                            "tracks keep the labels and fields aligned", app.font_bold);
        column c(body, 8.0f);
        track_row form(c.next(30.0f),
                       {track_size::fixed(70.0f), track_size::flex(), track_size::fixed(80.0f),
                        track_size::fixed(110.0f)},
                       8.0f);
        u.text(form.next(), "Note", th.text, ALIGN_LEFT);
        static std::string note = "IME text arrives through ime_event()";
        u.text_field(form.next(), note, "in_note"_id);
        u.text(form.next(), "", th.text, ALIGN_LEFT);
        (void)u.button(form.next(), "Clear", "in_clear"_id);
        u.text(c.next(18.0f),
               "SDL: SDL_EVENT_TEXT_INPUT -> text_input_event, SDL_EVENT_TEXT_EDITING -> ime_event",
               th.text_dim, ALIGN_LEFT);
    }
}

int main(int argc, char **argv)
{
    return example_run("inputs", 680, 540, argc, argv, inputs_frame);
}
