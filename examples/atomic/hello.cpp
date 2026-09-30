// hello — the smallest complete PufferUI program.
//
// Shows: context + window + frame loop, a background, text, a `column` cursor,
// `u.card`, `text_style` (bold), and raw drawing.
//
//   pui_ex_hello
//   pui_ex_hello --selftest
#include "../example_common.h"

static void hello_frame(ui &u, example_app &app)
{
    const theme &th = u.th();
    example_page page = example_begin_page(
        u, app, "Hello, PufferUI", "the smallest complete program: window, frame loop, card");

    // A column cursor slices the page content top-down with even spacing.
    column col(page.content, example_ui::SECTION_GAP);

    // A card is a themed rounded rect; lay content out inside its padding.
    {
        const rect body =
            example_section(u, col.next(86.0f), "The frame loop",
                            "begin_frame -> ui -> end_frame -> present", app.font_bold);
        u.text(body, "Run `pui_ex_hello --selftest` to see it run headless.", th.text_dim,
               ALIGN_LEFT);
    }

    // Raw drawing is always available for custom visuals.
    {
        const rect body =
            example_section(u, col.next(96.0f), "Raw drawing",
                            "draw_rounded_rect + text, no widget required", app.font_bold);
        const rect swatch = body.top_slice(28.0f);
        u.draw_rounded_rect(swatch, th.accent, 6.0f);
        u.text(swatch, "draw_rounded_rect", th.bg, ALIGN_CENTER);
    }
}

int main(int argc, char **argv)
{
    return example_run("hello", 640, 360, argc, argv, hello_frame);
}
