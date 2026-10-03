// text — rendering, measurement, wrapping and fonts.
//
// Shows: `text` + alignment, `text_style` (size/bold/oblique), `text_ellipsis`,
// `text_wrapped`, `text_fit`, `text_width`, `measure_text`, `has_glyph`, UTF-8
// coverage and kerning.
//
//   pui_ex_text
#include "../example_common.h"

static void text_frame(ui &u, example_app &app)
{
    const theme &th = u.th();
    example_page page =
        example_begin_page(u, app, "Text", "sizes, fonts, alignment, wrapping and measurement");
    column col(page.content, example_ui::SECTION_GAP);

    {
        const rect body =
            example_section(u, col.next(166.0f), "Styles, alignment and UTF-8",
                            "text_style(size, font), ALIGN_LEFT/CENTER/RIGHT", app.font_bold);
        column c(body, 6.0f);
        {
            text_scope oblique = u.text_style(15.0f, app.font_oblique);
            u.text(c.next(20.0f), "oblique text via u.text_style(size, font)", th.text_dim,
                   ALIGN_LEFT);
        }
        {
            const rect r = c.next(22.0f);
            u.draw_rect(r, th.widget_bg);
            u.text(r, "ALIGN_LEFT", th.text, ALIGN_LEFT);
            u.text(r, "ALIGN_CENTER", th.text, ALIGN_CENTER);
            u.text(r, "ALIGN_RIGHT", th.text, ALIGN_RIGHT);
        }

        // UTF-8 is decoded per codepoint; the bundled DejaVu Sans covers the
        // documented set (Latin-1/Extended-A, Greek, punctuation, currency, math).
        u.text(c.next(20.0f),
               "T\xC3\xBCrk\xC3\xA7"
               "e \xC4\x9F\xC3\xBC\xC5\x9F\xC3\xB6 | "
               "\xCE\xB1\xCE\xB2\xCE\xB3 \xCE\x94 \xC2\xB5m | "
               "x \xC3\x97 y \xC3\xB7 \xC2\xB1 \xE2\x89\xA4 \xE2\x89\xA0 | "
               "\xE2\x82\xBA \xE2\x82\xAC \xC2\xB2 \xE2\x86\x92",
               th.text, ALIGN_LEFT);
        {
            u.textf(c.next(18.0f), th.text_dim, ALIGN_LEFT,
                    "has_glyph(U+2026 ellipsis) = %s   has_glyph(U+03C0 pi) = %s",
                    u.has_glyph(0x2026) ? "yes" : "no", u.has_glyph(0x03C0) ? "yes" : "no");
        }
    }

    {
        const rect body =
            example_section(u, col.next(210.0f), "Fit and measure",
                            "text_ellipsis, text_wrapped, text_fit and text_width", app.font_bold);
        column c(body, 6.0f);

        // Ellipsis trims on codepoint boundaries and appends U+2026.
        u.text_ellipsis(c.next(20.0f),
                        "This label is far too long for the rect and gets an ellipsis", th.text,
                        ALIGN_LEFT);

        // Wrapping and fitting.
        {
            const char *s = "text_wrapped breaks on words inside the rect and clips the rest.";
            const measure_size m = u.measure_text(s, c.remaining().w);
            u.text_wrapped(c.next(m.height), s, th.text_dim);
        }
        {
            const char *s = "text_fit wraps and reports the height it used";
            const rect r = c.next(38.0f);
            const f32 used = u.text_fit(r, s, th.text);
            u.textf(c.next(18.0f), th.text_dim, ALIGN_LEFT, "text_fit used %.0f px of %.0f",
                    static_cast<double>(used), static_cast<double>(r.h));
        }

        // Measurement + kerning are exact: layout with the same numbers you draw.
        {
            char line[192];
            std::snprintf(
                line, sizeof(line),
                "text_width(\"Hello\") = %.1f px   kerning: \"AV\" %.1f < \"A\"+\"V\" %.1f",
                static_cast<double>(u.text_width("Hello")), static_cast<double>(u.text_width("AV")),
                static_cast<double>(u.text_width("A") + u.text_width("V")));
            u.text(c.next(18.0f), line, th.text_dim, ALIGN_LEFT);
        }
    }
}

int main(int argc, char **argv)
{
    return example_run("text", 720, 500, argc, argv, text_frame);
}
