#include "test_util.h"
#include <type_traits>

namespace
{
// A complete semantic backend that intentionally has no CPU mesh or font atlas.
// This proves that core does not tessellate, clip, rasterize glyphs or allocate a
// fallback draw list before dispatching an operation to its renderer.
struct paint_probe
{
    enum operation
    {
        MESH,
        RECT,
        LINE,
        ROUNDED,
        ROUNDED_GRADIENT,
        IMAGE,
        FITTED,
        NINE,
        POLYGON,
        SECTOR,
        ARC,
        BLUR,
        TEXT,
        ELLIPSIS,
        WRAPPED,
        RING,
        STROKE,
        GRADIENT,
        COUNT
    };
    i32 calls[COUNT]{};
    i32 created = 0, destroyed = 0, begins = 0, ends = 0, flushes = 0;
    i32 releases = 0;
    rect last_rect{};
    color last_tint{};
    std::string_view last_text;
    bool disabled = false;
    f32 scale = 0;
    bool source_in_destructor = false;
};
struct semantic_painter : render_painter
{
    context &ctx;
    paint_probe &probe;
    semantic_painter(context &c, paint_probe &p) : ctx(c), probe(p) { ++probe.created; }
    ~semantic_painter() override
    {
        if (probe.source_in_destructor) CHECK(!font_bytes(&ctx, 0).empty());
        ++probe.destroyed;
    }
    void begin() override { ++probe.begins; }
    void end() override { ++probe.ends; }
    void invalidate() override {}
    void flush() override { ++probe.flushes; }
    bool prepare(const geometry_capacity &) override { return true; }
    draw_list *geometry() override { return nullptr; }
    bool vector_text() const override { return false; }
    void release_text() override {}
    f32 measure_text_width(std::string_view s) override { return static_cast<f32>(s.size()) * 8; }
    f32 glyph_advance(const paint_glyph_metric &) override { return 8; }
    void release_effects(blur_store *&p) override
    {
        CHECK(p == nullptr);
        ++probe.releases;
    }
    void mesh(const paint_mesh &p) override
    {
        ++probe.calls[paint_probe::MESH];
        CHECK(p.vertex_count == 3 && p.index_count == 3);
    }
    void rectangle(const paint_rect &p) override
    {
        ++probe.calls[paint_probe::RECT];
        probe.last_rect = p.area;
        probe.last_tint = p.tint;
        probe.disabled = ctx.disabled_depth > 0;
        probe.scale = ctx.render_scale;
    }
    void line(const paint_line &) override { ++probe.calls[paint_probe::LINE]; }
    void rounded_rectangle(const paint_rounded_rect &) override
    {
        ++probe.calls[paint_probe::ROUNDED];
    }
    void rounded_gradient(const paint_rounded_gradient &) override
    {
        ++probe.calls[paint_probe::ROUNDED_GRADIENT];
    }
    void image(const paint_image &) override { ++probe.calls[paint_probe::IMAGE]; }
    rect fitted_image(const paint_fitted_image &p) override
    {
        ++probe.calls[paint_probe::FITTED];
        return p.area;
    }
    void nine_slice(const paint_image &) override { ++probe.calls[paint_probe::NINE]; }
    void polygon(const paint_polygon &) override { ++probe.calls[paint_probe::POLYGON]; }
    void sector(const paint_sector &) override { ++probe.calls[paint_probe::SECTOR]; }
    void arc(const paint_arc &) override { ++probe.calls[paint_probe::ARC]; }
    void blur(const paint_blur &) override { ++probe.calls[paint_probe::BLUR]; }
    void text(const paint_text &p) override
    {
        ++probe.calls[paint_probe::TEXT];
        probe.last_text = p.text;
    }
    void text_ellipsis(const paint_text &) override { ++probe.calls[paint_probe::ELLIPSIS]; }
    void text_wrapped(const paint_text &) override { ++probe.calls[paint_probe::WRAPPED]; }
    void ring(const paint_ring &) override { ++probe.calls[paint_probe::RING]; }
    void stroke(const paint_line &) override { ++probe.calls[paint_probe::STROKE]; }
    void gradient(const paint_gradient &) override { ++probe.calls[paint_probe::GRADIENT]; }
};
struct semantic_device : null_device
{
    paint_probe probe;
    render_painter *create_painter(context &c) override { return new semantic_painter(c, probe); }
};
static_assert(std::is_abstract_v<render_device>);
static_assert(std::is_abstract_v<render_painter>);
static_assert(!std::is_abstract_v<semantic_painter>);
} // namespace

PUI_TEST(renderer_receives_all_operations_before_geometry_preparation)
{
    semantic_device device;
    context *c = create_context(&device);
    ui u(c);
    begin_frame(c, 1, 1.0 / 60, {0, 0, 320, 240});
    CHECK(c->dl == nullptr);
    CHECK(u.prepare_geometry({4096, 8192}));
    const rect r{8, 12, 80, 24};
    const color tint{20, 40, 60, 80};
    const vertex verts[3]{};
    const i32 indices[3]{0, 1, 2};
    const vec2 points[3]{{1, 2}, {3, 4}, {5, 6}};
    const skin_image skin{};
    u.draw_triangles(nullptr, verts, 3, indices, 3);
    u.draw_rect(r, tint);
    u.draw_line(1, 2, 3, 4, tint, 2);
    u.draw_rounded_rect(r, tint, 5);
    u.draw_rounded_gradient({r, 5, tint, tint, tint, tint});
    u.draw_image(skin, r);
    const rect fitted = u.image(r, {});
    CHECK(fitted.x == r.x && fitted.w == r.w);
    u.draw_nine_slice(skin, r);
    u.draw_polygon(points, tint);
    u.draw_sector({20, 30}, 2, 10, 0, PI, tint);
    u.draw_arc({20, 30}, 10, 2, 0, PI, tint);
    u.blur(r, 6);
    u.text(r, "renderer-owned text", tint, ALIGN_LEFT);
    u.text_ellipsis(r, "renderer-owned truncation", tint);
    u.text_wrapped(r, "renderer-owned wrapping", tint);
    for (i32 i = 0; i <= paint_probe::WRAPPED; ++i) CHECK(device.probe.calls[i] == 1);
    CHECK(device.probe.last_text == "renderer-owned text");
    CHECK(c->ts == nullptr); // no hidden glyph preparation in core
    CHECK(device.draw_calls == 0 && device.vertices == 0);
    CHECK(violation_count(c) == 0);
    end_frame(c);
    CHECK(device.probe.begins == 1 && device.probe.ends == 1);
    destroy_context(c);
    CHECK(device.probe.created == 1 && device.probe.destroyed == 1);
}

PUI_TEST(renderer_receives_logical_state_without_mesh_fallback)
{
    semantic_device device;
    context *c = create_context(&device);
    ui u(c);
    begin_frame(c, 1, 1.0 / 60, {0, 0, 320, 240});
    const rect r{1000, 2000, 80, 24}; // even culling is a renderer decision
    const color tint{20, 40, 60, 80};
    {
        auto disabled = u.disabled();
        u.draw_rect(r, tint);
        CHECK(device.probe.disabled);
    }
    CHECK(device.probe.last_rect.x == r.x && device.probe.last_rect.y == r.y);
    CHECK(device.probe.last_tint.a == 80); // alpha has not been baked into vertices
    CHECK(device.probe.scale == c->render_scale);
    CHECK(violation_count(c) == 0);
    end_frame(c);
    destroy_context(c);
}

PUI_TEST(widget_geometry_is_submitted_semantically)
{
    semantic_device device;
    context *c = create_context(&device);
    ui u(c);
    begin_frame(c, 1, 1.0 / 60, {0, 0, 640, 480});
    u.button({10, 10, 160, 30}, "Button", "semantic-button"_id);
    bool checked = true;
    u.checkbox({10, 50, 160, 30}, "Check", checked, "semantic-check"_id);
    color picked{20, 40, 60, 255};
    comp::color_picker(u, {200, 10, 260, 300}, picked, {.id = "semantic-picker"_id});
    CHECK(device.probe.calls[paint_probe::RING] > 0);
    CHECK(device.probe.calls[paint_probe::STROKE] == 2);
    CHECK(device.probe.calls[paint_probe::GRADIENT] > 0);
    CHECK(device.probe.calls[paint_probe::MESH] == 0);
    CHECK(c->dl == nullptr);
    CHECK(violation_count(c) == 0);
    end_frame(c);
    destroy_context(c);
}

PUI_TEST(renderer_painter_lifetime_is_per_context_and_device_binding)
{
    semantic_device first, second;
    context *a = create_context(&first);
    context *b = create_context(&first);
    CHECK(a->painter != b->painter);
    CHECK(first.probe.created == 2);
    set_device(a, &second, nullptr);
    CHECK(first.probe.destroyed == 1 && second.probe.created == 1);
    destroy_context(a);
    destroy_context(b);
    CHECK(first.probe.destroyed == 2 && second.probe.destroyed == 1);
}

PUI_TEST(renderer_can_borrow_font_sources_and_choose_measurement)
{
    semantic_device device;
    context *c = create_context(&device);
    CHECK(font_bytes(nullptr, 0).empty());
    CHECK(font_bytes(c, FONT_INVALID).empty());
    const auto font = load_font(c, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
    CHECK(font != FONT_INVALID);
    const auto original = font_bytes(c, font);
    CHECK(original.size() > 1024);
    for (i32 i = 0; i < 8; ++i)
        CHECK(load_font(c, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans-Bold.ttf") != FONT_INVALID);
    const auto retained = font_bytes(c, font);
    CHECK(retained.data() == original.data() && retained.size() == original.size());
    CHECK(font_bytes(c, 1000).empty());
    ui u(c);
    CHECK(u.text_width("abc") == 24); // selected renderer metrics
    CHECK(text_atlas_pages(c) == 0);  // measurement did not select the mesh atlas
    device.probe.source_in_destructor = true;
    destroy_context(c);
}

PUI_TEST(renderer_rebinding_releases_old_texture_resources)
{
    struct resource_device : null_device
    {
        i32 released = 0;
        void destroy_texture(texture_handle) override { ++released; }
    } first, second;
    context *c = create_context(&first);
    const auto font = load_font(c, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
    CHECK(font != FONT_INVALID);
    theme t = c->active_theme;
    t.font = font;
    set_theme(c, t);
    ui u(c);
    const f32 width = u.text_width("Retained font");
    CHECK(text_atlas_pages(c) == 1 && first.textures == 1);
    set_device(c, &second, nullptr);
    CHECK(first.released == 1 && text_atlas_pages(c) == 0);
    CHECK(u.text_width("Retained font") == width);
    CHECK(text_atlas_pages(c) == 1 && second.textures == 1);
    destroy_context(c);
    CHECK(first.released == 1 && second.released == 1);
}
