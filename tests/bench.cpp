// pui_bench — the frame-cost benchmark. Headless (null_device): it
// measures the CORE's per-frame work (layout, interact, draw-list recording
// and flushing), not the renderer. The scene is the audit's benchmark shape:
// 2,000 buttons, 500 text rows, 50 rounded panels. Numbers land in
// docs/perf.md; relative deltas across revisions are what matter.
//
//   pui_bench [--frames N]     (default 300)
//
// Text-heavy scenes (one per flag; the default is the scene above):
//   --wrap-type    a 360 KB wrapped document whose model changes every frame
//   --wrap-resize  the same document, static, while the area width changes
//   --bigtext      a static 1 MB log with line numbers, scrolled to the end
//   --longlines    60 static no-wrap lines of 12,000 characters each
//   --vlist        a 200,000-row variable-height virtual_list
//   --vlist-kept   the same list with virtual_list_opts::heights_version set
//   --a11y         the default scene + a 1 MB text_area, accessibility on
#include <pufferui/pufferui.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace pui;

struct bench_device : null_device
{
    // null_device already counts draw_calls / vertices / targets; nothing to add.
};

static f32 g_slider_value = 0.5f;
static bool g_check_value = false;
static std::string g_field_value = "bench";

int main(int argc, char **argv)
{
    i32 frames = 300;
    bool textarea = false;       // --textarea: a 10k-line log in a text_area instead
    bool atlas = false;          // --atlas: 20 text sizes in rotation (glyph atlas growth)
    i32 budget = -1;             // --budget N: set_text_atlas_budget (default: the library's)
    const char *scene = nullptr; // one of the text scenes (see the header)
    for (i32 i = 1; i < argc; ++i)
    {
        if (std::strncmp(argv[i], "--frames", 8) == 0 && i + 1 < argc)
            frames = std::atoi(argv[i + 1]);
        if (std::strcmp(argv[i], "--textarea") == 0) textarea = true;
        if (std::strcmp(argv[i], "--atlas") == 0) atlas = true;
        if (std::strcmp(argv[i], "--budget") == 0 && i + 1 < argc) budget = std::atoi(argv[i + 1]);
        for (const char *s : {"--wrap-type", "--wrap-resize", "--bigtext", "--longlines", "--vlist",
                              "--vlist-kept", "--a11y"})
            if (std::strcmp(argv[i], s) == 0) scene = s + 2;
    }

    // Deterministic text for the text scenes.
    std::string doc, big, longl;
    if (scene)
    {
        static const char *const words[] = {"layout", "the",    "rect",  "cutting", "engine",
                                            "draws",  "every",  "frame", "from",    "scratch",
                                            "while",  "glyphs", "stay",  "cached",  "in",
                                            "a",      "shared", "atlas", "page",    "so"};
        u32 rng = 12345u;
        const auto word = [&]() -> const char *
        {
            rng = rng * 1664525u + 1013904223u;
            return words[(rng >> 16) % 20];
        };
        for (i32 p = 0; p < 300; ++p) // ~1,200 characters per paragraph
        {
            while (doc.size() < static_cast<usize>(p + 1) * 1200)
            {
                doc += word();
                doc += ' ';
            }
            doc += '\n';
        }
        for (i32 l = 0; big.size() < 1000000; ++l)
            big += "12:00:00.000 info  worker " + std::to_string(l % 16) +
                   ": processed a batch of records\n";
        for (i32 l = 0; l < 60; ++l)
        {
            while (longl.size() < static_cast<usize>(l + 1) * 12001)
            {
                longl += word();
                longl += ' ';
            }
            longl.resize(static_cast<usize>(l + 1) * 12001 - 1);
            longl += '\n';
        }
    }

    bench_device nd;
    nd.my_surface.w = 1600;
    nd.my_surface.h = 1000;
    context *c = create_context(&nd, nd.create_surface());
    if (scene && std::strcmp(scene, "a11y") == 0) set_accessibility(c, true);

    const font_handle fh = load_font(c, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
    if (fh != FONT_INVALID)
    {
        theme t = default_dark();
        t.font = fh;
        set_theme(c, t);
    }

    using clock_t = std::chrono::steady_clock;
    f64 total_ms = 0.0, worst_ms = 0.0;
    i32 last_draw_calls = 0, last_verts = 0;

    for (i32 f = 0; f < frames; ++f)
    {
        const f64 t = static_cast<f64>(f) / 60.0;
        const auto t0 = clock_t::now();
        if (f == 0 && budget >= 0) set_text_atlas_budget(c, budget);
        if (scene && std::strcmp(scene, "bigtext") == 0)
        {
            mouse_move(c, 800.0f, 500.0f);
            mouse_wheel(c, 0.0f, -400.0f); // scrolls to the end within the warm-up
        }
        begin_frame(c, t, 1.0 / 60.0, rect::make(0, 0, 1600, 1000));
        if (scene && std::strcmp(scene, "a11y") != 0)
        {
            ui u(c);
            const std::string_view sc(scene);
            const rect area = rect::make(100, 100, 1400, 800);
            if (sc == "wrap-type")
            {
                // typing at the end of the last paragraph: the model changes every frame
                doc.insert(doc.size() - 1, 1, (f % 6) ? 'x' : ' ');
                (void)u.text_area(area, doc, "bench_doc"_id, {.wrap = true});
            }
            else if (sc == "wrap-resize")
            {
                const f32 w = 900.0f + 500.0f * static_cast<f32>(f % 50) / 50.0f;
                (void)u.text_area(rect::make(100, 100, w, 800), doc, "bench_doc"_id,
                                  {.wrap = true});
            }
            else if (sc == "bigtext")
                (void)u.text_area(area, big, "bench_big"_id,
                                  {.wrap = false, .read_only = true, .line_numbers = true});
            else if (sc == "longlines")
                (void)u.text_area(area, longl, "bench_long"_id, {.wrap = false, .read_only = true});
            else if (sc == "vlist" || sc == "vlist-kept")
            {
                scroll_view sv(u, "bench_vlist"_id, area, scroll_options{});
                sv.virtual_list(
                    200000, [](i32 i) { return 18.0f + static_cast<f32>(i % 7) * 4.0f; },
                    [](ui &uu, i32 i, rect r)
                    {
                        char line[32];
                        std::snprintf(line, sizeof(line), "row %d", i);
                        uu.text(r, line, uu.th().text, ALIGN_LEFT);
                    },
                    {.heights_version = sc == "vlist-kept" ? 1u : 0u});
            }
        }
        else if (atlas)
        {
            // One of 20 sizes per frame, ~80 distinct glyphs each: an app that
            // zooms, or shows headings in many sizes over a long session.
            ui u(c);
            text_scope ts =
                u.text_style(24.0f + 6.0f * static_cast<f32>(f % 20), c->active_theme.font);
            column col(rect::make(0, 0, 1600, 1000), 0.0f);
            for (i32 r = 0; r < 4; ++r)
                u.text(col.next(u.line_height()),
                       "The quick brown fox jumps over the lazy dog 0123456789 "
                       "\xC3\x84\xC3\x96\xC3\x9C\xC3\xA4\xC3\xB6\xC3\xBC\xC3\x9F"
                       "\xC3\x87\xC4\x9E\xC4\xB0\xC5\x9E\xC3\xA7\xC4\x9F\xC4\xB1\xC5\x9F",
                       c->active_theme.text, ALIGN_LEFT);
        }
        else if (textarea)
        {
            // A read-only log that grows by one line a frame: the layout is
            // rebuilt when the text changes, only the visible rows are drawn.
            static std::string log;
            static i32 lines = 0;
            if (log.empty())
                for (; lines < 10000; ++lines)
                    log += "12:00:00.000 info  worker " + std::to_string(lines % 16) +
                           ": processed a batch of records\n";
            log += "12:00:01.000 info  tick " + std::to_string(lines++) + "\n";
            ui u(c);
            (void)u.text_area(rect::make(100, 100, 1400, 800), log, "bench_log"_id,
                              {.wrap = false, .read_only = true});
        }
        else
        {
            ui u(c);

            // 2,000 buttons (auto-fit grid at min 40px, 24px rows). Labels
            // on: the solid rect + text + ring per button is exactly the
            // solid<->text alternation the batching plan targets.
            {
                const rect grid_area = rect::make(0, 0, 1600, 500);
                grid_cursor grid = auto_fit_grid(grid_area, 2000, 40.0f, 24.0f);
                for (i32 i = 0; i < 2000; ++i)
                {
                    const rect cell = grid.next();
                    if (cell.w <= 0.0f) break;
                    char label[16];
                    std::snprintf(label, sizeof(label), "%d", i);
                    (void)u.button(cell, label, id_child("bench_btn"_id, static_cast<uiid>(i)));
                }
            }

            // 500 text rows
            {
                column col(rect::make(0, 510, 1600, 400), 0.0f);
                for (i32 i = 0; i < 500; ++i)
                {
                    char line[64];
                    std::snprintf(line, sizeof(line), "row %d: the quick brown fox", i);
                    u.text(col.next(16.0f), line, c->active_theme.text, ALIGN_LEFT);
                }
            }

            // 50 rounded panels
            {
                row r(rect::make(0, 920, 1600, 80), 0.0f);
                for (i32 i = 0; i < 50; ++i)
                {
                    const rect cell = r.next(31.0f);
                    u.card(cell, card_override{.radius = some(8.0f)});
                }
            }

            // a few interactive widgets so the per-frame state paths are hot
            (void)u.checkbox(rect::make(1200, 505, 120, 20), "b", g_check_value, "bench_cb"_id);
            (void)u.slider_float(rect::make(1330, 505, 200, 20), "s", g_slider_value, 0.0f, 1.0f,
                                 "bench_sld"_id);
            (void)u.text_field(rect::make(1200, 530, 330, 20), g_field_value, "bench_field"_id);
            if (scene) // --a11y: a large read-only text area joins the accessibility tree
                (void)u.text_area(rect::make(1200, 560, 380, 340), big, "bench_a11y"_id,
                                  {.wrap = false, .read_only = true});
        }
        end_frame(c);
        const auto t1 = clock_t::now();

        const f64 ms = std::chrono::duration<f64, std::milli>(t1 - t0).count();
        if (f >= frames / 4) // skip warm-up (glyph rasterization, first-touch)
        {
            total_ms += ms;
            if (ms > worst_ms) worst_ms = ms;
        }
        last_draw_calls = nd.draw_calls;
        last_verts = nd.vertices;
    }

    const f64 avg = total_ms / static_cast<f64>(frames - frames / 4);
    std::printf("frames=%d avg=%.3fms worst=%.3fms draw_calls=%d vertices=%d atlas_pages=%d\n",
                frames, avg, worst_ms, last_draw_calls, last_verts, text_atlas_pages(c));
    destroy_context(c);
    return 0;
}
