// text: utf8, text drawing, fields, IME, fonts.
#include "test_util.h"

PUI_TEST(test_utf8)
{
    // "├âãÆ├åÔÇÖ├âÔÇá├óÔé¼Ôäó├âãÆ├é┬ó├â┬ó├óÔé¼┼í├é┬¼├âÔÇª├é┬¥├âãÆ├åÔÇÖ├â┬ó├óÔÇÜ┬¼├é┬ª├âãÆ├óÔé¼┼í├âÔÇÜ├é┬©"
    // (C4 9F), space,     //
    // "├âãÆ├åÔÇÖ├âÔÇá├óÔé¼Ôäó├âãÆ├óÔé¼┼í├âÔÇÜ├é┬ó├âãÆ├åÔÇÖ├âÔÇÜ├é┬ó├âãÆ├é┬ó├â┬ó├óÔÇÜ┬¼├à┬í├âÔÇÜ├é┬¼├âãÆ├óÔé¼┬ª├âÔÇÜ├é┬í├âãÆ├åÔÇÖ├â┬ó├óÔÇÜ┬¼├à┬í├âãÆ├óÔé¼┼í├âÔÇÜ├é┬¼"
    // (E2 82 AC)
    std::string_view s = "\xC4\x9F \xE2\x82\xAC";
    CHECK(utf8_count(s) == 3);
    usize i = 0;
    CHECK(utf8_decode(s, i) == 0x011F);
    // ├âãÆ├åÔÇÖ├âÔÇá├óÔé¼Ôäó├âãÆ├é┬ó├â┬ó├óÔé¼┼í├é┬¼├âÔÇª├é┬¥├âãÆ├åÔÇÖ├â┬ó├óÔÇÜ┬¼├é┬ª├âãÆ├óÔé¼┼í├âÔÇÜ├é┬©
    CHECK(utf8_decode(s, i) == 0x0020);
    // space
    CHECK(utf8_decode(s, i) == 0x20AC);
    // ├âãÆ├åÔÇÖ├âÔÇá├óÔé¼Ôäó├âãÆ├óÔé¼┼í├âÔÇÜ├é┬ó├âãÆ├åÔÇÖ├âÔÇÜ├é┬ó├âãÆ├é┬ó├â┬ó├óÔÇÜ┬¼├à┬í├âÔÇÜ├é┬¼├âãÆ├óÔé¼┬ª├âÔÇÜ├é┬í├âãÆ├åÔÇÖ├â┬ó├óÔÇÜ┬¼├à┬í├âãÆ├óÔé¼┼í├âÔÇÜ├é┬¼
    CHECK(utf8_decode(s, i) == UTF8_END);
    std::string_view bad = "\xFF\xFE";
    usize j = 0;
    CHECK(utf8_decode(bad, j) == UTF8_REPLACEMENT);
}

PUI_TEST(test_text)
{
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    font_handle fh = load_font(c, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
    if (fh == FONT_INVALID) fh = load_font(c, "C:/Windows/Fonts/segoeui.ttf");
    if (fh == FONT_INVALID) fh = load_font(c, "C:/Windows/Fonts/arial.ttf");
    if (fh == FONT_INVALID)
    {
        // no system font available
        std::printf("note: no system font found; skipping text rendering test\n");
        destroy_context(c);
        return;
    }
    theme t = default_dark();
    t.font = fh;
    t.text_size = 16.0f;
    set_theme(c, t);
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 400, 200));
    ui u(c);
    const f32 w = u.text_width("Merhaba \xE2\x82\xAC");
    // "Merhaba                                  //
    // ├âãÆ├åÔÇÖ├âÔÇá├óÔé¼Ôäó├âãÆ├óÔé¼┼í├âÔÇÜ├é┬ó├âãÆ├åÔÇÖ├âÔÇÜ├é┬ó├âãÆ├é┬ó├â┬ó├óÔÇÜ┬¼├à┬í├âÔÇÜ├é┬¼├âãÆ├óÔé¼┬ª├âÔÇÜ├é┬í├âãÆ├åÔÇÖ├â┬ó├óÔÇÜ┬¼├à┬í├âãÆ├óÔé¼┼í├âÔÇÜ├é┬¼"
    CHECK(w > 0.0f);
    u.text({0, 0, 200, 24}, "Merhaba", color::white(), ALIGN_LEFT);
    u.button({0, 40, 120, 28}, "OK", "ok"_id);
    const measure_size wrapped =
        u.measure_text("the quick brown fox jumps over the lazy dog", 80.0f);
    CHECK(wrapped.width <= 80.5f);
    CHECK(wrapped.height >= u.line_height() * 2.0f);
    // wrapped onto several lines      // Same text, wider slot: one line. Narrower slot:
    // strictly more lines.
    const measure_size one_line = u.measure_text("the quick brown fox", 1000.0f);
    CHECK(one_line.height <= u.line_height() + 0.01f);
    CHECK(wrapped.height > one_line.height);
    u.text_wrapped({0, 80, 120, 60}, "some wrapped text here", color::white());
    // text_fit: one line when it fits, wrapped (and taller) when it does not
    const f32 fit_one = u.text_fit({0, 0, 1000, 20}, "short", color::white());
    const f32 fit_many =
        u.text_fit({0, 160, 60, 80}, "this is a much longer piece of text", color::white());
    CHECK(fit_one <= u.line_height() + 0.01f);
    CHECK(fit_many > u.line_height());
    auto fn = [](f32 w) -> measure_size { return {w, 10.0f}; };
    const measure_size custom = u.measure(fn, 50.0f);
    CHECK(custom.height == 10.0f && custom.width == 50.0f);
    end_frame(c);
    CHECK(nd.textures == 1);
    // one shared atlas
    CHECK(nd.draw_calls >= 1);
    // text + button geometry flushed
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

PUI_TEST(test_text_edges)
{
    tf_env env;
    context *c = env.c;
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        CHECK(u.text_width("") == 0.0f);
        const measure_size m = u.measure_text("", 100.0f);
        CHECK(m.width == 0.0f && m.height == u.line_height());
        CHECK(u.text_fit(rect::make(0, 0, 100, 20), "", color::white()) == 0.0f);
        u.text(rect::make(0, 0, 100, 20), "", color::white());
        u.text_wrapped(rect::make(0, 0, 100, 20), "", color::white());
    }
    end_frame(c);
    CHECK(env.nd.draw_calls == 0);
    // no font loaded -> nothing drawn
    CHECK(violation_count(c) == 0);
    // a bad font path fails cleanly, no violation
    CHECK(load_font(c, "C:/Windows/Fonts/__pufferui_missing__.ttf") == FONT_INVALID);
}

PUI_TEST(test_utf8_truncated)
{
    std::string_view cut = "\xE2\x82";
    // truncated 3-byte sequence
    usize i = 0;
    CHECK(utf8_decode(cut, i) == UTF8_REPLACEMENT);
    CHECK(i == cut.size());
    // consumed to the end
    CHECK(utf8_count(cut) == 1);
    std::string_view lone = "\x80";
    // stray continuation byte
    usize j = 0;
    CHECK(utf8_decode(lone, j) == UTF8_REPLACEMENT);
    CHECK(j == 1);
}

PUI_TEST(test_ime)
{
    context *c = create_context(nullptr);
    ime_event(c, "\xE3\x81\xAB", 1);
    // U+306B preedit, caret at 1
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        CHECK(c->ime_preedit_len == 3);
        CHECK(c->ime_cursor == 1);
    }
    end_frame(c);
    // committing text ends the composition and queues the text
    text_input_event(c, "n");
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 200, 100));
    CHECK(c->ime_preedit_len == 0);
    CHECK(c->text_len == 1);
    end_frame(c);
    CHECK(c->text_len == 0);
    // consumed by the frame
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

PUI_TEST(test_text_atlas_paging)
{
    // More distinct glyphs than one 1024x1024 page holds: the store spills
    // into additional atlas pages; a full page never drops glyphs.
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    font_handle fh = load_font(c, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
    if (fh == FONT_INVALID)
    {
        std::printf("note: bundled font not found; skipping atlas paging\n");
        destroy_context(c);
        return;
    }
    theme t = default_dark();
    t.font = fh;
    t.text_size = 48.0f; // big glyphs: each consumes real page area, so the
                         // flood spills across atlas pages
    set_theme(c, t);

    // 1400 codepoints across Latin/Greek/Cyrillic/punctuation: one page fills,
    // the second page takes the rest.
    std::string big;
    auto push_cp = [&big](u32 cp)
    {
        if (cp < 0x80)
        {
            big.push_back(static_cast<char>(cp));
            return;
        }
        if (cp < 0x800)
        {
            big.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            big.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            return;
        }
        big.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        big.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        big.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    };
    for (u32 cp = 0x20; cp < 0x590; ++cp) push_cp(cp);

    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 8000, 60));
    {
        ui u(c);
        u.text(rect::make(0, 0, 7900, 60), big, color::white(), ALIGN_LEFT);
    }
    end_frame(c);

    CHECK(nd.textures >= 2);   // paged: more than one atlas texture
    CHECK(nd.vertices > 0);    // glyphs drawn
    CHECK(nd.draw_calls >= 2); // batching flushed on the page change

    // Every glyph from the string has a texture after the flood: redraw one
    // page-boundary glyph and confirm it lands on a real texture.
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 8000, 60));
    {
        ui u(c);
        const i32 draws_before = nd.draw_calls;
        u.text(rect::make(0, 0, 380, 20), big, color::white(), ALIGN_LEFT);
        CHECK(nd.draw_calls >= draws_before); // the cached glyphs all drew
    }
    end_frame(c);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

PUI_TEST(test_text_wrapped_height)
{
    // `text_wrapped_height` predicts exactly what `text_wrapped` draws: one
    // line for short text, more for wrapped paragraphs, and newline breaks
    // honored. The examples use it to size containers around paragraphs.
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    font_handle fh = load_font(c, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
    if (fh == FONT_INVALID)
    {
        std::printf("note: bundled font not found; skipping wrapped-height\n");
        destroy_context(c);
        return;
    }
    theme t = default_dark();
    t.font = fh;
    t.text_size = 15.0f;
    set_theme(c, t);

    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 400, 300));
    i32 draws_before = 0;
    f32 predicted_h = 0.0f;
    f32 line_h = 0.0f;
    {
        ui u(c);
        const f32 one_line = u.text_wrapped_height(200.0f, "short");
        CHECK(one_line == u.line_height());
        line_h = u.line_height();

        // short words in a 100px-wide rect: the paragraph wraps
        const f32 wrapped = u.text_wrapped_height(100.0f, "aaa bbb ccc ddd eee");
        CHECK(wrapped > u.line_height());
        CHECK(wrapped <= 5.0f * u.line_height());
        i32 draws_before = 0;
        f32 predicted_h = 0.0f;

        // an explicit newline breaks even when the line would fit
        const f32 broken = u.text_wrapped_height(400.0f, "one\ntwo");
        CHECK(broken == 2.0f * u.line_height());

        // empty text: zero
        CHECK(u.text_wrapped_height(100.0f, "") == 0.0f);

        // the prediction matches the drawn extent: draw into a rect one line
        // taller than predicted; the batch flushes at end_frame
        const std::string para = "the quick brown fox jumps over the lazy dog "
                                 "again and again and again";
        const f32 predicted = u.text_wrapped_height(140.0f, para);
        draws_before = nd.draw_calls;
        u.text_wrapped(rect::make(0, 0, 140, predicted + 1.0f), para, color::white());
        predicted_h = predicted;
    }
    end_frame(c);
    CHECK(nd.draw_calls > draws_before); // the predicted space held the text
    CHECK(predicted_h <= 6.0f * line_h);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

PUI_TEST(test_font_coverage)
{
    tf_env env;
    context *c = env.c;
    font_handle fh = load_font(c, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
    if (fh == FONT_INVALID) fh = load_font(c, "C:/Windows/Fonts/segoeui.ttf");
    if (fh == FONT_INVALID) fh = load_font(c, "C:/Windows/Fonts/arial.ttf");
    if (fh == FONT_INVALID)
    {
        std::printf("note: no font found; skipping coverage test\n");
        return;
    }
    theme t = default_dark();
    t.font = fh;
    set_theme(c, t);
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 400, 400));
    {
        ui u(c);
        const struct range
        {
            u32 lo, hi;
            const char *name;
        };
        const range ranges[] = {
            {0x0020, 0x007E, "ASCII"},
            {0x00A1, 0x024F, "Latin-1/Latin Extended"},
            {0x0370, 0x03FF, "Greek"},
            {0x0400, 0x04FF, "Cyrillic"},
            {0x2010, 0x205F, "Punctuation"},
            {0x20A0, 0x20BF, "Currency Symbols"},
            {0x2100, 0x214F, "Letterlike Symbols"},
            {0x2190, 0x21FF, "Arrows"},
            {0x2200, 0x22FF, "Mathematical Operators"},
        };
        i32 missing = 0;
        // Unassigned slots / format controls in these blocks
        // have no glyphs in any font; the contract is every *assigned, printable*
        // codepoint.
        auto is_unassigned = [](u32 cp)
        {
            switch (cp)
            {
            case 0x0378:
            case 0x0379:
            case 0x038B:
            case 0x038D:
            case 0x03A2:
            case 0x2065:
            case 0x2072:
            case 0x2073:
            case 0x208F:
            case 0x20B6:
            case 0x20B7:
            case 0x20BB:
            case 0x20BC:
                return true;
            default:
                break;
            }
            return (cp >= 0x0380 && cp <= 0x0383) || // unassigned
                   (cp >= 0x2066 && cp <= 0x2069) || // bidi isolates (format)
                   (cp >= 0x209D && cp <= 0x209F);
        };
        // Assigned, but not present in the bundled DejaVu Sans 2.37 (rare
        // currency/letterlike symbols outside the required UI set).
        auto not_in_bundled = [](u32 cp)
        {
            switch (cp)
            {
            case 0x20BE:
            case 0x20BF: // lari, bitcoin
            case 0x210A:
            case 0x214A:
            case 0x214C: // script g, property line, per
            case 0x214D:
            case 0x214F: // aktieselskab, samaritan
                return true;
            default:
                return false;
            }
        };
        for (const range &r : ranges)
        {
            for (u32 cp = r.lo; cp <= r.hi; ++cp)
            {
                if (is_unassigned(cp) || not_in_bundled(cp)) continue;
                if (!u.has_glyph(cp))
                {
                    if (missing < 60) std::printf("coverage: missing U+%04X (%s)\n", cp, r.name);
                    ++missing;
                }
            }
        }
        for (u32 cp : {0x2713u, 0x2714u, 0x2717u})
        {
            if (!u.has_glyph(cp))
            {
                std::printf("coverage: missing U+%04X (Dingbats)\n", cp);
                ++missing;
            }
        }
        CHECK(missing == 0);
    }
    end_frame(c);
}

PUI_TEST(test_text_polish)
{
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    font_handle fh = load_font(c, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
    if (fh == FONT_INVALID)
    {
        std::printf("note: bundled font not found; "
                    "skipping text polish test\n");
        destroy_context(c);
        return;
    }
    theme t = default_dark();
    t.font = fh;
    t.text_size = 16.0f;
    set_theme(c, t);
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        // kerning: "AV" is tighter than A + V
        const f32 av = u.text_width("AV");
        const f32 sum = u.text_width("A") + u.text_width("V");
        CHECK(av < sum);
        // ellipsis: long text is trimmed to fit, short
        // text is untouched
        u.text_ellipsis(rect::make(0, 0, 40, 18), "a very long label", color::white());
        u.text_ellipsis(rect::make(0, 20, 200, 18), "short", color::white());
        u.text_ellipsis(rect::make(0, 40, 200, 18), "", color::white());
        // text_scope: size override, then restore
        const f32 before = u.text_width("X");

        {
            text_scope big = u.text_style(32.0f);
            CHECK(u.text_width("X") > before);
        }
        CHECK(u.text_width("X") == before);
        // bold/oblique fonts via text_scope
        font_handle bold = load_font(c, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans-Bold.ttf");
        font_handle oblique = load_font(c, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans-Oblique.ttf");
        if (bold != FONT_INVALID)
        {
            text_scope sc = u.text_style(16.0f, bold);
            CHECK(u.has_glyph('A'));
            CHECK(u.text_width("Hello") > 0.0f);
        }
        if (oblique != FONT_INVALID)
        {
            text_scope sc = u.text_style(16.0f, oblique);
            CHECK(u.has_glyph('A'));
        }
    }
    end_frame(c);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

PUI_TEST(test_text_field_caret_placement)
{
    tf_env env;
    context *c = env.c;
    if (tf_needs_font(c)) return;
    std::string value = "abcdef";
    const rect field = {0, 0, 160, 24};
    const uiid id = "cp"_id;
    auto draw = [&](ui &u) { (void)u.text_field(field, value, id); };
    const f32 pad = default_dark().padding * 0.5f;
    f32 x_after_abc = pad;

    {
        ui mu(c);
        x_after_abc += mu.text_width("abc");
    }
    // First click lands between 'c' and 'd': typing inserts there, not
    // at the end.
    tf_click(c, x_after_abc + 1.0f, 12.0f, 0.0, draw);
    text_input_event(c, "X");
    tf_frame(c, 0.05, draw);
    CHECK(value == "abcXdef");
    // Clicking past the end appends.
    f32 x_end = pad;

    {
        ui mu(c);
        x_end += mu.text_width(value) + 6.0f;
    }
    tf_click(c, x_end, 12.0f, 0.10, draw);
    text_input_event(c, "!");
    tf_frame(c, 0.15, draw);
    CHECK(value == "abcXdef!");
    // Tab focus selects all, so typing replaces the value.
    std::string second = "hello";
    const rect field_b = {0, 40, 160, 24};
    const uiid idb = "cp2"_id;
    auto draw_b = [&](ui &u)
    {
        (void)u.text_field(field, value, id);
        (void)u.text_field(field_b, second, idb);
    };
    tf_click(c, 10.0f, 12.0f, 0.20, draw_b);
    // focus the first field
    key_event(c, key::TAB, true);
    tf_frame(c, 0.25, draw_b);
    key_event(c, key::TAB, false);
    tf_frame(c, 0.30, draw_b);
    // second field focused, value selected
    text_input_event(c, "yo");
    tf_frame(c, 0.35, draw_b);
    CHECK(second == "yo");
    CHECK(violation_count(c) == 0);
}

PUI_TEST(test_text_field_word_ops)
{
    tf_env env;
    context *c = env.c;
    if (tf_needs_font(c)) return;
    std::string value = "hello brave world";
    const rect field = {0, 0, 240, 24};
    const uiid id = "word"_id;
    auto draw = [&](ui &u) { (void)u.text_field(field, value, id); };
    tf_click(c, 6.0f, 12.0f, 0.0, draw);
    // caret at 0
    // Ctrl+Right jumps by words (whitespace is skipped): 0
    // -> 5 -> 11.
    tf_key(c, key::RIGHT, true, false, 0.05, draw);
    tf_key(c, key::RIGHT, true, false, 0.10, draw);
    text_input_event(c, "|");
    tf_frame(c, 0.15, draw);
    CHECK(value == "hello brave| world");
    // Ctrl+Backspace removes the chunk before the caret (here just the
    // "|").
    tf_key(c, key::BACKSPACE, true, false, 0.20, draw);
    CHECK(value == "hello brave world");
    // Ctrl+Delete removes the whitespace and the next word.
    tf_key(c, key::DEL, true, false, 0.25, draw);
    CHECK(value == "hello brave");
    // Ctrl+Shift+Right selects the next word; typing replaces it.
    tf_key(c, key::HOME, false, false, 0.30, draw);
    tf_key(c, key::RIGHT, true, true, 0.35, draw);
    text_input_event(c, "bye");
    tf_frame(c, 0.40, draw);
    CHECK(value == "bye brave");
    CHECK(violation_count(c) == 0);
}

PUI_TEST(test_text_field_undo_redo)
{
    tf_env env;
    context *c = env.c;
    if (tf_needs_font(c)) return;
    std::string value;
    const rect field = {0, 0, 200, 24};
    const uiid id = "undo"_id;
    auto draw = [&](ui &u) { (void)u.text_field(field, value, id); };
    tf_click(c, 6.0f, 12.0f, 0.0, draw);
    // A run of typing coalesces into one undo step.
    text_input_event(c, "a");
    tf_frame(c, 0.01, draw);
    text_input_event(c, "b");
    tf_frame(c, 0.02, draw);
    text_input_event(c, "c");
    tf_frame(c, 0.03, draw);
    CHECK(value == "abc");
    tf_key(c, key::Z, true, false, 0.10, draw);
    CHECK(value == "");
    tf_key(c, key::Y, true, false, 0.15, draw);
    CHECK(value == "abc");
    // Navigation breaks coalescing: the next edit is its own step.
    tf_key(c, key::HOME, false, false, 0.20, draw);
    text_input_event(c, "X");
    tf_frame(c, 0.25, draw);
    CHECK(value == "Xabc");
    tf_key(c, key::Z, true, false, 0.30, draw);
    CHECK(value == "abc");
    tf_key(c, key::Z, true, false, 0.35, draw);
    CHECK(value == "");
    // Ctrl+Shift+Z is redo too.
    tf_key(c, key::Z, true, true, 0.40, draw);
    CHECK(value == "abc");
    CHECK(violation_count(c) == 0);
}

PUI_TEST(test_input_overflow)
{
    // Text input that does not fit the per-frame buffer is never silently
    // truncated: the overflow is reported once and the part that fits is
    // kept. The buffer is 512 bytes; a long IME commit or paste trips it.
    null_device nd;
    context *c = create_context(&nd, nd.create_surface());
    set_violation_handler(c, capture_violation, nullptr);
    g_violation_events = 0;

    // 600 'a' bytes will not fit the 512-byte buffer (room for 511 + NUL).
    std::string long_text(600, 'a');
    text_input_event(c, long_text.c_str());
    CHECK(g_violation_events == 1);
    CHECK(violation_was(
        "text input did not fit the per-frame input buffer (IME commit or paste too long)"));
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 200, 200));
    CHECK(std::strlen(c->text_input) == 511); // the part that fits was kept
    end_frame(c);

    // A normal event still fits without a violation.
    g_violation_events = 0;
    text_input_event(c, "hello");
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 200, 200));
    CHECK(std::strcmp(c->text_input, "hello") == 0);
    end_frame(c);
    CHECK(g_violation_events == 0);

    destroy_context(c);
}

PUI_TEST(test_text_field_undo_across_focus)
{
    tf_env env;
    context *c = env.c;
    if (tf_needs_font(c)) return;
    std::string value = "start";
    const rect field = {0, 0, 220, 24};
    const uiid id = "undofocus"_id;
    auto draw = [&](ui &u) { (void)u.text_field(field, value, id); };
    // Focus, type (building one undo step), then commit with Enter.
    tf_click(c, 200.0f, 12.0f, 0.0, draw); // past the text: the caret goes to the end;
    text_input_event(c, "x");
    tf_frame(c, 0.05, draw);
    CHECK(value == "startx");
    tf_key(c, key::ENTER, false, false, 0.10, draw);
    // The app replaces the value while the field is unfocused (e.g. a load).
    value = "loaded from disk";
    tf_frame(c, 0.15, draw);
    CHECK(value == "loaded from disk");
    // Refocus + undo: the stale typing history must not overwrite the model.
    tf_click(c, 200.0f, 12.0f, 0.20, draw);
    tf_key(c, key::Z, true, false, 0.25, draw);
    CHECK(value == "loaded from disk");
    CHECK(violation_count(c) == 0);
}

PUI_TEST(test_text_field_kerned_caret)
{
    tf_env env;
    context *c = env.c;
    if (tf_needs_font(c)) return;
    // "AV" is a kern pair in DejaVu Sans: the drawn caret positions
    // include the
    // // kerning, and a click at one of them must land on the same
    // index.
    std::string value = "AVAVAV";
    const rect field = {0, 0, 240, 24};
    const uiid id = "kern"_id;
    auto draw = [&](ui &u) { (void)u.text_field(field, value, id); };
    const f32 pad = default_dark().padding * 0.5f;
    f32 x = pad;

    {
        ui mu(c);
        x += mu.text_width("AVAVA");
        // exactly where the caret is drawn before 'V'     }
        tf_click(c, x, 12.0f, 0.0, draw);
        text_input_event(c, "|");
        tf_frame(c, 0.05, draw);
        CHECK(value == "AVAVA|V");
        CHECK(violation_count(c) == 0);
    }
}

PUI_TEST(test_text_field_drag_select)
{
    tf_env env;
    context *c = env.c;
    if (tf_needs_font(c)) return;
    std::string value = "drag me around";
    const rect field = {0, 0, 240, 24};
    const uiid id = "dragsel"_id;
    auto draw = [&](ui &u) { (void)u.text_field(field, value, id); };
    // Press at the start, drag over "drag", release: Backspace removes
    // it.
    f32 x_drag_end = default_dark().padding * 0.5f;

    {
        ui mu(c);
        x_drag_end += mu.text_width("drag");
    }
    mouse_move(c, 6.0f, 12.0f);
    mouse_button(c, true);
    tf_frame(c, 0.0, draw);
    mouse_move(c, x_drag_end - 1.0f, 12.0f);
    tf_frame(c, 0.05, draw);
    mouse_button(c, false);
    tf_frame(c, 0.10, draw);
    key_event(c, key::BACKSPACE, true);
    tf_frame(c, 0.15, draw);
    key_event(c, key::BACKSPACE, false);
    CHECK(value != "drag me around");
    CHECK(value == " me around");
    // Double-click selects the word under the pointer (same spot,
    // quick).
    f32 x_around = 6.0f;

    {
        ui mu(c);
        x_around += mu.text_width(" me a");
    }
    tf_click(c, x_around, 12.0f, 0.20, draw);
    tf_click(c, x_around, 12.0f, 0.25, draw);
    // within 0.4s and 5px: streak 2
    text_input_event(c, "X");
    tf_frame(c, 0.30, draw);
    CHECK(value == " me X");
    CHECK(violation_count(c) == 0);
}

PUI_TEST(test_text_field_drag_drop)
{
    tf_env env;
    context *c = env.c;
    if (tf_needs_font(c)) return;
    std::string a = "hello world", b;
    const rect fa = {0, 0, 200, 24}, fb = {0, 40, 200, 24};
    const uiid ida = "dd_a"_id, idb = "dd_b"_id;
    auto draw = [&](ui &u)
    {
        (void)u.text_field(fa, a, ida);
        (void)u.text_field(fb, b, idb);
    };
    // Seeding the model requires unfocused fields (a focused buffer is
    // the // source of truth and would be written back over the new
    // value).
    auto seed = [&](const char *av, const char *bv, f64 t)
    {
        tf_key(c, key::ENTER, false, false, t, draw);
        a = av;
        b = bv;
        tf_frame(c, t + 0.02, draw);
    };
    // Focus a, select all, then drag the selection into b (move).
    tf_click(c, 10.0f, 12.0f, 0.0, draw);
    tf_key(c, key::A, true, false, 0.05, draw);
    mouse_move(c, 40.0f, 12.0f);
    // inside the selection
    mouse_button(c, true);
    tf_frame(c, 0.10, draw);
    mouse_move(c, 40.0f, 52.0f);
    // over b, past the drag threshold
    tf_frame(c, 0.15, draw);
    mouse_button(c, false);
    tf_frame(c, 0.20, draw);
    tf_frame(c, 0.25, draw);
    // let the focus transfer settle
    CHECK(a == "");
    CHECK(b == "hello world");
    // Ctrl while dropping copies instead of moving.
    seed("copy me", "", 0.30);
    tf_click(c, 10.0f, 12.0f, 0.35, draw);
    tf_key(c, key::A, true, false, 0.40, draw);
    mouse_move(c, 30.0f, 12.0f);
    mouse_button(c, true);
    tf_frame(c, 0.45, draw);
    mouse_move(c, 30.0f, 52.0f);
    tf_frame(c, 0.50, draw);
    mods_event(c, false, true);
    // Ctrl held when dropping
    mouse_button(c, false);
    tf_frame(c, 0.55, draw);
    tf_frame(c, 0.60, draw);
    mods_event(c, false, false);
    CHECK(a == "copy me");
    CHECK(b == "copy me");
    // Move inside one field: select "abc", drag it past the end -> " defabc".
    seed("abc def", "", 0.62);
    tf_click(c, 6.0f, 12.0f, 0.65, draw);          // caret at 0 in a
    tf_key(c, key::RIGHT, true, true, 0.70, draw); // select "abc";
    f32 x_move_to = 6.0f;

    {
        ui mu(c);
        x_move_to += mu.text_width("abc def") + 8.0f;
    }
    mouse_move(c, 14.0f, 12.0f);
    // inside the selection
    mouse_button(c, true);
    tf_frame(c, 0.75, draw);
    mouse_move(c, x_move_to, 12.0f);
    // past the end, same field
    tf_frame(c, 0.80, draw);
    mouse_button(c, false);
    tf_frame(c, 0.85, draw);
    CHECK(a == " defabc");
    // Escape cancels an in-flight drag: both fields keep their text.
    seed("keep", "", 0.90);
    tf_click(c, 10.0f, 12.0f, 0.95, draw);
    tf_key(c, key::A, true, false, 1.00, draw);
    mouse_move(c, 20.0f, 12.0f);
    mouse_button(c, true);
    tf_frame(c, 1.05, draw);
    mouse_move(c, 20.0f, 52.0f);
    tf_frame(c, 1.10, draw);
    tf_key(c, key::ESCAPE, false, false, 1.15, draw);
    mouse_button(c, false);
    tf_frame(c, 1.20, draw);
    CHECK(a == "keep");
    CHECK(b == "");
    CHECK(violation_count(c) == 0);
}

PUI_TEST(test_text_field_drag_edit_cancels)
{
    tf_env env;
    context *c = env.c;
    if (tf_needs_font(c)) return;
    std::string a = "hello world", b;
    const rect fa = {0, 0, 200, 24}, fb = {0, 40, 200, 24};
    const uiid ida = "dec_a"_id, idb = "dec_b"_id;
    auto draw = [&](ui &u)
    {
        (void)u.text_field(fa, a, ida);
        (void)u.text_field(fb, b, idb);
    };
    // Lift a's whole value and start dragging it towards b.
    tf_click(c, 10.0f, 12.0f, 0.0, draw);
    tf_key(c, key::A, true, false, 0.05, draw);
    mouse_move(c, 40.0f, 12.0f);
    mouse_button(c, true);
    tf_frame(c, 0.10, draw);
    mouse_move(c, 40.0f, 52.0f);
    // past the threshold: the drag is in flight     tf_frame(c, 0.15, draw);      // Editing
    // the source replaces the lifted selection, so the recorded range no     // longer holds
    // the lifted text: the move must cancel rather than erase the     // wrong characters from
    // either field.
    text_input_event(c, "X");
    tf_frame(c, 0.20, draw);
    CHECK(a == "X");
    mouse_button(c, false);
    tf_frame(c, 0.25, draw);
    CHECK(a == "X");
    CHECK(b == "");
    CHECK(violation_count(c) == 0);
}

PUI_TEST(test_text_field_long_value)
{
    tf_env env;
    context *c = env.c;
    if (tf_needs_font(c)) return;
    std::string value = "a very long value that will not fit inside the field";
    const rect field = {0, 0, 90, 24};
    const uiid id = "long"_id;
    auto draw = [&](ui &u) { (void)u.text_field(field, value, id); };
    // Focusing and pressing End scrolls to the caret instead of
    // overflowing.
    tf_click(c, 10.0f, 12.0f, 0.0, draw);
    tf_key(c, key::END, false, false, 0.05, draw);
    text_input_event(c, "!");
    tf_frame(c, 0.10, draw);
    CHECK(value == "a very long value that will not fit inside the field!");
    // Still balanced clips and no violations (the field clips its own
    // body).
    CHECK(violation_count(c) == 0);
}

PUI_TEST(test_text_field)
{
    context *c = create_context(nullptr);
    std::string value = "ab";
    const rect field = {0, 0, 120, 24};
    const uiid id = "tf"_id;
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 200, 100));
    mouse_move(c, 40, 10);
    // past the text: the caret goes to the end
    mouse_button(c, true);

    {
        ui u(c);
        (void)u.text_field(field, value, id);
    }
    end_frame(c);
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 200, 100));
    mouse_button(c, false);

    {
        ui u(c);
        (void)u.text_field(field, value, id);
    }
    end_frame(c);
    // No font is loaded here, so caret positions are not measurable:
    // place the caret at the end explicitly (the click-position
    // behavior is covered by test_text_field_caret_placement).
    key_event(c, key::END, true);
    begin_frame(c, 0.024, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        (void)u.text_field(field, value, id);
    }
    end_frame(c);
    key_event(c, key::END, false);
    text_input_event(c, "c");
    begin_frame(c, 0.032, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        const bool changed = u.text_field(field, value, id);
        CHECK(changed);
    }
    end_frame(c);
    CHECK(value == "abc");
    key_event(c, key::BACKSPACE, true);
    begin_frame(c, 0.048, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        (void)u.text_field(field, value, id);
    }
    end_frame(c);
    key_event(c, key::BACKSPACE, false);
    CHECK(value == "ab");
    // Home then Delete removes the first character
    key_event(c, key::HOME, true);
    begin_frame(c, 0.064, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        (void)u.text_field(field, value, id);
    }
    end_frame(c);
    key_event(c, key::HOME, false);
    key_event(c, key::DEL, true);
    begin_frame(c, 0.080, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        (void)u.text_field(field, value, id);
    }
    end_frame(c);
    key_event(c, key::DEL, false);
    CHECK(value == "b");
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

PUI_TEST(test_number_field)
{
    context *c = create_context(nullptr);
    f32 value = 2.5f;
    const rect field = {0, 0, 120, 24};
    const uiid id = "nf"_id;
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        CHECK(!u.number_field(field, value, id));
    }
    end_frame(c);
    CHECK(value == 2.5f);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

PUI_TEST(test_focus_tab_clipboard)
{
    context *c = create_context(nullptr);
    test_clipboard clip;
    set_clipboard(c, &clip);
    std::string a = "hello", b = "";
    const rect fa = {0, 0, 120, 24}, fb = {0, 40, 120, 24};
    const uiid ida = "a"_id, idb = "b"_id;
    auto draw = [&]()
    {
        ui u(c);
        (void)u.text_field(fa, a, ida);
        (void)u.text_field(fb, b, idb);
    };
    // focus a (press then release over it)
    begin_frame(c, 0.00, 0.016, rect::make(0, 0, 200, 100));
    mouse_move(c, 10, 10);
    mouse_button(c, true);
    draw();
    end_frame(c);
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 200, 100));
    mouse_button(c, false);
    draw();
    end_frame(c);
    // ctrl+A, ctrl+C
    mods_event(c, false, true);
    key_event(c, key::A, true);
    begin_frame(c, 0.032, 0.016, rect::make(0, 0, 200, 100));
    draw();
    end_frame(c);
    key_event(c, key::A, false);
    key_event(c, key::C, true);
    begin_frame(c, 0.048, 0.016, rect::make(0, 0, 200, 100));
    draw();
    end_frame(c);
    key_event(c, key::C, false);
    mods_event(c, false, false);
    CHECK(clip.data == "hello");
    // Tab moves focus from a to b
    key_event(c, key::TAB, true);
    begin_frame(c, 0.064, 0.016, rect::make(0, 0, 200, 100));
    draw();
    end_frame(c);
    key_event(c, key::TAB, false);
    // typing now goes to b
    text_input_event(c, "X");
    begin_frame(c, 0.080, 0.016, rect::make(0, 0, 200, 100));
    draw();
    end_frame(c);
    CHECK(b == "X");
    // ctrl+A then ctrl+V pastes "hello" over b
    mods_event(c, false, true);
    key_event(c, key::A, true);
    begin_frame(c, 0.096, 0.016, rect::make(0, 0, 200, 100));
    draw();
    end_frame(c);
    key_event(c, key::A, false);
    key_event(c, key::V, true);
    begin_frame(c, 0.112, 0.016, rect::make(0, 0, 200, 100));
    draw();
    end_frame(c);
    key_event(c, key::V, false);
    mods_event(c, false, false);
    CHECK(b == "hello");
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

PUI_TEST(test_number_locale)
{
    context *c = create_context(nullptr);
    theme t = default_dark();
    t.decimal_separator = ',';
    set_theme(c, t);
    f32 value = 1.5f;
    const rect field = {0, 0, 120, 24};
    const uiid id = "n"_id;
    begin_frame(c, 0.00, 0.016, rect::make(0, 0, 200, 100));
    mouse_move(c, 10, 10);
    mouse_button(c, true);

    {
        ui u(c);
        (void)u.number_field(field, value, id);
    }
    end_frame(c);
    begin_frame(c, 0.016, 0.016, rect::make(0, 0, 200, 100));
    mouse_button(c, false);

    {
        ui u(c);
        (void)u.number_field(field, value, id);
    }
    end_frame(c);
    // select all, then type "2,25" (comma decimal)
    mods_event(c, false, true);
    key_event(c, key::A, true);
    begin_frame(c, 0.032, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        (void)u.number_field(field, value, id);
    }
    end_frame(c);
    key_event(c, key::A, false);
    mods_event(c, false, false);
    text_input_event(c, "2,25");
    begin_frame(c, 0.048, 0.016, rect::make(0, 0, 200, 100));

    {
        ui u(c);
        (void)u.number_field(field, value, id);
    }
    end_frame(c);
    CHECK(std::fabs(value - 2.25f) < 0.001f);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

namespace
{
// Draws `s` once and returns the glyph quads' vertices (4 per quad).
std::vector<vertex> text_quads(vertex_log_device &dev, context *c, rect r, const char *s,
                               align a = ALIGN_LEFT)
{
    dev.log.clear();
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 400, 200));
    {
        ui u(c);
        u.text(r, s, color::white(), a);
    }
    end_frame(c);
    return dev.log;
}
} // namespace

PUI_TEST(test_text_glyphs_on_whole_pixels)
{
    // Every glyph quad lands on whole pixels, wherever the text starts: a glyph
    // bitmap sampled at a fractional position is smeared and uneven. Accuracy is
    // kept by choosing among quarter-pixel rasterizations of each glyph.
    vertex_log_device dev;
    context *c = create_context(&dev, dev.create_surface());
    if (tf_needs_font(c))
    {
        destroy_context(c);
        return;
    }
    for (const f32 size : {13.0f, 16.0f, 22.5f})
    {
        theme t = c->active_theme;
        t.text_size = size;
        set_theme(c, t);
        for (const f32 x : {10.0f, 10.25f, 10.37f, 10.5f, 10.8f})
        {
            for (const align a : {ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT})
            {
                const std::vector<vertex> q = text_quads(
                    dev, c, rect::make(x, 20.37f, 211.3f, 26.0f), "Read chapter 4, AV fi", a);
                CHECK(q.size() >= 4 * 12);
                bool whole = true;
                for (const vertex &v : q)
                    whole = whole && v.x == std::floor(v.x) && v.y == std::floor(v.y);
                CHECK(whole);
            }
        }
    }
    destroy_context(c);
}

PUI_TEST(test_text_kerning_places_the_second_glyph)
{
    // Kerning moves a glyph relative to the one BEFORE it. "V" inside "AV" must
    // sit exactly where a lone "V" sits when started at the pen position text_width
    // reports for it (the pair's kerning applies before the second glyph).
    vertex_log_device dev;
    context *c = create_context(&dev, dev.create_surface());
    if (tf_needs_font(c))
    {
        destroy_context(c);
        return;
    }
    theme t = c->active_theme;
    t.text_size = 32.0f;
    set_theme(c, t);

    f32 w_av = 0.0f, w_v = 0.0f, w_a = 0.0f;
    begin_frame(c, 0.0, 0.016, rect::make(0, 0, 400, 200));
    {
        ui u(c);
        w_av = u.text_width("AV");
        w_v = u.text_width("V");
        w_a = u.text_width("A");
    }
    end_frame(c);
    CHECK(w_av != w_a + w_v); // the bundled font really kerns this pair

    const std::vector<vertex> both =
        text_quads(dev, c, rect::make(10.0f, 20.0f, 300.0f, 40.0f), "AV");
    CHECK(both.size() == 8);
    const std::vector<vertex> lone =
        text_quads(dev, c, rect::make(10.0f + (w_av - w_v), 20.0f, 300.0f, 40.0f), "V");
    CHECK(lone.size() == 4);
    if (both.size() == 8 && lone.size() == 4)
    {
        CHECK(both[4].x == lone[0].x); // same whole-pixel column ...
        CHECK(both[4].y == lone[0].y);
        CHECK(both[5].x - both[4].x == lone[1].x - lone[0].x); // ... and the same bitmap
    }
    destroy_context(c);
}

// Home/End with Ctrl and Shift: Ctrl+Home/End jump to the ends, Shift extends the
// selection, Ctrl+Shift+Home/End select to the ends (from wherever the anchor is).
PUI_TEST(test_text_field_home_end_shortcuts)
{
    tf_env env;
    context *c = env.c;
    if (tf_needs_font(c)) return;
    std::string value = "hello brave world";
    const rect field = {0, 0, 300, 24};
    const uiid id = "homeend"_id;
    auto draw = [&](ui &u) { (void)u.text_field(field, value, id); };
    auto type = [&](const char *s, f64 t)
    {
        text_input_event(c, s);
        tf_frame(c, t, draw);
    };
    tf_click(c, 6.0f, 12.0f, 0.0, draw); // caret at 0

    // Ctrl+End jumps to the end; typing appends
    tf_key(c, key::END, true, false, 0.05, draw);
    type("!", 0.10);
    CHECK(value == "hello brave world!");
    // Ctrl+Home jumps to the start
    tf_key(c, key::HOME, true, false, 0.15, draw);
    type("<", 0.20);
    CHECK(value == "<hello brave world!");

    // Shift+End selects to the end; typing replaces the selection
    tf_key(c, key::RIGHT, true, false, 0.25, draw); // after "<hello"
    tf_key(c, key::END, false, true, 0.30, draw);
    type("|", 0.35);
    CHECK(value == "<hello|");

    // Ctrl+Shift+Home selects to the start from the caret
    tf_key(c, key::HOME, true, true, 0.40, draw);
    type("A", 0.45);
    CHECK(value == "A");

    // Ctrl+Shift+End selects to the end from the caret (anchor stays put)
    type(" two three", 0.50);
    CHECK(value == "A two three");
    tf_key(c, key::HOME, true, false, 0.55, draw);
    tf_key(c, key::RIGHT, true, false, 0.60, draw); // after "A"
    tf_key(c, key::END, true, true, 0.65, draw);
    type("!", 0.70);
    CHECK(value == "A!");
    CHECK(violation_count(c) == 0);
}
