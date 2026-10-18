// ui::text_area (r102): multi-line editing on the shared editing path.
#include "test_util.h"

namespace
{
struct area_fixture
{
    tf_env env;
    std::string value;
    rect r = rect::make(0, 0, 200, 120);
    uiid id = "area"_id;
    text_area_opts opts{};
    f32 pad = 0.0f, line_h = 0.0f;
    bool ok = false;
    area_fixture(std::string v, text_area_opts o = {}) : value(std::move(v)), opts(o)
    {
        if (tf_needs_font(env.c)) return;
        pad = default_dark().padding * 0.5f;
        ui u(env.c);
        line_h = u.line_height();
        ok = true;
    }
    auto draw()
    {
        return [this](ui &u) { (void)u.text_area(r, value, id, opts); };
    }
    // The point inside row `row` at x offset `x` from the text's left edge.
    void click(f32 x, i32 row, f64 t)
    {
        tf_click(env.c, pad + x, pad + line_h * (static_cast<f32>(row) + 0.5f), t, draw());
    }
    void key(pui::key k, f64 t, bool ctrl = false, bool shift = false)
    {
        tf_key(env.c, k, ctrl, shift, t, draw());
    }
    void type(const char *s, f64 t)
    {
        text_input_event(env.c, s);
        tf_frame(env.c, t, draw());
    }
    pui::editor_info st()
    {
        ui u(env.c);
        return u.editor_info(id);
    }
};
} // namespace

PUI_TEST(test_text_area_typing_and_enter)
{
    area_fixture f("");
    if (!f.ok) return;
    f.click(4, 0, 0.0);
    f.type("ab", 0.05);
    f.key(key::ENTER, 0.10); // a new line, not a blur
    f.type("cd", 0.15);
    CHECK(f.value == "ab\ncd");
    CHECK(f.env.c->focus == f.id);
    f.key(key::ESCAPE, 0.20); // Escape still leaves the field
    CHECK(f.env.c->focus == 0);
}

PUI_TEST(test_text_area_vertical_moves_keep_the_column)
{
    area_fixture f("abcde\nab\nabcde"); // rows 1 and 3 measure the same
    if (!f.ok) return;
    f.click(170, 0, 0.0); // past the end of row 1
    CHECK(f.st().caret == 5);
    f.key(key::DOWN, 0.05); // "ab" is shorter: its end
    CHECK(f.st().caret == 8);
    f.key(key::DOWN, 0.10); // back at the remembered column on row 3
    CHECK(f.st().caret == 14);
    f.key(key::UP, 0.15);
    f.key(key::UP, 0.20);
    CHECK(f.st().caret == 5);
    f.key(key::UP, 0.25); // past the first line: the document start (column forgotten)
    CHECK(f.st().caret == 0);
    f.key(key::DOWN, 0.30, false, true); // Shift extends the selection downwards
    CHECK(f.st().anchor == 0 && f.st().caret == 6);
}

PUI_TEST(test_text_area_home_end_and_triple_click)
{
    area_fixture f("first line\nsecond line\nthird");
    if (!f.ok) return;
    f.click(30, 1, 0.0); // inside "second line"
    f.key(key::HOME, 0.05);
    CHECK(f.st().caret == 11);
    f.key(key::END, 0.10);
    CHECK(f.st().caret == 22);
    f.key(key::END, 0.15, true); // Ctrl+End: the document
    CHECK(f.st().caret == f.value.size());
    f.key(key::HOME, 0.20, true);
    CHECK(f.st().caret == 0);
    // A triple click selects the line it lands on.
    f.click(20, 1, 1.0);
    f.click(20, 1, 1.05);
    f.click(20, 1, 1.10);
    CHECK(f.st().anchor == 11 && f.st().caret == 22);
}

PUI_TEST(test_text_area_wraps_and_scrolls)
{
    std::string words;
    for (i32 i = 0; i < 60; ++i) words += "word ";
    area_fixture f(words);
    if (!f.ok) return;
    tf_frame(f.env.c, 0.0, f.draw());
    const i32 visual = f.st().visual_lines;
    CHECK(visual > 3); // one paragraph, many wrapped lines
    // Ctrl+End at the bottom: the view scrolls so the caret shows.
    f.click(4, 0, 0.05);
    f.key(key::END, 0.10, true);
    tf_frame(f.env.c, 0.15, f.draw());
    {
        ui u(f.env.c);
        CHECK(u.scroll_offset(id_child(f.id, "scroll"_id)) > 0.0f);
    }
    CHECK(f.st().line == 0 && f.st().column == static_cast<i32>(words.size()));
    // End on a wrapped line stays on that line (one codepoint short of the wrap).
    f.key(key::HOME, 0.20, true);
    f.key(key::END, 0.25);
    const usize first_row_end = f.st().caret; // the first visual row's last codepoint
    CHECK(first_row_end > 0 && first_row_end < words.size() / 2);
    f.key(key::DOWN, 0.30); // the row below starts right after it
    CHECK(f.st().caret > first_row_end);
    // No wrap: one visual line per paragraph.
    area_fixture g(words, {.wrap = false});
    if (!g.ok) return;
    tf_frame(g.env.c, 0.0, g.draw());
    CHECK(g.st().visual_lines == 1);
}

PUI_TEST(test_text_area_read_only_and_undo)
{
    area_fixture f("keep\nme", {.read_only = true});
    if (!f.ok) return;
    test_clipboard clip;
    set_clipboard(f.env.c, &clip);
    f.click(4, 0, 0.0);
    f.type("xx", 0.05);
    f.key(key::ENTER, 0.10);
    CHECK(f.value == "keep\nme"); // nothing edits a read-only area
    f.key(key::A, 0.15, true);
    f.key(key::C, 0.20, true);
    CHECK(clip.data == "keep\nme"); // but selection and copy work

    area_fixture g("one");
    if (!g.ok) return;
    g.click(170, 0, 0.0);
    g.key(key::ENTER, 0.05);
    g.type("two", 0.10);
    CHECK(g.value == "one\ntwo");
    g.key(key::Z, 1.0, true); // undo the typing (coalesced), then the newline
    g.key(key::Z, 1.5, true);
    CHECK(g.value == "one");
}

PUI_TEST(test_text_area_draws_only_visible_lines)
{
    std::string log;
    for (i32 i = 0; i < 10000; ++i) log += "log line with some text in it\n";
    area_fixture f(log, {.wrap = false, .read_only = true});
    if (!f.ok) return;
    tf_frame(f.env.c, 0.0, f.draw());
    tf_frame(f.env.c, 0.016, f.draw());
    CHECK(f.st().visual_lines == 10001);
    // ~6 visible rows of ~30 glyphs: a few thousand vertices at most, not 10k rows.
    CHECK(f.env.nd.vertices < 6000);
}

// Regression: the drag-extension code ran in the press frame of a triple click
// and pulled a field's select-all back to a word boundary.
PUI_TEST(test_text_field_triple_click_selects_all)
{
    tf_env env;
    if (tf_needs_font(env.c)) return;
    std::string value = "hello brave world";
    const rect field = {0, 0, 240, 24};
    auto draw = [&](ui &u) { (void)u.text_field(field, value, "tri"_id); };
    tf_click(env.c, 30.0f, 12.0f, 0.0, draw);
    tf_click(env.c, 30.0f, 12.0f, 0.05, draw);
    tf_click(env.c, 30.0f, 12.0f, 0.10, draw);
    text_input_event(env.c, "X");
    tf_frame(env.c, 0.15, draw);
    CHECK(value == "X");
}

namespace
{
// The word wrap before r108, measuring every prefix with text_width: the
// reference the one-pass wrap must match break for break.
std::vector<usize> reference_wrap(ui &u, std::string_view s, f32 width)
{
    const auto next = [&](usize i)
    {
        usize j = i + 1;
        while (j < s.size() && (static_cast<unsigned char>(s[j]) & 0xC0) == 0x80) ++j;
        return j;
    };
    std::vector<usize> starts;
    usize start = 0;
    const usize e = s.size();
    while (start < e)
    {
        starts.push_back(start);
        usize fit = start, i = start;
        while (i < e)
        {
            usize j = i;
            while (j < e && s[j] == ' ') ++j;
            while (j < e && s[j] != ' ') j = next(j);
            if (u.text_width(s.substr(start, j - start)) <= width)
            {
                fit = i = j;
                continue;
            }
            if (fit == start)
            {
                usize k = next(start);
                fit = k;
                while (k < j)
                {
                    const usize nk = next(k);
                    if (u.text_width(s.substr(start, nk - start)) > width) break;
                    fit = k = nk;
                }
            }
            break;
        }
        while (fit < e && s[fit] == ' ') ++fit;
        start = fit;
    }
    return starts;
}
} // namespace

// r108: the wrap sums glyph advances in one pass instead of measuring each
// prefix; the breaks must stay exactly where prefix measuring put them
// (kerning pairs, multi-byte codepoints, a word wider than the line).
PUI_TEST(test_text_area_wrap_breaks_where_prefixes_measure)
{
    const std::string text = "To AVAV Wavy fiords: \xC3\x84rger \xC3\x96l \xC3\x9C"
                             "ber pairs like AT, Yo, Te. "
                             "Supercalifragilisticexpialidociousandthensome end.";
    for (const f32 w : {60.0f, 97.0f, 140.0f, 233.0f})
    {
        area_fixture f(text);
        if (!f.ok) return;
        f.r = rect::make(0, 0, w + 2.0f * f.pad + 2.0f, 1000); // wrap width == w
        std::vector<usize> expect;
        {
            ui u(f.env.c);
            expect = reference_wrap(u, text, w);
        }
        f.click(1, 0, 0.0);
        f.key(key::HOME, 0.05);
        std::vector<usize> got{f.st().caret};
        for (i32 n = 0; n < 64; ++n)
        {
            f.key(key::DOWN, 0.1 + 0.1 * n);
            f.key(key::HOME, 0.15 + 0.1 * n);
            if (f.st().caret == got.back()) break; // the last line
            got.push_back(f.st().caret);
        }
        CHECK(got == expect);
        CHECK(f.st().visual_lines == static_cast<i32>(expect.size()));
    }
}

// r108: the line cache keys on the buffer's edit generation, not a hash of the
// text: a model change of the same size must still re-lay the lines out.
PUI_TEST(test_text_area_relayouts_on_same_size_model_change)
{
    area_fixture f("a\nb\nc");
    if (!f.ok) return;
    tf_frame(f.env.c, 0.0, f.draw());
    CHECK(f.st().visual_lines == 3);
    f.value = "abcde"; // same size, one paragraph
    tf_frame(f.env.c, 0.016, f.draw());
    CHECK(f.st().visual_lines == 1);
    f.value = "ab\ncd";
    tf_frame(f.env.c, 0.032, f.draw());
    CHECK(f.st().visual_lines == 2);
}

namespace
{
// Every vertex position and color a text area draws on its third frame.
std::vector<vertex> area_frame(vertex_log_device &dev, context *c, std::string &v1, std::string &v2,
                               text_area_opts o)
{
    std::vector<vertex> out;
    for (i32 f = 0; f < 3; ++f)
    {
        dev.log.clear();
        begin_frame(c, 0.016 * f, 0.016, rect::make(0, 0, 400, 2000));
        {
            ui u(c);
            (void)u.text_area(rect::make(0, 0, 300, 1900), f == 0 ? v1 : v2, "ln"_id, o);
        }
        end_frame(c);
    }
    for (vertex v : dev.log)
    {
        v.u = v.v = 0.0f; // atlas placement depends on what was rasterized first
        out.push_back(v);
    }
    return out;
}
} // namespace

// r108: the gutter's paragraph count and each line's number are cached with the
// lines. After a same-size model change (99 -> 100 paragraphs: a wider gutter)
// the area must draw exactly what a fresh area showing the new text draws.
PUI_TEST(test_text_area_line_numbers_follow_model_changes)
{
    std::string v1, v2;
    for (i32 i = 0; i < 98; ++i) v1 += "x\n";
    v1 += "tail"; // 99 paragraphs
    for (i32 i = 0; i < 99; ++i) v2 += "x\n";
    v2 += "ta"; // 100 paragraphs, the same size
    CHECK(v1.size() == v2.size());
    for (const bool wrap : {false, true})
    {
        const text_area_opts o{.wrap = wrap, .line_numbers = true};
        vertex_log_device da, db;
        context *a = create_context(&da, da.create_surface());
        context *b = create_context(&db, db.create_surface());
        if (tf_needs_font(a) || tf_needs_font(b))
        {
            destroy_context(a);
            destroy_context(b);
            return;
        }
        const std::vector<vertex> changed = area_frame(da, a, v1, v2, o);
        const std::vector<vertex> fresh = area_frame(db, b, v2, v2, o);
        CHECK(changed.size() == fresh.size());
        bool same = changed.size() == fresh.size();
        for (usize i = 0; same && i < changed.size(); ++i)
            same = changed[i].x == fresh[i].x && changed[i].y == fresh[i].y &&
                   changed[i].c.a == fresh[i].c.a;
        CHECK(same);
        destroy_context(a);
        destroy_context(b);
    }
}
