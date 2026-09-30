// renderer — implementing the backend contract: a CPU framebuffer device.
//
// Shows: `render_device` / `render_surface` (the only two interfaces a backend
// must implement), the `vertex` format, texture upload/destroy, scissor clipping,
// triangle rasterization, and a PPM dump you can open in any image viewer.
//
//   pui_ex_renderer     (no SDL; writes pui_renderer_example.ppm)
//
// See docs/porting_a_backend.md for the full contract.
#include <pufferui/pufferui.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace pui;

namespace
{
struct cpu_texture
{
    i32 w = 0, h = 0;
    std::vector<u8> px; // RGBA8
};

struct cpu_surface : render_surface
{
    cpu_texture *target = nullptr;
    i32 w = 0, h = 0;
    texture_handle default_target() override { return target; }
    texture_handle scene_target() override { return target; }
    void output_size(i32 &ow, i32 &oh) override
    {
        ow = w;
        oh = h;
    }
};

inline u8 to_u8(f32 v)
{
    const f32 x = v * 255.0f;
    return static_cast<u8>(x < 0.0f ? 0.0f : x > 255.0f ? 255.0f : x);
}

struct cpu_device : render_device
{
    cpu_surface surf;
    cpu_texture *fb = nullptr;
    std::vector<cpu_texture *> textures;
    rect clip{};
    bool clip_on = false;
    i32 draw_calls = 0;

    ~cpu_device() override
    {
        for (cpu_texture *t : textures) delete t;
        delete fb;
    }

    backend_caps caps() const override
    {
        return backend_caps::SCISSOR | backend_caps::STREAMING_TEXTURES;
    }

    render_surface *create_surface(void *) override
    {
        fb = new cpu_texture();
        fb->w = surf.w;
        fb->h = surf.h;
        fb->px.assign(static_cast<usize>(fb->w) * static_cast<usize>(fb->h) * 4, 0);
        surf.target = fb;
        return &surf;
    }

    texture_handle create_texture(i32 w, i32 h, const u8 *rgba) override
    {
        cpu_texture *t = new cpu_texture();
        t->w = w;
        t->h = h;
        t->px.assign(static_cast<usize>(w) * static_cast<usize>(h) * 4, 255);
        if (rgba) std::memcpy(t->px.data(), rgba, t->px.size());
        textures.push_back(t);
        return t;
    }

    void update_texture(texture_handle tex, i32 x, i32 y, i32 w, i32 h, const u8 *rgba) override
    {
        cpu_texture *t = static_cast<cpu_texture *>(tex);
        if (!t || !rgba) return;
        for (i32 row = 0; row < h; ++row)
        {
            u8 *dst = t->px.data() + (static_cast<usize>(y + row) * t->w + x) * 4;
            std::memcpy(dst, rgba + static_cast<usize>(row) * w * 4, static_cast<usize>(w) * 4);
        }
    }

    void destroy_texture(texture_handle tex) override
    {
        cpu_texture *t = static_cast<cpu_texture *>(tex);
        for (usize i = 0; i < textures.size(); ++i)
            if (textures[i] == t)
            {
                textures.erase(textures.begin() + static_cast<std::ptrdiff_t>(i));
                break;
            }
        delete t;
    }

    void set_clip(const rect *r) override
    {
        clip_on = (r != nullptr);
        if (r) clip = *r;
    }

    void clear(color c) override
    {
        for (i32 y = 0; y < fb->h; ++y)
            for (i32 x = 0; x < fb->w; ++x)
            {
                u8 *p = fb->px.data() + (static_cast<usize>(y) * fb->w + x) * 4;
                p[0] = c.r;
                p[1] = c.g;
                p[2] = c.b;
                p[3] = c.a;
            }
    }

    void blend(i32 x, i32 y, f32 r, f32 g, f32 b, f32 a)
    {
        if (a <= 0.0f) return;
        if (x < 0 || y < 0 || x >= fb->w || y >= fb->h) return;
        if (clip_on && (x < clip.x || y < clip.y || x >= clip.right() || y >= clip.bottom()))
            return;
        u8 *p = fb->px.data() + (static_cast<usize>(y) * fb->w + x) * 4;
        const f32 ia = 1.0f - a;
        p[0] = to_u8(r * a + (p[0] / 255.0f) * ia);
        p[1] = to_u8(g * a + (p[1] / 255.0f) * ia);
        p[2] = to_u8(b * a + (p[2] / 255.0f) * ia);
        p[3] = 255;
    }

    void triangle(const vertex &v0, const vertex &v1, const vertex &v2, const cpu_texture *tex)
    {
        const f32 area = (v1.x - v0.x) * (v2.y - v0.y) - (v1.y - v0.y) * (v2.x - v0.x);
        if (std::fabs(area) < 0.0001f) return;

        const f32 minx = max2(0.0f, std::floor(min2(min2(v0.x, v1.x), v2.x)));
        const f32 maxx = min2(static_cast<f32>(fb->w - 1), std::ceil(max2(max2(v0.x, v1.x), v2.x)));
        const f32 miny = max2(0.0f, std::floor(min2(min2(v0.y, v1.y), v2.y)));
        const f32 maxy = min2(static_cast<f32>(fb->h - 1), std::ceil(max2(max2(v0.y, v1.y), v2.y)));

        for (i32 y = static_cast<i32>(miny); y <= static_cast<i32>(maxy); ++y)
            for (i32 x = static_cast<i32>(minx); x <= static_cast<i32>(maxx); ++x)
            {
                const f32 px = static_cast<f32>(x) + 0.5f;
                const f32 py = static_cast<f32>(y) + 0.5f;
                const f32 w0 = ((v1.x - px) * (v2.y - py) - (v1.y - py) * (v2.x - px)) / area;
                const f32 w1 = ((v2.x - px) * (v0.y - py) - (v2.y - py) * (v0.x - px)) / area;
                const f32 w2 = 1.0f - w0 - w1;
                if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;

                f32 r = (w0 * v0.c.r + w1 * v1.c.r + w2 * v2.c.r) / 255.0f;
                f32 g = (w0 * v0.c.g + w1 * v1.c.g + w2 * v2.c.g) / 255.0f;
                f32 b = (w0 * v0.c.b + w1 * v1.c.b + w2 * v2.c.b) / 255.0f;
                f32 a = (w0 * v0.c.a + w1 * v1.c.a + w2 * v2.c.a) / 255.0f;

                if (tex)
                {
                    const f32 u = w0 * v0.u + w1 * v1.u + w2 * v2.u;
                    const f32 v = w0 * v0.v + w1 * v1.v + w2 * v2.v;
                    const i32 tx = static_cast<i32>(u * static_cast<f32>(tex->w));
                    const i32 ty = static_cast<i32>(v * static_cast<f32>(tex->h));
                    if (tx < 0 || ty < 0 || tx >= tex->w || ty >= tex->h) continue;
                    const u8 *tp = tex->px.data() + (static_cast<usize>(ty) * tex->w + tx) * 4;
                    r *= tp[0] / 255.0f;
                    g *= tp[1] / 255.0f;
                    b *= tp[2] / 255.0f;
                    a *= tp[3] / 255.0f;
                }
                blend(x, y, r, g, b, a);
            }
    }

    void draw(texture_handle tex, const vertex *vertices, i32 vertex_count, const i32 *indices,
              i32 index_count) override
    {
        ++draw_calls;
        const cpu_texture *t = static_cast<const cpu_texture *>(tex);
        for (i32 i = 0; i + 2 < index_count; i += 3)
            triangle(vertices[indices[i]], vertices[indices[i + 1]], vertices[indices[i + 2]], t);
    }

    void set_cursor(cursor) override {}
    void set_vsync(bool) override {}
};

void write_ppm(const char *path, const cpu_texture &t)
{
    std::FILE *f = std::fopen(path, "wb");
    if (!f)
    {
        std::printf("FAIL renderer: cannot open %s\n", path);
        return;
    }
    std::fprintf(f, "P6\n%d %d\n255\n", t.w, t.h);
    for (i32 i = 0; i < t.w * t.h; ++i)
        std::fwrite(t.px.data() + static_cast<usize>(i) * 4, 1, 3, f);
    std::fclose(f);
}
} // namespace

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    cpu_device dev;
    dev.surf.w = 480;
    dev.surf.h = 320;
    render_surface *surf = dev.create_surface(nullptr);
    context *c = create_context(&dev, surf);

    // No RENDER_TARGETS: blur degrades to a flat tint (see docs/porting_a_backend.md).
    font_handle font = load_font(c, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
    theme t = default_dark();
    t.font = font;
    set_theme(c, t);

    window *w = add_window(c, nullptr, surf, rect::make(0.0f, 0.0f, 480.0f, 320.0f));
    focus_window(c, *w);

    for (i32 frame = 0; frame < 2; ++frame)
    {
        begin_frame(c, static_cast<f64>(frame) * (1.0 / 60.0), 1.0 / 60.0,
                    rect::make(0.0f, 0.0f, 480.0f, 320.0f));
        {
            ui u(c);
            const theme &th = u.th();
            u.draw_rect(rect::make(0.0f, 0.0f, 480.0f, 320.0f), th.bg);
            u.card(rect::make(20.0f, 20.0f, 440.0f, 280.0f));
            u.text(rect::make(40.0f, 40.0f, 400.0f, 26.0f), "A CPU render_device -> PPM", th.text,
                   ALIGN_LEFT);
            u.text(rect::make(40.0f, 70.0f, 400.0f, 20.0f),
                   "Nothing here came from SDL: just vertices, indices and textures.", th.text_dim,
                   ALIGN_LEFT);
            (void)u.button(rect::make(40.0f, 110.0f, 160.0f, 32.0f), "A button", "cpu_btn"_id);
            f32 v = 0.6f;
            (void)u.slider_float(rect::make(40.0f, 160.0f, 260.0f, 28.0f), "value", v, 0.0f, 1.0f,
                                 "cpu_slider"_id, "%.2f");
            u.draw_arc(vec2{380.0f, 200.0f}, 50.0f, 14.0f, 0.4f, 5.0f, th.accent);
        }
        end_frame(c);
    }

    write_ppm("pui_renderer_example.ppm", *dev.fb);
    std::printf("wrote pui_renderer_example.ppm (%d x %d) via a custom render_device\n", dev.fb->w,
                dev.fb->h);
    std::printf("draw calls: %d   textures alive: %d\n", dev.draw_calls,
                static_cast<i32>(dev.textures.size()));

    const bool ok = dev.draw_calls > 0;
    destroy_context(c);
    std::printf(ok ? "ok   renderer\n" : "FAIL renderer: no draw calls\n");
    return ok ? 0 : 1;
}
