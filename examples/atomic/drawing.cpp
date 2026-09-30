// drawing — raw primitives, custom geometry and images.
//
// Shows: `draw_rect`, `draw_rounded_rect`, `draw_line`, `draw_polygon`,
// `draw_sector`, `draw_arc`, `draw_triangles` (custom vertex geometry),
// `create_texture` + `draw_image` / `draw_nine_slice`.
//
//   pui_ex_drawing
#include "../example_common.h"

#include <vector>

static texture_handle g_checker = nullptr;
static skin_image g_patch;
static bool g_ready = false;

static void build_images(example_app &app)
{
    std::vector<u8> px(static_cast<usize>(64) * 64 * 4);
    for (i32 y = 0; y < 64; ++y)
        for (i32 x = 0; x < 64; ++x)
        {
            const bool on = (((x / 8) + (y / 8)) & 1) != 0;
            u8 *p = px.data() + (static_cast<usize>(y) * 64 + x) * 4;
            p[0] = on ? 200 : 60;
            p[1] = on ? 170 : 70;
            p[2] = on ? 90 : 120;
            p[3] = 255;
        }
    g_checker = app.device->create_texture(64, 64, px.data());

    // A 9-slice atlas with distinct border pixels (corners/edges vs center).
    std::vector<u8> at(static_cast<usize>(32) * 32 * 4);
    for (i32 y = 0; y < 32; ++y)
        for (i32 x = 0; x < 32; ++x)
        {
            const bool edge = x < 6 || y < 6 || x >= 26 || y >= 26;
            u8 *p = at.data() + (static_cast<usize>(y) * 32 + x) * 4;
            p[0] = edge ? 60 : 90;
            p[1] = edge ? 90 : 130;
            p[2] = edge ? 160 : 200;
            p[3] = 255;
        }
    texture_handle atlas = app.device->create_texture(32, 32, at.data());
    g_patch = make_skin_image(atlas, 32, 32, rect::make(0, 0, 32, 32), 6, 6, 6, 6);
}

static void drawing_frame(ui &u, example_app &app)
{
    const theme &th = u.th();
    example_page page =
        example_begin_page(u, app, "Drawing", "primitives, custom geometry and images");
    if (!g_ready)
    {
        build_images(app);
        g_ready = true;
    }
    column col(page.content, example_ui::SECTION_GAP);

    {
        const rect body = example_section(
            u, col.next(206.0f), "Primitives",
            "draw_rect / draw_rounded_rect / draw_line / draw_polygon / draw_sector / draw_arc",
            app.font_bold);
        column c(body, 10.0f);

        {
            row r(c.next(36.0f), 8.0f);
            u.draw_rect(r.next(90.0f), th.widget_bg);
            u.draw_rounded_rect(r.next(90.0f), th.accent, 10.0f);
            const rect cross = r.next(90.0f);
            u.draw_line(cross.x, cross.center_y(), cross.right(), cross.center_y(), th.text_dim,
                        2.0f);
            u.draw_line(cross.center_x(), cross.y, cross.center_x(), cross.bottom(), th.text_dim,
                        2.0f);
            u.text(r.remaining(), "rounded rects are antialiased", th.text_dim, ALIGN_LEFT);
        }

        {
            row r(c.next(92.0f), 10.0f);

            const rect tri = r.next(92.0f);
            const vec2 pts[3] = {{tri.center_x(), tri.y + 6.0f},
                                 {tri.x + 8.0f, tri.bottom() - 8.0f},
                                 {tri.right() - 8.0f, tri.bottom() - 8.0f}};
            u.draw_polygon(std::span<const vec2>(pts, 3), th.accent);

            const rect pie = r.next(92.0f);
            const vec2 pc{pie.center_x(), pie.center_y()};
            const f32 pr = min2(pie.w, pie.h) * 0.42f;
            u.draw_sector(pc, 0.0f, pr, 0.0f, 2.0f, th.accent);
            u.draw_sector(pc, 0.0f, pr, 2.0f, 4.0f, th.widget_hover);
            u.draw_sector(pc, 0.0f, pr, 4.0f, 6.2832f, th.widget_bg);

            const rect ring = r.next(92.0f);
            const vec2 rc{ring.center_x(), ring.center_y()};
            u.draw_arc(rc, 28.0f, 9.0f, 0.4f, 4.2f, th.accent);
            u.draw_arc(rc, 28.0f, 9.0f, 4.4f, 5.9f, th.text_dim);
        }
    }

    {
        const rect body = example_section(
            u, col.next(186.0f), "Custom geometry and images",
            "draw_triangles; create_texture + draw_image / draw_nine_slice", app.font_bold);
        column c(body, 10.0f);

        {
            const rect box = c.next(36.0f);
            const vertex v[4] = {{box.x, box.y, 0.0f, 0.0f, th.accent},
                                 {box.right(), box.y, 0.0f, 0.0f, th.widget_hover},
                                 {box.right(), box.bottom(), 0.0f, 0.0f, th.panel_bg},
                                 {box.x, box.bottom(), 0.0f, 0.0f, th.text_dim}};
            const i32 idx[6] = {0, 1, 2, 0, 2, 3};
            u.draw_triangles(nullptr, v, 4, idx, 6);
        }

        {
            row r(c.next(72.0f), 10.0f);
            const skin_image full{g_checker,
                                  rect::make(0.0f, 0.0f, 1.0f, 1.0f),
                                  64.0f,
                                  64.0f,
                                  0.0f,
                                  0.0f,
                                  0.0f,
                                  0.0f,
                                  SKIN_CENTER_STRETCH};
            u.draw_image(full, r.next(120.0f));
            u.draw_nine_slice(g_patch, r.next(150.0f));
            u.text(r.remaining(), "draw_image stretches; draw_nine_slice keeps corners crisp",
                   th.text_dim, ALIGN_LEFT);
        }
    }
}

int main(int argc, char **argv)
{
    return example_run("drawing", 720, 510, argc, argv, drawing_frame);
}
