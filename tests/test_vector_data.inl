// Independent private vector resource/import regressions; existing tests stay fixed.
#include "test_util.h"
#include <cstdlib>
#include <new>
#include <filesystem>
#include <fstream>
#if defined(_WIN32)
#include <malloc.h>
#endif
namespace
{
thread_local bool vector_count_allocations = false;
thread_local usize vector_allocation_count = 0;
thread_local i64 vector_fail_allocation_after = -1;
bool vector_verbose = false;
} // namespace
static void vector_track_allocation(std::size_t size)
{
    // MSVC Debug allocates 16-byte iterator proxies in noexcept empty-container
    // constructors. Inject faults into resource payloads; counting below still
    // observes all allocations, including these Debug-only proxies.
    const bool payload = size >= 64;
    if (payload && vector_fail_allocation_after == 0)
    {
        if (vector_verbose)
        {
            std::fprintf(stderr, "injecting allocation failure, bytes=%zu\n", size);
            std::fflush(stderr);
        }
        throw std::bad_alloc{};
    }
    if (payload && vector_fail_allocation_after > 0) --vector_fail_allocation_after;
    if (vector_count_allocations) ++vector_allocation_count;
}
void *operator new(std::size_t size)
{
    vector_track_allocation(size);
    if (void *p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc{};
}
void operator delete(void *p) noexcept
{
    std::free(p);
}
void operator delete(void *p, std::size_t) noexcept
{
    std::free(p);
}
void *operator new[](std::size_t size)
{
    return ::operator new(size);
}
void operator delete[](void *p) noexcept
{
    ::operator delete(p);
}
void operator delete[](void *p, std::size_t) noexcept
{
    ::operator delete(p);
}
void *operator new(std::size_t size, std::align_val_t alignment)
{
    vector_track_allocation(size);
    const auto align = static_cast<std::size_t>(alignment);
#if defined(_WIN32)
    if (void *p = _aligned_malloc(size ? size : 1, align)) return p;
#else
    if (size <= SIZE_MAX - align)
        if (void *p = std::aligned_alloc(align, ((size ? size : 1) + align - 1) / align * align))
            return p;
#endif
    throw std::bad_alloc{};
}
void operator delete(void *p, std::align_val_t) noexcept
{
#if defined(_WIN32)
    _aligned_free(p);
#else
    std::free(p);
#endif
}
void *operator new[](std::size_t size, std::align_val_t a)
{
    return ::operator new(size, a);
}
void operator delete[](void *p, std::align_val_t a) noexcept
{
    ::operator delete(p, a);
}
void operator delete(void *p, std::size_t, std::align_val_t a) noexcept
{
    ::operator delete(p, a);
}
void operator delete[](void *p, std::size_t, std::align_val_t a) noexcept
{
    ::operator delete(p, a);
}
void *operator new(std::size_t size, const std::nothrow_t &) noexcept
{
    try
    {
        return ::operator new(size);
    }
    catch (...)
    {
        return nullptr;
    }
}
void *operator new[](std::size_t size, const std::nothrow_t &n) noexcept
{
    return ::operator new(size, n);
}
void *operator new(std::size_t size, std::align_val_t a, const std::nothrow_t &) noexcept
{
    try
    {
        return ::operator new(size, a);
    }
    catch (...)
    {
        return nullptr;
    }
}
void *operator new[](std::size_t size, std::align_val_t a, const std::nothrow_t &n) noexcept
{
    return ::operator new(size, a, n);
}
void operator delete(void *p, const std::nothrow_t &) noexcept
{
    ::operator delete(p);
}
void operator delete[](void *p, const std::nothrow_t &) noexcept
{
    ::operator delete(p);
}
void operator delete(void *p, std::align_val_t a, const std::nothrow_t &) noexcept
{
    ::operator delete(p, a);
}
void operator delete[](void *p, std::align_val_t a, const std::nothrow_t &) noexcept
{
    ::operator delete(p, a);
}

PUI_TEST(vector_allocation_counter_observes_scalar_array_aligned_and_nothrow_forms)
{
    const usize before = vector_allocation_count;
    vector_count_allocations = true;
    void *a = ::operator new(17), *b = ::operator new[](17);
    void *c = ::operator new(17, std::align_val_t{64}),
         *d = ::operator new[](17, std::align_val_t{64});
    void *e = ::operator new(17, std::nothrow), *f = ::operator new[](17, std::nothrow);
    void *g = ::operator new(17, std::align_val_t{64}, std::nothrow),
         *h = ::operator new[](17, std::align_val_t{64}, std::nothrow);
    vector_count_allocations = false;
    CHECK(vector_allocation_count - before == 8);
    CHECK(reinterpret_cast<std::uintptr_t>(c) % 64 == 0 &&
          reinterpret_cast<std::uintptr_t>(d) % 64 == 0);
    ::operator delete(a);
    ::operator delete[](b);
    ::operator delete(c, std::align_val_t{64});
    ::operator delete[](d, std::align_val_t{64});
    ::operator delete(e, std::nothrow);
    ::operator delete[](f, std::nothrow);
    ::operator delete(g, std::align_val_t{64}, std::nothrow);
    ::operator delete[](h, std::align_val_t{64}, std::nothrow);
}

PUI_TEST(vector_svg_keeps_quadratic_cubic_arc_and_smooth_control_geometry)
{
    using namespace vector_detail;
    vector_error error;
    std::vector<path_command> commands;
    CHECK(svg_path("M1 2c3 4 5 6 7 8s9 10 11 12q2 3 4 5t6 7z", commands, 100, error));
    CHECK(commands.size() == 6);
    if (commands.size() == 6)
    {
        CHECK(commands[1].verb == path_verb::CUBIC && commands[1].p1.x == 4 &&
              commands[1].p3.y == 10);
        CHECK(commands[2].p1.x == 10 && commands[2].p1.y == 12 && commands[2].p3.x == 19);
        CHECK(commands[4].verb == path_verb::QUADRATIC && commands[4].p1.x == 25 &&
              commands[4].p2.y == 34);
    }
    commands.clear();
    error = {};
    CHECK(svg_path("M0 0 A5 5 0 0 1 10 0", commands, 100, error));
    CHECK(commands.size() >= 3 && commands.size() <= 9 && commands.back().verb == path_verb::CUBIC);
    CHECK(commands.back().p3.x == 10 && commands.back().p3.y == 0);
    commands.clear();
    error = {};
    CHECK(svg_path("M0 0A0 5 0 1 0 10 20", commands, 100, error));
    CHECK(commands.size() == 2 && commands.back().verb == path_verb::LINE);
    commands.clear();
    error = {};
    CHECK(svg_path("M0 0A5 5 0 1 0 0 0", commands, 100, error));
    CHECK(commands.size() == 1);
    commands.clear();
    error = {};
    CHECK(svg_path("M0 0 A1e-300 1e-300 0 0 1 1 1", commands, 100, error));
    CHECK(error.code == vector_error_code::NONE && commands.back().p3.x == 1 &&
          commands.back().p3.y == 1);
}

PUI_TEST(vector_svg_affine_order_and_center_rotation_match_column_vector_contract)
{
    using namespace vector_detail;
    vector_error error;
    affine2 m;
    CHECK(svg_transform("translate(10 20) scale(2 3)", m, error));
    CHECK(m.apply({4, 5}).x == 18 && m.apply({4, 5}).y == 35);
    CHECK(svg_transform("rotate(90 10 20)", m, error));
    const vec2 p = m.apply({11, 20});
    CHECK(std::fabs(p.x - 10) < .00001f && std::fabs(p.y - 21) < .00001f);
    affine2 inv;
    CHECK(inverse(m, inv));
    const vec2 original = inv.apply(p);
    CHECK(std::fabs(original.x - 11) < .00001f && std::fabs(original.y - 20) < .00001f);
    CHECK(!inverse({0, 0, 0, 1, 0, 0}, inv));
}

PUI_TEST(vector_svg_group_opacity_is_isolated_and_inherited_paints_are_copied)
{
    const auto source =
        R"(<svg viewBox="0 0 100 100"><g opacity=".5" fill="red"><rect width="80" height="80"/><rect x="20" y="20" width="80" height="80" fill="blue"/></g></svg>)";
    auto r = load_svg(source);
    CHECK(r);
    if (!r) return;
    const auto &doc = r.image->source;
    CHECK(doc.shapes.size() == 2 && doc.operations.size() == 4);
    if (doc.shapes.size() == 2 && doc.operations.size() == 4)
    {
        CHECK(doc.operations[0].kind == vector_detail::operation_kind::BEGIN_GROUP &&
              doc.operations[0].opacity == .5f);
        CHECK(doc.operations[3].kind == vector_detail::operation_kind::END_GROUP);
        CHECK(doc.shapes[0].fill_opacity == 1 && doc.shapes[1].fill_opacity == 1);
        CHECK(doc.shapes[0].fill.tint.r == 255 && doc.shapes[0].fill.tint.b == 0);
        CHECK(doc.shapes[1].fill.tint.r == 0 && doc.shapes[1].fill.tint.b == 255);
    }
    destroy_vector(r.image);
}

PUI_TEST(vector_svg_gradients_clips_and_visibility_preserve_semantics)
{
    auto r = load_svg(
        R"svg(<svg viewBox="0 0 100 100"><defs><linearGradient id="base"><stop offset=".8" stop-color="red"/><stop offset=".2" stop-color="currentColor" stop-opacity=".25"/></linearGradient><linearGradient id="g" href="#base" x2="50%" spreadMethod="repeat"/><clipPath id="c" clipPathUnits="objectBoundingBox"><path d="M0 0H1V1H0Z" clip-rule="evenodd"/></clipPath></defs><g visibility="hidden"><path d="M0 0H10V10Z"/><path visibility="visible" d="M10 20H50V80H10Z" clip-path="url(#c)" fill="url(#g)"/></g></svg>)svg");
    CHECK(r);
    if (!r) return;
    const auto &doc = r.image->source;
    CHECK(doc.shapes.size() == 1 && doc.clips.size() == 1);
    if (doc.shapes.size() == 1 && doc.clips.size() == 1)
    {
        const auto &p = doc.shapes[0].fill;
        CHECK(p.kind == vector_paint_kind::LINEAR && p.object_bounds && p.end.x == .5f);
        CHECK(p.spread == vector_spread::REPEAT && p.stops.size() == 2);
        if (p.stops.size() == 2)
        {
            CHECK(p.stops[0].offset == .8f && p.stops[1].offset == .8f);
            CHECK(p.stops[1].current_color && p.stops[1].opacity == .25f);
        }
        CHECK(doc.clips[0].shapes.size() == 1);
        if (doc.clips[0].shapes.size() == 1)
        {
            const auto &s = doc.clips[0].shapes[0];
            CHECK(s.rule == path_fill_rule::EVENODD);
            CHECK(s.transform.a == 40 && s.transform.d == 60 && s.transform.e == 10 &&
                  s.transform.f == 20);
        }
        CHECK(doc.operations.front().clips.size() == 1);
    }
    destroy_vector(r.image);
}

PUI_TEST(vector_resources_retain_immutable_data_after_application_release)
{
    auto r = load_svg("<svg><path d='M0 0L10 10'/></svg>");
    CHECK(r);
    if (!r) return;
    vector_handle retained = r.image;
    const u64 identity = retained->identity;
    CHECK(vector_detail::retain(retained));
    destroy_vector(r.image);
    CHECK(r.image == nullptr);
    CHECK(retained->identity == identity && retained->source.shapes[0].commands[1].p1.x == 10);
    vector_detail::release(retained);
}

namespace
{
bool vector_outline_contains(std::span<const path_command> commands, vec2 point)
{
    using namespace renderer_detail::vector_geometry;
    std::vector<contour> paths;
    vector_error error;
    CHECK(flatten(commands, .0005, paths, error));
    i32 winding = 0;
    for (const auto &path : paths)
    {
        for (usize i = 0; i < path.points.size(); ++i)
        {
            const vec2 a = path.points[i], b = path.points[(i + 1) % path.points.size()];
            const f64 side = (static_cast<f64>(b.x) - a.x) * (point.y - a.y) -
                             (static_cast<f64>(point.x) - a.x) * (b.y - a.y);
            if (a.y <= point.y && b.y > point.y && side > 0) ++winding;
            if (a.y > point.y && b.y <= point.y && side < 0) --winding;
        }
    }
    return winding != 0;
}
std::vector<path_command> vector_test_stroke(std::string_view d, const vector_detail::stroke &pen)
{
    using namespace renderer_detail::vector_geometry;
    vector_error error;
    std::vector<path_command> commands, outline;
    std::vector<contour> paths;
    CHECK(vector_detail::svg_path(d, commands, POINT_LIMIT, error));
    CHECK(flatten(commands, .005, paths, error));
    CHECK(stroke_path(paths, pen, outline, error));
    CHECK(error.code == vector_error_code::NONE);
    return outline;
}
} // namespace

PUI_TEST(vector_stroke_caps_and_closed_rings_match_independent_region_checks)
{
    vector_detail::stroke pen{{.width = 2}};
    auto line = vector_test_stroke("M0 0L10 0", pen);
    CHECK(vector_outline_contains(line, {5, .9f}));
    CHECK(!vector_outline_contains(line, {-0.5f, 0}));
    CHECK(!vector_outline_contains(line, {5, 1.1f}));
    pen.cap = path_line_cap::ROUND;
    line = vector_test_stroke("M0 0L10 0", pen);
    CHECK(vector_outline_contains(line, {-.6f, .6f}));
    CHECK(!vector_outline_contains(line, {-.9f, .9f}));
    pen.cap = path_line_cap::SQUARE;
    line = vector_test_stroke("M0 0L10 0", pen);
    CHECK(vector_outline_contains(line, {-.9f, .9f}));
    CHECK(!vector_outline_contains(line, {-1.1f, 0}));
    const auto ring = vector_test_stroke("M0 0H10V10H0Z", pen);
    CHECK(vector_outline_contains(ring, {-.5f, 5}));
    CHECK(!vector_outline_contains(ring, {5, 5}));
    CHECK(vector_outline_contains(ring, {10.9f, 10.9f}));
    CHECK(!vector_outline_contains(ring, {11.1f, 5}));
    const auto degenerate = vector_test_stroke("M0 0L10 0Z", pen);
    CHECK(vector_outline_contains(degenerate, {5, 0}));
    CHECK(!vector_outline_contains(degenerate, {-.5f, 0}));
    const auto move = vector_test_stroke("M5 5", pen);
    CHECK(move.empty());
    const auto zero = vector_test_stroke("M5 5L5 5", pen);
    CHECK(vector_outline_contains(zero, {5.8f, 5.8f}));
}

PUI_TEST(vector_stroke_joins_respect_round_bevel_and_miter_limits)
{
    vector_detail::stroke pen{{.width = 2}};
    auto outline = vector_test_stroke("M0 0L10 0L10 10", pen);
    CHECK(vector_outline_contains(outline, {10.9f, -.9f}));
    pen.join = path_line_join::BEVEL;
    outline = vector_test_stroke("M0 0L10 0L10 10", pen);
    CHECK(!vector_outline_contains(outline, {10.6f, -.6f}));
    pen.join = path_line_join::ROUND;
    outline = vector_test_stroke("M0 0L10 0L10 10", pen);
    CHECK(vector_outline_contains(outline, {10.6f, -.6f}));
    CHECK(!vector_outline_contains(outline, {10.9f, -.9f}));
    pen.join = path_line_join::MITER;
    pen.miter_limit = 1;
    outline = vector_test_stroke("M0 0L10 0L10 10", pen);
    CHECK(!vector_outline_contains(outline, {10.6f, -.6f}));
}

PUI_TEST(vector_stroke_dash_phase_zero_dots_and_closed_seams_are_explicit)
{
    vector_detail::stroke pen{{.width = 2}, {4, 2}};
    auto outline = vector_test_stroke("M0 0H10", pen);
    CHECK(vector_outline_contains(outline, {3, 0}));
    CHECK(!vector_outline_contains(outline, {5, 0}));
    CHECK(vector_outline_contains(outline, {7, 0}));
    pen.dash_offset = -1;
    outline = vector_test_stroke("M0 0H10", pen);
    CHECK(!vector_outline_contains(outline, {.5f, 0}));
    CHECK(vector_outline_contains(outline, {1.5f, 0}));
    pen.dash_offset = 0;
    pen.dashes = {0, 4};
    pen.cap = path_line_cap::ROUND;
    outline = vector_test_stroke("M0 0H8", pen);
    CHECK(vector_outline_contains(outline, {0, .5f}));
    CHECK(!vector_outline_contains(outline, {2, 0}));
    CHECK(vector_outline_contains(outline, {4, .5f}));
    using namespace renderer_detail::vector_geometry;
    contour square{{{0, 0}, {10, 0}, {10, 10}, {0, 10}}, true, true};
    std::vector<contour> dashed;
    vector_error error;
    pen.dashes = {8, 4};
    CHECK(dash_contour(square, pen, dashed, error));
    CHECK(dashed.size() == 3);
    if (dashed.size() == 3)
        CHECK(dashed[0].points.size() == 3 && dashed[0].points[1].x == 0 &&
              dashed[0].points[1].y == 0);
    dashed.clear();
    pen.dashes = {100, 20};
    CHECK(dash_contour(square, pen, dashed, error));
    CHECK(dashed.size() == 1 && dashed[0].closed);
    dashed.clear();
    pen.dashes = {1e-8f, 1e-8f};
    CHECK(!dash_contour(square, pen, dashed, error));
    CHECK(error.code == vector_error_code::LIMIT);
}

PUI_TEST(vector_round_strokes_match_distance_union_for_sharp_and_crossing_paths)
{
    const std::vector<std::vector<vec2>> cases = {{{0, 0}, {10, 0}, {0, .1f}},
                                                  {{0, 0}, {10, 0}, {9, 1}},
                                                  {{0, 0}, {10, 10}, {0, 10}, {10, 0}},
                                                  {{0, 0}, {1, 0}, {1, 1}, {0, 1}, {0, .1f}}};
    vector_detail::stroke pen{
        {.width = 2, .cap = path_line_cap::ROUND, .join = path_line_join::ROUND}};
    for (const auto &points : cases)
    {
        renderer_detail::vector_geometry::contour contour{points, false, true};
        std::vector<path_command> outline;
        vector_error error;
        CHECK(renderer_detail::vector_geometry::stroke_contour(contour, pen, outline, error));
        i32 mismatches = 0;
        for (i32 iy = -6; iy <= 46; ++iy)
            for (i32 ix = -6; ix <= 46; ++ix)
            {
                const vec2 point{ix * .25f + .03125f, iy * .25f + .0625f};
                f64 nearest = 1e30;
                for (usize i = 1; i < points.size(); ++i)
                {
                    const f64 dx = static_cast<f64>(points[i].x) - points[i - 1].x,
                              dy = static_cast<f64>(points[i].y) - points[i - 1].y;
                    const f64 t = std::clamp(
                        ((point.x - points[i - 1].x) * dx + (point.y - points[i - 1].y) * dy) /
                            (dx * dx + dy * dy),
                        0.0, 1.0);
                    const f64 x = point.x - (points[i - 1].x + t * dx),
                              y = point.y - (points[i - 1].y + t * dy);
                    nearest = std::min(nearest, x * x + y * y);
                }
                if (std::abs(nearest - 1) > .002 &&
                    vector_outline_contains(outline, point) != (nearest < 1))
                    ++mismatches;
            }
        CHECK(mismatches == 0);
    }
}

PUI_TEST(vector_portable_raster_isolates_group_opacity_and_unassociates_final_rgb)
{
    auto image = load_svg(
        R"svg(<svg viewBox="0 0 16 16"><g opacity=".5"><rect width="12" height="12" fill="red"/><rect x="4" y="4" width="12" height="12" fill="blue"/></g></svg>)svg");
    CHECK(image);
    if (!image) return;
    std::vector<u8> pixels;
    vector_error error;
    CHECK(renderer_detail::vector_raster::raster_document(
        image.image->source, 16, 16, color::black(), 1024 * 1024, pixels, error));
    const auto at = [&](i32 x, i32 y) { return pixels.data() + (y * 16 + x) * 4; };
    CHECK(at(2, 2)[0] == 255 && at(2, 2)[1] == 0 && at(2, 2)[2] == 0 && at(2, 2)[3] == 128);
    CHECK(at(8, 8)[0] == 0 && at(8, 8)[1] == 0 && at(8, 8)[2] == 255 && at(8, 8)[3] == 128);
    CHECK(at(1, 15)[3] == 0);
    destroy_vector(image.image);
}

PUI_TEST(vector_portable_raster_evenodd_holes_and_adjacent_clip_union_are_exact)
{
    for (const char *rule : {"evenodd", "nonzero"})
    {
        const std::string input = std::string("<svg viewBox='0 0 16 16'><path d='M1 1H15V15H1Z M5 "
                                              "5H11V11H5Z' fill='red' fill-rule='") +
                                  rule + "'/></svg>";
        auto image = load_svg(input);
        CHECK(image);
        if (!image) continue;
        std::vector<u8> pixels;
        vector_error error;
        CHECK(renderer_detail::vector_raster::raster_document(
            image.image->source, 16, 16, color::black(), 1024 * 1024, pixels, error));
        CHECK(pixels[(2 * 16 + 2) * 4 + 3] == 255);
        CHECK(pixels[(8 * 16 + 8) * 4 + 3] == (std::string_view(rule) == "evenodd" ? 0 : 255));
        destroy_vector(image.image);
    }
    auto image = load_svg(
        R"svg(<svg viewBox="0 0 16 16"><defs><clipPath id="c"><rect width="8.5" height="16"/><rect x="8.5" width="7.5" height="16"/></clipPath></defs><g clip-path="url(#c)"><rect width="16" height="16" fill="red"/></g></svg>)svg");
    CHECK(image);
    if (!image) return;
    std::vector<u8> pixels;
    vector_error error;
    CHECK(renderer_detail::vector_raster::raster_document(
        image.image->source, 16, 16, color::black(), 1024 * 1024, pixels, error));
    for (usize i = 0; i < pixels.size(); i += 4) CHECK(pixels[i] == 255 && pixels[i + 3] == 255);
    destroy_vector(image.image);
}

PUI_TEST(vector_paint_sampling_uses_straight_svg_color_and_independent_alpha)
{
    using namespace renderer_detail::vector_raster;
    vector_detail::paint paint;
    paint.kind = vector_paint_kind::LINEAR;
    paint.stops = {{0, {255, 0, 0, 255}}, {1, {0, 0, 255, 0}}};
    auto sampler = prepare_paint(paint, {0, 0, 1, 1}, {});
    const rgba middle = sample_paint(sampler, {.5f, 0}, color::black());
    CHECK(middle.r == .5f && middle.g == 0 && middle.b == .5f && middle.a == .5f);
    paint.spread = vector_spread::REPEAT;
    const rgba repeated = sample_paint(sampler, {1.5f, 0}, color::black());
    CHECK(repeated.r == middle.r && repeated.a == middle.a);
    paint.spread = vector_spread::REFLECT;
    const rgba reflected = sample_paint(sampler, {-1.5f, 0}, color::black());
    CHECK(reflected.r == middle.r && reflected.a == middle.a);
    paint.kind = vector_paint_kind::RADIAL;
    paint.stops[1].tint.a = 255;
    const rgba center = sample_paint(sampler, {.5f, .5f}, color::black());
    const rgba edge = sample_paint(sampler, {1, .5f}, color::black());
    CHECK(center.r == 1 && center.b == 0 && edge.r == 0 && edge.b == 1);
    std::vector<u8> pixels;
    vector_error error;
    auto image = load_svg("<svg viewBox='0 0 16 16'><rect width='16' height='16'/></svg>");
    CHECK(image);
    if (!image) return;
    CHECK(!raster_document(image.image->source, 16, 16, color::black(), 32, pixels, error));
    CHECK(error.code == vector_error_code::LIMIT);
    destroy_vector(image.image);
}

PUI_TEST(vector_svg_gradient_templates_clone_stop_styles_in_host_context)
{
    auto image = load_svg(
        R"svg(<svg viewBox="0 0 10 10"><defs><linearGradient id="base" color="red" stop-color="green" stop-opacity=".2"><stop offset="0" stop-color="currentColor"/><stop offset="1"/></linearGradient><linearGradient id="derived" href="#base" color="blue"/></defs><rect width="10" height="10" fill="url(#derived)"/></svg>)svg");
    CHECK(image);
    if (!image) return;
    const auto &stops = image.image->source.shapes[0].fill.stops;
    CHECK(stops.size() == 2);
    if (stops.size() == 2)
    {
        CHECK(stops[0].tint.r == 0 && stops[0].tint.b == 255 && stops[0].opacity == 1);
        CHECK(stops[1].tint.r == 0 && stops[1].tint.g == 0 && stops[1].tint.b == 0 &&
              stops[1].opacity == 1);
    }
    destroy_vector(image.image);
}

PUI_TEST(vector_renderer_preparation_retains_source_and_draws_with_bounded_batches)
{
    tf_env env;
    ui u(env.c);
    auto image = load_svg("<svg viewBox='0 0 16 16'><rect width='16' height='16'/></svg>");
    CHECK(image);
    if (!image) return;
    CHECK(u.prepare_vector(image.image, {.max_pixel_size = {16, 16}, .max_instances = 8}));
    CHECK(env.nd.textures == 1);
    vector_handle borrowed = image.image;
    destroy_vector(image.image);
    CHECK(borrowed->references.load() == 1);
    env.frame(0, [&](ui &frame) { CHECK(frame.draw_vector({0, 0, 16, 16}, borrowed)); });
    vector_allocation_count = 0;
    env.frame(.016,
              [&](ui &frame)
              {
                  vector_count_allocations = true;
                  for (i32 i = 0; i < 5000; ++i) CHECK(frame.draw_vector({0, 0, 16, 16}, borrowed));
                  vector_count_allocations = false;
              });
    CHECK(vector_allocation_count == 0);
    CHECK(env.nd.textures == 1 && env.nd.vertices == 20000 && env.nd.draw_calls >= 625);
    CHECK(u.release_vector(borrowed));
    const auto *painter = static_cast<renderer_detail::mesh_painter *>(env.c->painter);
    CHECK(painter->vector_images->entries.empty());
}

PUI_TEST(vector_renderer_rejects_drawing_path_preparation_and_unprepared_extents)
{
    tf_env env;
    ui u(env.c);
    auto image = load_svg("<svg viewBox='0 0 16 16'><rect width='16' height='16'/></svg>");
    CHECK(image);
    if (!image) return;
    CHECK(u.draw_vector({0, 0, 16, 16}, image.image).error.code ==
          vector_error_code::WRONG_CONTEXT);
    env.frame(
        0,
        [&](ui &frame)
        {
            CHECK(frame.draw_vector({0, 0, 16, 16}, image.image).error.code ==
                  vector_error_code::NOT_PREPARED);
            CHECK(frame.draw_vector({0, 0, 0, 16}, image.image));
            CHECK(frame.draw_vector({1000, 1000, 16, 16}, image.image));
            CHECK(frame.prepare_vector(image.image).error.code == vector_error_code::WRONG_CONTEXT);
            CHECK(frame.release_vector(image.image).error.code == vector_error_code::WRONG_CONTEXT);
        });
    CHECK(env.nd.textures == 0 && env.nd.vertices == 0);
    CHECK(u.prepare_vector(image.image, {.max_pixel_size = {16, 16}}));
    env.frame(.016,
              [&](ui &frame)
              {
                  CHECK(frame.draw_vector({0, 0, 32, 32}, image.image).error.code ==
                        vector_error_code::NOT_PREPARED);
                  CHECK(frame
                            .draw_vector({0, 0, 16, 16}, image.image,
                                         {.style = {.current_color = {255, 0, 0, 255}}})
                            .error.code == vector_error_code::NOT_PREPARED);
                  env.c->clip_stack[0] = {0, 0, 0, 16};
                  env.c->clip_depth = 1;
                  CHECK(frame.draw_vector({0, 0, 16, 16}, image.image));
                  env.c->clip_depth = 0;
              });
    CHECK(env.nd.vertices == 0);
    destroy_vector(image.image);
}

PUI_TEST(vector_renderer_upgrades_monotonically_and_rebind_releases_residency)
{
    tf_env env;
    ui u(env.c);
    auto image = load_svg("<svg viewBox='0 0 16 16'><rect width='16' height='16'/></svg>");
    CHECK(image);
    if (!image) return;
    CHECK(u.prepare_vector(image.image, {.max_pixel_size = {32, 16}}));
    CHECK(u.prepare_vector(image.image, {.max_pixel_size = {16, 32}}));
    auto *painter = static_cast<renderer_detail::mesh_painter *>(env.c->painter);
    const auto *entry = painter->vector_images->get(image.image, color::black());
    CHECK(entry && entry->max_pixels.x == 32 && entry->max_pixels.y == 32);
    CHECK(image.image->references.load() == 2);
    set_device(env.c, &env.nd, &env.nd.my_surface);
    CHECK(image.image->references.load() == 1);
    env.frame(0,
              [&](ui &frame)
              {
                  CHECK(frame.draw_vector({0, 0, 16, 16}, image.image).error.code ==
                        vector_error_code::NOT_PREPARED);
              });
    CHECK(u.prepare_vector(image.image, {.max_pixel_size = {16, 16}}));
    destroy_vector(image.image);
}

PUI_TEST(vector_renderer_allocation_and_device_failures_keep_previous_residency)
{
    struct fail_device : null_device
    {
        bool fail_texture = false, fail_filter = false;
        int live = 0;
        texture_handle create_texture(i32 w, i32 h, const u8 *pixels) override
        {
            if (fail_texture) return nullptr;
            ++live;
            return null_device::create_texture(w, h, pixels);
        }
        void destroy_texture(texture_handle) override { --live; }
        void set_texture_filter(texture_handle, bool) override
        {
            if (fail_filter) throw std::bad_alloc{};
        }
    } device;
    context *c = create_context(&device, device.create_surface());
    set_violation_handler(c, capture_violation, nullptr);
    ui u(c);
    auto image = load_svg("<svg viewBox='0 0 16 16'><rect width='16' height='16'/></svg>");
    CHECK(image);
    if (!image)
    {
        destroy_context(c);
        return;
    }
    CHECK(u.prepare_vector(image.image, {.max_pixel_size = {16, 16}}));
    auto *painter = static_cast<renderer_detail::mesh_painter *>(c->painter);
    const auto original = painter->vector_images->entries[0].texture;
    vector_fail_allocation_after = 0;
    const auto allocation = u.prepare_vector(image.image, {.max_pixel_size = {32, 32}});
    vector_fail_allocation_after = -1;
    CHECK(!allocation && allocation.error.code == vector_error_code::LIMIT && device.live == 1);
    device.fail_texture = true;
    CHECK(u.prepare_vector(image.image, {.max_pixel_size = {32, 32}}).error.code ==
          vector_error_code::DEVICE_ERROR);
    device.fail_texture = false;
    device.fail_filter = true;
    CHECK(u.prepare_vector(image.image, {.max_pixel_size = {32, 32}}).error.code ==
          vector_error_code::LIMIT);
    device.fail_filter = false;
    CHECK(device.live == 1 && painter->vector_images->entries[0].texture == original &&
          image.image->references.load() == 2);
    CHECK(u.prepare_vector(image.image, {.max_pixel_size = {32, 32}}));
    CHECK(device.live == 1);
    destroy_vector(image.image);
    destroy_context(c);
    CHECK(device.live == 0);
}

PUI_TEST(vector_portable_group_clip_intersection_does_not_square_antialiasing)
{
    auto image = load_svg(
        R"svg(<svg viewBox="0 0 32 32"><defs><clipPath id="c"><circle cx="16" cy="16" r="11.3"/></clipPath></defs>
      <g clip-path="url(#c)"><rect width="32" height="32" fill="red"/></g></svg>)svg");
    CHECK(image);
    if (!image) return;
    std::vector<u8> single, repeated;
    vector_error error;
    CHECK(renderer_detail::vector_raster::raster_document(
        image.image->source, 32, 32, {0, 0, 0, 255}, 16 * 1024 * 1024, single, error));
    bool changed = false;
    for (auto &op : image.image->source.operations)
        if (op.kind == vector_operation_kind::BEGIN_GROUP && !op.clips.empty())
        {
            op.clips.push_back(op.clips.front());
            changed = true;
            break;
        }
    CHECK(changed);
    CHECK(renderer_detail::vector_raster::raster_document(
        image.image->source, 32, 32, {0, 0, 0, 255}, 16 * 1024 * 1024, repeated, error));
    CHECK(single == repeated);
    destroy_vector(image.image);
}

PUI_TEST(vector_boolean_signed_winding_and_canonical_intersection_are_consistent)
{
    using namespace renderer_detail::vector_geometry;
    const path_command positive[] = {{path_verb::MOVE, {0, 0}},
                                     {path_verb::LINE, {16, 0}},
                                     {path_verb::LINE, {16, 16}},
                                     {path_verb::LINE, {0, 16}},
                                     {path_verb::CLOSE}};
    std::vector<path_command> normalized, negative, intersection, disjoint;
    vector_error error;
    CHECK(normalize_path(positive, .001, winding_mode::POSITIVE, normalized, error));
    CHECK(!normalized.empty());
    CHECK(normalize_path(positive, .001, winding_mode::NEGATIVE, negative, error));
    CHECK(negative.empty());
    auto duplicate = normalized;
    duplicate.insert(duplicate.end(), normalized.begin(), normalized.end());
    CHECK(normalize_path(duplicate, .001, winding_mode::INTERSECTION, intersection, error));
    CHECK(!intersection.empty());
    rect bounds;
    CHECK(vector_detail::command_bounds(intersection, bounds, error));
    CHECK(bounds.x == 0 && bounds.y == 0 && bounds.w == 16 && bounds.h == 16);
    duplicate = normalized;
    auto shifted = normalized;
    for (auto &command : shifted)
        if (command.verb != path_verb::CLOSE) command.p1.x += 32;
    duplicate.insert(duplicate.end(), shifted.begin(), shifted.end());
    CHECK(normalize_path(duplicate, .001, winding_mode::INTERSECTION, disjoint, error));
    CHECK(disjoint.empty());
}

PUI_TEST(vector_source_allocation_failures_are_structured_and_leave_no_partial_owner)
{
    const path_command square[] = {{path_verb::MOVE, {0, 0}},
                                   {path_verb::LINE, {16, 0}},
                                   {path_verb::LINE, {16, 16}},
                                   {path_verb::LINE, {0, 16}},
                                   {path_verb::CLOSE}};
    vector_fail_allocation_after = 0;
    const auto path = create_path({.commands = square});
    vector_fail_allocation_after = -1;
    CHECK(!path && path.error.code == vector_error_code::LIMIT);
    vector_fail_allocation_after = 0;
    const auto svg = load_svg("<svg viewBox='0 0 16 16'><rect width='16' height='16'/></svg>");
    vector_fail_allocation_after = -1;
    CHECK(!svg && svg.error.code == vector_error_code::LIMIT);
    auto valid = create_path({.commands = square});
    CHECK(valid);
    destroy_vector(valid.image);
}

PUI_TEST(vector_svg_use_preserves_referenced_viewport_and_css_keyword_semantics)
{
    auto image = load_svg(
        R"svg(<svg viewBox="0 0 100 100"><defs><svg id="panel" x="3" y="4" width="20" height="10" viewBox="0 0 20 10"><rect width="20" height="10" fill="RED" stroke="NONE"/></svg></defs><use href="#panel" x="5" y="7"/><use href="#panel" x="30" y="7" width="40" height="20"/></svg>)svg");
    CHECK(image);
    if (image)
    {
        CHECK(image.image->source.shapes.size() == 2);
        const auto &a = image.image->source.shapes[0], &b = image.image->source.shapes[1];
        CHECK(a.transform.a == 1 && a.transform.d == 1 && a.transform.e == 8 &&
              a.transform.f == 11);
        CHECK(b.transform.a == 2 && b.transform.d == 2 && b.transform.e == 33 &&
              b.transform.f == 11);
        CHECK(a.fill.tint.r == 255 && a.outline.kind == vector_paint_kind::NONE);
        destroy_vector(image.image);
    }
    auto gradient = load_svg(
        R"svg(<svg viewBox="0 0 16 16"><defs><linearGradient id="MiXeD"><stop stop-color="red"/><stop offset="1" stop-color="blue"/></linearGradient></defs><rect width="16PX" height="16" fill="URL(#MiXeD)" fill-rule="EVENODD"/></svg>)svg");
    CHECK(gradient);
    if (gradient)
    {
        CHECK(gradient.image->source.shapes[0].fill.kind == vector_paint_kind::LINEAR);
        destroy_vector(gradient.image);
    }
    for (auto xml : {"<svg xmlns:xml='urn:bad'/>", "<svg xmlns:a:b='urn:bad'/>",
                     "<svg xmlns:xmlns='urn:bad'/>", "<svg xmlns:a=''/>",
                     "<svg xmlns:a='http://www.w3.org/XML/1998/namespace'/>"})
        CHECK(load_svg(xml).error.code == vector_error_code::INVALID_SVG);
}

PUI_TEST(vector_svg_resolves_namespace_scopes_and_inline_important_without_silent_changes)
{
    auto image = load_svg(
        R"svg(<s:svg xmlns:s="http://www.w3.org/2000/svg" xmlns:h="http://www.w3.org/1999/xlink" viewBox="0 0 32 32">
      <s:defs><s:rect id="r" style="WIDTH:16px;HEIGHT:12px;FILL:red ! IMPORTANT;fill:blue"/></s:defs>
      <s:use h:href="#r"/></s:svg>)svg");
    CHECK(image);
    if (image)
    {
        const auto &shape = image.image->source.shapes.front();
        CHECK(shape.bounds.w == 16 && shape.bounds.h == 12);
        CHECK(shape.fill.tint.r == 255 && shape.fill.tint.b == 0);
        destroy_vector(image.image);
    }
    const std::string_view invalid[] = {
        R"(<svg xmlns="http://www.w3.org/2000/svg"><g xmlns="urn:other"><rect width="10" height="10"/></g></svg>)",
        R"(<svg><foreign:rect width="10" height="10"/></svg>)",
        R"(<svg><g style="transform-origin:center"><rect width="10" height="10"/></g></svg>)",
        R"(<svg><path pathLength="100" d="M0 0H10" stroke="red" stroke-dasharray="1 2"/></svg>)"};
    for (auto source : invalid)
    {
        const auto result = load_svg(source);
        CHECK(!result && result.error.code != vector_error_code::NONE &&
              result.error.offset <= source.size());
    }
    auto metadata = load_svg(
        R"(<svg xmlns="http://www.w3.org/2000/svg" xmlns:r="urn:metadata"><metadata><r:data>ignored description</r:data></metadata><rect width="10" height="10"/></svg>)");
    CHECK(metadata);
    destroy_vector(metadata.image);
}

#if defined(PUFFERUI_ENABLE_HARFBUZZ)
PUI_TEST(vector_slug_encodes_arbitrary_shapes_without_font_or_atlas_state)
{
    tf_env env;
    CHECK(env.c->ts == nullptr);
    using namespace renderer_detail::vector_slug;
    encoder encoder;
    path_data encoded, repeat;
    vector_error error;
    const path_command square[] = {{path_verb::MOVE, {0, 0}},
                                   {path_verb::LINE, {16, 0}},
                                   {path_verb::LINE, {16, 16}},
                                   {path_verb::LINE, {0, 16}},
                                   {path_verb::CLOSE}};
    const encode_opts options{.view_box = {0, 0, 16, 16}};
    CHECK(encoder.encode(square, options, encoded, error));
    CHECK(!encoded.bytes.empty() && encoded.bytes.size() % 8 == 0);
    CHECK(env.c->ts == nullptr && env.nd.textures == 0);
    CHECK(encoder.encode(square, options, repeat, error));
    CHECK(repeat.bytes == encoded.bytes);
    std::vector<path_command> duplicate(std::begin(square), std::end(square));
    duplicate.insert(duplicate.end(), std::begin(square), std::end(square));
    CHECK(encoder.encode(duplicate, options, repeat, error));
    CHECK(repeat.bytes == encoded.bytes);
    auto evenodd = options;
    evenodd.rule = path_fill_rule::EVENODD;
    CHECK(encoder.encode(duplicate, evenodd, repeat, error));
    CHECK(repeat.bytes.empty());
    auto small_budget = options;
    small_budget.budget_bytes = 1;
    error = {};
    CHECK(!encoder.encode(square, small_budget, repeat, error));
    CHECK(error.code == vector_error_code::LIMIT);
}

PUI_TEST(vector_slug_keeps_thin_details_and_nonuniform_viewboxes_in_native_range)
{
    using namespace renderer_detail::vector_slug;
    encoder encoder;
    path_data encoded;
    vector_error error;
    const path_command thin[] = {{path_verb::MOVE, {0, 0}},
                                 {path_verb::LINE, {.0001f, 0}},
                                 {path_verb::LINE, {.0001f, .0001f}},
                                 {path_verb::LINE, {0, .0001f}},
                                 {path_verb::CLOSE}};
    CHECK(encoder.encode(thin, {.view_box = {0, 0, 1, 1}}, encoded, error));
    CHECK(!encoded.bytes.empty());
    CHECK(encoded.scale.x > 10000 && encoded.scale.y > 10000);
    const path_command wide[] = {{path_verb::MOVE, {0, 0}},
                                 {path_verb::LINE, {1000, 0}},
                                 {path_verb::LINE, {1000, 10}},
                                 {path_verb::LINE, {0, 10}},
                                 {path_verb::CLOSE}};
    CHECK(encoder.encode(wide, {.view_box = {0, 0, 1000, 10}}, encoded, error));
    CHECK(!encoded.bytes.empty());
    CHECK(encoded.scale.y > encoded.scale.x * 50);
    const path_command bezier[] = {{path_verb::MOVE, {0, 0}},
                                   {path_verb::CUBIC, {0, 100}, {100, 100}, {100, 0}},
                                   {path_verb::QUADRATIC, {50, -50}, {0, 0}},
                                   {path_verb::CLOSE}};
    CHECK(encoder.encode(bezier, {.view_box = {0, -50, 100, 125}}, encoded, error));
    CHECK(!encoded.bytes.empty());
    CHECK(encoder.encode({}, {.view_box = {}}, encoded, error));
    CHECK(encoded.bytes.empty());
}

PUI_TEST(vector_slug_null_encode_result_is_a_recoverable_structured_limit)
{
    using namespace renderer_detail::vector_slug;
    const path_command square[] = {{path_verb::MOVE, {0, 0}},
                                   {path_verb::LINE, {16, 0}},
                                   {path_verb::LINE, {16, 16}},
                                   {path_verb::LINE, {0, 16}},
                                   {path_verb::CLOSE}};
    std::vector<path_command> commands;
    commands.reserve(16384 * 5);
    for (i32 i = 0; i < 16384; ++i)
        commands.insert(commands.end(), std::begin(square), std::end(square));
    encoder encoder;
    path_data data;
    vector_error error;
    CHECK(!encoder.encode(commands, {.view_box = {0, 0, 16, 16}, .canonical = true}, data, error));
    CHECK(error.code == vector_error_code::LIMIT && data.bytes.empty());
    error = {};
    CHECK(encoder.encode(square, {.view_box = {0, 0, 16, 16}}, data, error));
    CHECK(!data.bytes.empty());
}

PUI_TEST(vector_slug_scene_metadata_keeps_gradient_coordinates_and_isolated_groups)
{
    using namespace renderer_detail::vector_slug;
    auto image = load_svg(R"svg(<svg viewBox="0 0 64 64"><defs>
      <linearGradient id="paint"><stop stop-color="red"/><stop offset="1" stop-color="currentColor" stop-opacity=".5"/></linearGradient>
      <clipPath id="clip"><circle cx="24" cy="24" r="20"/></clipPath></defs>
      <g opacity=".5" clip-path="url(#clip)"><rect x="4" y="8" width="16" height="24" transform="translate(5 6)"
       fill="url(#paint)" stroke="blue" stroke-width="2"/></g></svg>)svg");
    CHECK(image);
    if (!image) return;
    prepared_scene prepared;
    vector_error error;
    CHECK(prepare_scene(image.image->source, {}, prepared, error));
    CHECK(prepared.quads >= 3 && prepared.max_depth >= 1);
    const auto scalar = [&](u32 word)
    {
        f32 value;
        std::memcpy(&value, &prepared.words[word], sizeof(value));
        return value;
    };
    bool gradient = false, group = false;
    for (usize i = 0; i < prepared.steps.size(); ++i)
    {
        const auto &step = prepared.steps[i];
        if (step.type == scene_step::PUSH)
        {
            CHECK(step.pair > i && step.pair < prepared.steps.size());
            CHECK(prepared.steps[step.pair].type == scene_step::POP &&
                  prepared.steps[step.pair].pair == i);
            if (step.opacity == .5f) group = true;
        }
        if (step.type != scene_step::PAINT) continue;
        CHECK(step.path + 8 <= prepared.words.size() && step.paint + 32 <= prepared.words.size());
        CHECK(prepared.words[step.path] * 2 < step.path);
        if (prepared.words[step.paint] == static_cast<u32>(vector_paint_kind::LINEAR))
        {
            gradient = true;
            CHECK(prepared.words[step.paint + 2] == 2);
            CHECK(scalar(step.paint + 8) == 1.0f / 16 && scalar(step.paint + 10) == -9.0f / 16);
            CHECK(std::abs(scalar(step.paint + 13) - 1.0f / 24) < .000001f);
            CHECK(scalar(step.paint + 25) == 1);
            CHECK(prepared.words[step.paint + 32 + 8 + 1] == 1);
        }
    }
    CHECK(gradient && group);
    const auto previous = prepared.words;
    error = {};
    CHECK(!prepare_scene(image.image->source, {.resource_budget_bytes = 1}, prepared, error));
    CHECK(error.code == vector_error_code::LIMIT && prepared.words == previous);
    destroy_vector(image.image);
}

PUI_TEST(vector_slug_empty_clip_is_distinct_from_unclipped_and_bad_graphs_are_rejected)
{
    using namespace renderer_detail::vector_slug;
    auto image = load_svg(
        R"svg(<svg viewBox="0 0 16 16"><defs><clipPath id="empty"><rect width="0" height="0"/></clipPath></defs>
      <g clip-path="url(#empty)"><path d="M0 0H16V16H0Z"/></g></svg>)svg");
    CHECK(image);
    if (!image) return;
    prepared_scene prepared;
    vector_error error;
    CHECK(prepare_scene(image.image->source, {}, prepared, error));
    bool empty = false;
    for (const auto &step : prepared.steps)
        if (step.type == scene_step::PUSH && step.clip == EMPTY_CLIP) empty = true;
    CHECK(empty && EMPTY_CLIP != NO_CLIP);
    vector_detail::document invalid;
    invalid.view_box = {0, 0, 16, 16};
    invalid.operations.push_back({vector_operation_kind::END_GROUP});
    error = {};
    CHECK(!prepare_scene(invalid, {}, prepared, error) &&
          error.code == vector_error_code::INVALID_PATH);
    destroy_vector(image.image);
}
#endif

#if defined(PUFFERUI_ENABLE_VULKAN) && defined(PUFFERUI_ENABLE_HARFBUZZ)
#include "test_vulkan_vector_pipeline.inl"
#include "test_vulkan_vector_painter.inl"
#include "test_vulkan_vector_lifecycle.inl"
#include "test_vulkan_vector_overlay.inl"
#include "test_vulkan_vector_bounds.inl"
#include "test_vulkan_vector_faults.inl"
#include "bench_vector.inl"
#endif

int main(int argc, char **argv)
{
#if defined(PUFFERUI_ENABLE_VULKAN) && defined(PUFFERUI_ENABLE_HARFBUZZ)
    if (argc >= 2 && std::string_view(argv[1]) == "--bench") return vector_benchmark(argc, argv);
    if (argc == 2 && std::string_view(argv[1]) == "--faults")
    {
        vector_native_fault_regressions();
        return g_failures ? 1 : 0;
    }
    if (argc == 3 && (std::string_view(argv[1]) == "--capture-reference" ||
                      std::string_view(argv[1]) == "--goldens"))
    {
        vector_painter_gpu_regressions(argv[2], std::string_view(argv[1]) == "--capture-reference");
        return g_failures ? 1 : 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--bounds")
    {
        vector_native_bounds_regressions();
        return g_failures ? 1 : 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--lifecycle")
    {
        vector_native_lifecycle_regressions();
        return g_failures ? 1 : 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--overlay")
    {
        vector_overlay_regressions();
        return g_failures ? 1 : 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--painter")
    {
        vector_painter_gpu_regressions();
        return g_failures ? 1 : 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--gpu")
    {
        vector_pipeline_gpu_regressions();
        return g_failures ? 1 : 0;
    }
#endif
    const bool verbose = argc > 1 && std::string_view(argv[1]) == "--verbose";
    vector_verbose = verbose;
    for (const auto &test : test_registry())
    {
        if (verbose)
        {
            std::fprintf(stderr, "running %s\n", test.name);
            std::fflush(stderr);
        }
        test.fn();
    }
    if (!g_failures)
        std::printf("all vector resource regressions passed (%zu tests)\n", test_registry().size());
    return g_failures ? 1 : 0;
}
