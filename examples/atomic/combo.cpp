// combo — dropdown selection, tooltips and context menus.
//
// Shows: `combo` (click to open, hover or Up/Down + Enter to pick), `tooltip`
// (delayed, then instant while you move between widgets), and `context_menu`
// (right-press opens at the pointer, picks feed back). All three paint at the
// end of the frame, so nothing drawn after their anchor covers them.
//
//   pui_ex_combo   (right-press the canvas; hover the buttons)
#include "../example_common.h"
#include <cstdarg>

static i32 g_kind = 1; // default layer kind: "Sprite"
static const i32 MAX_LOG = 6;
static char g_log[MAX_LOG][64];
static i32 g_log_count = 0;

static void log_line(const char *fmt, ...)
{
    // Newest first; the log shows at most MAX_LOG lines.
    char buf[64];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    for (i32 i = MAX_LOG - 1; i > 0; --i)
        std::snprintf(g_log[i], sizeof(g_log[i]), "%s", g_log[i - 1]);
    std::snprintf(g_log[0], sizeof(g_log[0]), "%s", buf);
    if (g_log_count < MAX_LOG) g_log_count += 1;
}

static void combo_frame(ui &u, example_app &app)
{
    const theme &th = u.th();

    // The r70 layout-overflow reporter runs in this example: any slice this
    // page requests beyond what remains is reported (and fails --selftest),
    // so the section budgets below cannot silently rot again.
    set_report_layout_overflow(app.ctx, true);

    example_page page =
        example_begin_page(u, app, "Combo, tooltip, context menu", "the popup-family widgets");

    // Section budgets (verified by the reporter): head 38 + divider 13 + card
    // padding 24 + the content itself, 3 gaps between the 4 sections.
    constexpr f32 H_COMBO = 103.0f;  // one 28px control row
    constexpr f32 H_TIPS = 103.0f;   // one 28px button row
    constexpr f32 H_CANVAS = 166.0f; // 56px canvas + 28px button row + gap
    constexpr f32 H_LOG = 130.0f;    // a scroll view: fits whatever the log holds
    constexpr f32 PAGE_H = H_COMBO + H_TIPS + H_CANVAS + H_LOG + 3.0f * example_ui::SECTION_GAP;

    scroll_view page_scroll = u.scroll(page.content, "cm_page"_id);
    page_scroll.set_content_height(PAGE_H);
    column col(page_scroll.content(), example_ui::SECTION_GAP);

    // --- combo: a dropdown backed by a popup --------------------------------
    static const char *kinds[4] = {"Sprite", "Emitter", "Light", "Camera"};
    {
        const rect body = example_section(u, col.next(H_COMBO), "Combo",
                                          "click opens; hover or Up/Down moves; "
                                          "Enter, Escape or outside click finishes",
                                          app.font_bold);
        column c(body, 6.0f);
        row r(c.next(example_ui::ROW_H), 10.0f);
        example_caption(u, r.next(64.0f), "new layer:");
        if (u.combo(r.next(150.0f), "", kinds, 4, g_kind, "cm_kind"_id, 120.0f))
        {
            // max_popup_height = 120: four rows at 26px do not all fit, so the
            // dropdown clamps, scrolls and never paints outside its panel.
            log_line("layer kind: %s", kinds[g_kind]);
        }
        example_caption(u, r.remaining(), "hover the buttons below, too");
    }

    // --- tooltip: the delayed+instant pattern -------------------------------
    {
        const rect body = example_section(u, col.next(H_TIPS), "Tooltips",
                                          "the first one waits ~0.5 s, then "
                                          "sliding between them opens instantly",
                                          app.font_bold);
        column c(body, 6.0f);
        row b(c.next(example_ui::ROW_H), 8.0f);
        struct tip_def
        {
            const char *label;
            const char *tip;
        };
        static const tip_def defs[3] = {{"Save", "Write the scene to disk"},
                                        {"Run", "Execute the active script"},
                                        {"Lint", "Check naming and ids"}};
        for (i32 i = 0; i < 3; ++i)
        {
            const rect item = b.next(96.0f);
            const uiid id = id_child("tip_btn"_id, static_cast<uiid>(i + 1));
            (void)u.button(item, defs[i].label, id);
            u.tooltip(item, id, defs[i].tip);
        }
    }

    // --- context menu on a canvas -------------------------------------------
    {
        const rect body =
            example_section(u, col.next(H_CANVAS), "Context menu",
                            "right-press the canvas; picking prints to the log", app.font_bold);
        column c(body, 6.0f);
        const rect canvas = c.next(56.0f);
        u.draw_rounded_rect(canvas, th.panel_bg, 6.0f);
        u.text(canvas, "canvas", th.text_dim, ALIGN_CENTER);
        static const char *actions[3] = {"Fit to view", "Duplicate", "Delete"};
        const i32 picked = u.context_menu("cm_canvas"_id, canvas, actions, 3);
        if (picked >= 0) log_line("canvas: %s", actions[picked]);

        row r(c.next(example_ui::ROW_H), 8.0f);
        if (u.button(r.next(120.0f), "Right me too", "cm_btn"_id)) log_line("button clicked");
        static const char *b_actions[2] = {"Reset", "Rename"};
        if (const i32 bp = u.context_menu("cm_button"_id, r.next(110.0f), b_actions, 2); bp >= 0)
            log_line("button: %s", b_actions[bp]);
    }

    // --- the log -------------------------------------------------------------
    {
        const rect body = example_section(u, col.next(H_LOG), "Event log",
                                          "what the widgets above did", app.font_bold);
        // The log scrolls; the height is set before laying out (the r17
        // first-frame fix), so no line ever clamps or draws on top of another.
        // The newest entry is first, so it is always visible.
        scroll_view log = u.scroll(body, "cm_log"_id);
        const f32 line_h = 16.0f;
        const i32 lines = (g_log_count == 0) ? 1 : g_log_count;
        log.set_content_height(static_cast<f32>(lines) * line_h +
                               static_cast<f32>(lines - 1) * 4.0f);
        column c(log.content(), 4.0f);
        if (g_log_count == 0) u.text(c.next(line_h), "(no events yet)", th.text_dim, ALIGN_LEFT);
        for (i32 i = 0; i < g_log_count; ++i)
            u.text(c.next(line_h), g_log[i], i == 0 ? th.text : th.text_dim, ALIGN_LEFT);
    }
}

int main(int argc, char **argv)
{
    return example_run("combo", 660, 560, argc, argv, combo_frame);
}
