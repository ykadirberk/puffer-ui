// buttons — labels, roles, per-call overrides and scoped restyling.
//
// Shows: `button` + `button_role` (`set_button_role`), `button_override`,
// `style_scope`, `resolve_button_style`, and hover-color transitions.
//
//   pui_ex_buttons
#include "../example_common.h"

static i32 g_clicks = 0;

static void buttons_frame(ui &u, example_app &app)
{
    const theme &th = u.th();
    example_page page =
        example_begin_page(u, app, "Buttons", "labels, roles, overrides and scoped restyling");
    column col(page.content, example_ui::SECTION_GAP);

    {
        const rect body = example_section(u, col.next(146.0f), "Button roles",
                                          "u.button(rect, label, id, role_id) - role styles come "
                                          "from the theme",
                                          app.font_bold);
        column c(body, 6.0f);
        row r(c.next(30.0f), 8.0f);
        if (u.button(r.next(120.0f), "Default", "btn_default"_id)) g_clicks += 1;
        if (u.button(r.next(120.0f), "Primary", "btn_primary"_id, "primary"_id)) g_clicks += 1;
        if (u.button(r.next(120.0f), "Danger", "btn_danger"_id, "danger"_id)) g_clicks += 1;
        char label[32];
        std::snprintf(label, sizeof(label), "clicks: %d", g_clicks);
        u.text(r.remaining(), label, th.text_dim, ALIGN_RIGHT);

        // Roles are theme entries; resolve_button_style shows what a role resolves to.
        {
            const button_style st = u.resolve_button_style("primary"_id);
            char line[160];
            std::snprintf(line, sizeof(line),
                          "resolve_button_style(\"primary\"): radius %.1f, pad_x %.1f, border %.1f",
                          static_cast<double>(st.radius), static_cast<double>(st.pad_x),
                          static_cast<double>(st.border_thickness));
            u.text(c.next(18.0f), line, th.text_dim, ALIGN_LEFT);
        }
        u.text(c.next(18.0f), "hover/active colors animate when theme.button.transition is set",
               th.text_dim, ALIGN_LEFT);
    }

    {
        const rect body = example_section(u, col.next(140.0f), "Per-call overrides",
                                          "button_override per call, or style_scope around a group",
                                          app.font_bold);
        column c(body, 8.0f);
        row r(c.next(34.0f), 8.0f);
        u.button(r.next(130.0f), "square", "btn_sq"_id, 0, button_override{.radius = some(0.0f)});
        u.button(r.next(140.0f), "outlined", "btn_out"_id, 0,
                 button_override{.bg = some(color{0, 0, 0, 0}),
                                 .border = some(th.accent),
                                 .border_thickness = some(1.5f)});
        u.button(r.next(150.0f), "pill", "btn_pill"_id, 0,
                 button_override{.radius = some(15.0f), .pad_x = some(18.0f)});

        // style_scope restyles every button drawn inside it.
        style_scope warn(u, button_override{.bg = some(color{200, 150, 40, 255}),
                                            .hover_bg = some(color{230, 180, 60, 255}),
                                            .text = some(color{25, 25, 25, 255})});
        row r2(c.next(30.0f), 8.0f);
        u.button(r2.next(150.0f), "scoped A", "btn_scope_a"_id);
        u.button(r2.next(150.0f), "scoped B", "btn_scope_b"_id);
    }
}

int main(int argc, char **argv)
{
    return example_run("buttons", 640, 420, argc, argv, buttons_frame);
}
