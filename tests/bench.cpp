// pui_bench — the frame-cost benchmark (r88). Headless (null_device): it
// measures the CORE's per-frame work (layout, interact, draw-list recording
// and flushing), not the renderer. The scene is the audit's benchmark shape:
// 2,000 buttons, 500 text rows, 50 rounded panels. Numbers land in
// docs/perf.md; relative deltas across revisions are what matter.
//
//   pui_bench [--frames N]     (default 300)
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
    for (i32 i = 1; i < argc; ++i)
        if (std::strncmp(argv[i], "--frames", 8) == 0 && i + 1 < argc)
            frames = std::atoi(argv[i + 1]);

    bench_device nd;
    nd.my_surface.w = 1600;
    nd.my_surface.h = 1000;
    context *c = create_context(&nd, nd.create_surface());

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
        begin_frame(c, t, 1.0 / 60.0, rect::make(0, 0, 1600, 1000));
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
    std::printf("frames=%d avg=%.3fms worst=%.3fms draw_calls=%d vertices=%d\n", frames, avg,
                worst_ms, last_draw_calls, last_verts);
    destroy_context(c);
    return 0;
}
