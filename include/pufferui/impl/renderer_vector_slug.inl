// Font-independent native vector encoding through the pinned HarfBuzz GPU API.
// Included inside pui after renderer-owned vector preparation helpers.
// General winding is canonicalized before Slug; text shaders stay unchanged.
// Encoder scratch is reused during resource setup, never on the drawing path.
#if defined(PUFFERUI_ENABLE_HARFBUZZ)
namespace renderer_detail::vector_slug
{
struct path_data
{
    std::vector<u8> bytes;
    rect bounds{};
    vec2 center{};
    vec2 scale{1, 1};
};
struct encode_opts
{
    affine2 transform{};
    rect view_box{};
    vec2 max_pixels{256, 256};
    path_fill_rule rule = path_fill_rule::NONZERO;
    bool canonical = false;
    usize budget_bytes = 16 * 1024 * 1024;
};
inline bool self_intersects(const std::vector<vector_geometry::contour> &contours, bool &result,
                            vector_error &error)
{
    result = contours.size() > 1;
    if (result || contours.empty()) return true;
    const auto &points = contours[0].points;
    usize work = 0;
    const auto orientation = [](vec2 a, vec2 b, vec2 p)
    {
        return (static_cast<f64>(b.x) - a.x) * (p.y - a.y) -
               (static_cast<f64>(b.y) - a.y) * (p.x - a.x);
    };
    for (usize i = 0; i < points.size(); ++i)
        for (usize j = i + 1; j < points.size(); ++j)
        {
            if (++work > 64 * 1024 * 1024)
                return vector_detail::fail(error, vector_error_code::LIMIT,
                                           "vector winding classification budget");
            if (j == i + 1 || (i == 0 && j + 1 == points.size())) continue;
            const vec2 a = points[i], b = points[(i + 1) % points.size()], c = points[j],
                       d = points[(j + 1) % points.size()];
            if (std::max(a.x, b.x) < std::min(c.x, d.x) ||
                std::max(c.x, d.x) < std::min(a.x, b.x) ||
                std::max(a.y, b.y) < std::min(c.y, d.y) || std::max(c.y, d.y) < std::min(a.y, b.y))
                continue;
            const f64 ab_c = orientation(a, b, c), ab_d = orientation(a, b, d),
                      cd_a = orientation(c, d, a), cd_b = orientation(c, d, b);
            if ((ab_c <= 0 && ab_d >= 0 || ab_c >= 0 && ab_d <= 0) &&
                (cd_a <= 0 && cd_b >= 0 || cd_a >= 0 && cd_b <= 0))
            {
                result = true;
                return true;
            }
        }
    return true;
}
struct encoder
{
    hb_gpu_draw_t *draw = hb_gpu_draw_create_or_fail();
    ~encoder()
    {
        if (draw) hb_gpu_draw_destroy(draw);
    }
    encoder() = default;
    encoder(const encoder &) = delete;
    encoder &operator=(const encoder &) = delete;
    bool encode(std::span<const path_command> commands, const encode_opts &options,
                path_data &result, vector_error &error)
    {
        result.bytes.clear();
        result.bounds = {};
        result.center = {};
        result.scale = {1, 1};
        if (!draw)
            return vector_detail::fail(error, vector_error_code::LIMIT,
                                       "HarfBuzz vector encoder allocation");
        if (options.view_box.w <= 0 || options.view_box.h <= 0) return true;
        std::vector<path_command> transformed(commands.begin(), commands.end());
        for (auto &c : transformed)
        {
            if (c.verb == path_verb::CLOSE) continue;
            c.p1 = options.transform.apply(c.p1);
            if (c.verb == path_verb::QUADRATIC || c.verb == path_verb::CUBIC)
                c.p2 = options.transform.apply(c.p2);
            if (c.verb == path_verb::CUBIC) c.p3 = options.transform.apply(c.p3);
        }
        rect bounds;
        if (!vector_detail::command_bounds(transformed, bounds, error)) return false;
        if (bounds.w <= 0 || bounds.h <= 0) return true;
        const f64 pixels_per_unit =
            std::max(static_cast<f64>(options.max_pixels.x) / options.view_box.w,
                     static_cast<f64>(options.max_pixels.y) / options.view_box.h);
        if (!(pixels_per_unit > 0) || !std::isfinite(pixels_per_unit))
            return vector_detail::fail(error, vector_error_code::LIMIT, "Slug vector pixel scale");
        if (!options.canonical)
        {
            std::vector<vector_geometry::contour> contours;
            bool overlaps = false;
            if (!vector_geometry::flatten(transformed, .015625 / pixels_per_unit, contours,
                                          error) ||
                !self_intersects(contours, overlaps, error))
                return false;
            if (overlaps || options.rule == path_fill_rule::EVENODD)
            {
                std::vector<path_command> canonical;
                if (!vector_geometry::normalize_contours(
                        contours, .015625 / pixels_per_unit,
                        options.rule == path_fill_rule::EVENODD
                            ? vector_geometry::winding_mode::EVENODD
                            : vector_geometry::winding_mode::NONZERO,
                        canonical, error))
                    return false;
                transformed.swap(canonical);
                if (transformed.empty()) return true;
                if (!vector_detail::command_bounds(transformed, bounds, error)) return false;
            }
        }
        vector_detail::bounds_builder controls;
        for (const auto &c : transformed)
        {
            if (c.verb == path_verb::CLOSE) continue;
            controls.add(c.p1.x, c.p1.y);
            if (c.verb == path_verb::QUADRATIC || c.verb == path_verb::CUBIC)
                controls.add(c.p2.x, c.p2.y);
            if (c.verb == path_verb::CUBIC) controls.add(c.p3.x, c.p3.y);
        }
        const f64 span_x = controls.right - controls.left, span_y = controls.bottom - controls.top;
        if (!(span_x > 0 && span_y > 0)) return true;
        const f64 pixel_x = static_cast<f64>(options.max_pixels.x) / options.view_box.w,
                  pixel_y = static_cast<f64>(options.max_pixels.y) / options.view_box.h;
        const f64 scale_x =
            std::min(std::max(4 * pixel_x, std::min(1000 / span_x, 32760.0 / options.view_box.w)),
                     16000 / span_x);
        const f64 scale_y =
            std::min(std::max(4 * pixel_y, std::min(1000 / span_y, 32760.0 / options.view_box.h)),
                     16000 / span_y);
        if (scale_x < 3 * pixel_x || scale_y < 3 * pixel_y)
            return vector_detail::fail(error, vector_error_code::LIMIT,
                                       "Slug coordinate range cannot meet requested precision");
        result.center = {static_cast<f32>((controls.left + controls.right) * .5),
                         static_cast<f32>((controls.top + controls.bottom) * .5)};
        result.scale = {static_cast<f32>(scale_x), static_cast<f32>(scale_y)};
        result.bounds = bounds;
        if (!vector_detail::finite(result.scale))
            return vector_detail::fail(error, vector_error_code::LIMIT,
                                       "Slug normalization scale precision");
        const auto normalized = [&](vec2 p)
        {
            return vec2{
                static_cast<f32>((static_cast<f64>(p.x) - result.center.x) * result.scale.x),
                static_cast<f32>((static_cast<f64>(p.y) - result.center.y) * result.scale.y)};
        };
        hb_gpu_draw_clear(draw);
        const i32 em_x = static_cast<i32>(
                      std::clamp(std::ceil(scale_x * options.view_box.w), 1.0, 32767.0)),
                  em_y = static_cast<i32>(
                      std::clamp(std::ceil(scale_y * options.view_box.h), 1.0, 32767.0));
        hb_gpu_draw_set_scale(draw, em_x, em_y);
        auto *functions = hb_gpu_draw_get_funcs(draw);
        hb_draw_state_t state = HB_DRAW_STATE_DEFAULT;
        bool open = false;
        vec2 current{}, start{};
        for (const auto &c : transformed)
        {
            const vec2 p = c.verb == path_verb::CLOSE ? vec2{} : normalized(c.p1);
            const vec2 q = (c.verb == path_verb::QUADRATIC || c.verb == path_verb::CUBIC)
                               ? normalized(c.p2)
                               : vec2{};
            const vec2 r = c.verb == path_verb::CUBIC ? normalized(c.p3) : vec2{};
            const auto valid = [](vec2 point)
            {
                return vector_detail::finite(point) && std::abs(point.x) < 8191.5f &&
                       std::abs(point.y) < 8191.5f;
            };
            if (c.verb != path_verb::CLOSE &&
                (!valid(p) ||
                 ((c.verb == path_verb::QUADRATIC || c.verb == path_verb::CUBIC) && !valid(q)) ||
                 (c.verb == path_verb::CUBIC && !valid(r))))
                return vector_detail::fail(error, vector_error_code::LIMIT,
                                           "Slug normalized control coordinate range");
            if (c.verb == path_verb::MOVE)
            {
                if (open) hb_draw_close_path(functions, draw, &state);
                hb_draw_move_to(functions, draw, &state, p.x, p.y);
                open = true;
                current = start = p;
                continue;
            }
            if (c.verb == path_verb::CLOSE)
            {
                if (open) hb_draw_close_path(functions, draw, &state);
                open = false;
                current = start;
                continue;
            }
            if (!open)
            {
                hb_draw_move_to(functions, draw, &state, current.x, current.y);
                open = true;
                start = current;
            }
            if (c.verb == path_verb::LINE)
            {
                hb_draw_line_to(functions, draw, &state, p.x, p.y);
                current = p;
            }
            else if (c.verb == path_verb::QUADRATIC)
            {
                hb_draw_quadratic_to(functions, draw, &state, p.x, p.y, q.x, q.y);
                current = q;
            }
            else if (c.verb == path_verb::CUBIC)
            {
                hb_draw_cubic_to(functions, draw, &state, p.x, p.y, q.x, q.y, r.x, r.y);
                current = r;
            }
        }
        if (open) hb_draw_close_path(functions, draw, &state);
        hb_glyph_extents_t extents;
        hb_blob_t *blob = hb_gpu_draw_encode(draw, &extents);
        if (!blob)
            return vector_detail::fail(error, vector_error_code::LIMIT,
                                       "HarfBuzz vector encoding range or allocation limit");
        struct recycle
        {
            hb_gpu_draw_t *draw;
            hb_blob_t *blob;
            ~recycle() { hb_gpu_draw_recycle_blob(draw, blob); }
        } cleanup{draw, blob};
        const unsigned length = hb_blob_get_length(blob);
        if (!length || length % 8)
            return vector_detail::fail(error, vector_error_code::LIMIT,
                                       "HarfBuzz vector encoding failed");
        if (length > options.budget_bytes)
            return vector_detail::fail(error, vector_error_code::LIMIT,
                                       "Slug vector encoding budget");
        const auto *bytes = reinterpret_cast<const u8 *>(hb_blob_get_data(blob, nullptr));
        result.bytes.assign(bytes, bytes + length);
        return true;
    }
};
} // namespace renderer_detail::vector_slug
#endif
