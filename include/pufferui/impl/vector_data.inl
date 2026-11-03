// Immutable vector documents, source validation and geometry mathematics.
// Included inside namespace pui; no native SDK or rendering allocation here.
// Renderer implementations own preparation and raster/native representation.
// Source spans are copied before construction returns.
namespace vector_detail
{
constexpr usize COMMAND_LIMIT = 262144, STOP_LIMIT = 256, DASH_LIMIT = 256;
struct paint : vector_paint
{
    bool object_bounds = false;
    // Owned spans hide their borrowed counterparts in the public descriptor.
    std::vector<gradient_stop> stops;
};
struct stroke : path_stroke
{
    std::vector<f32> dashes;
};
struct shape
{
    std::vector<path_command> commands;
    paint fill{}, outline{{.kind = vector_paint_kind::NONE}};
    stroke pen{};
    path_fill_rule rule = path_fill_rule::NONZERO;
    bool stroke_first = false;
    affine2 transform{};
    rect bounds{};
    f32 fill_opacity = 1, stroke_opacity = 1;
    std::vector<u32> clips;
};
struct clip
{
    std::vector<shape> shapes;
    bool object_bounds = false;
};
using operation_kind = vector_operation_kind;
struct operation : vector_operation_view
{
    std::vector<u32> clips;
    operation(operation_kind value = operation_kind::SHAPE, u32 shape_index = 0, f32 alpha = 1)
    {
        kind = value;
        index = shape_index;
        opacity = alpha;
    }
};
struct document
{
    rect view_box{};
    std::vector<shape> shapes;
    std::vector<clip> clips;
    std::vector<operation> operations;
    // SVG viewport aspect alignment: 0 min, .5 mid, 1 max.
    vec2 alignment{.5f, .5f};
    bool preserve_aspect = true, slice = false;
};
inline bool finite(vec2 p)
{
    return std::isfinite(p.x) && std::isfinite(p.y);
}
inline bool finite(const affine2 &m)
{
    return std::isfinite(m.a) && std::isfinite(m.b) && std::isfinite(m.c) && std::isfinite(m.d) &&
           std::isfinite(m.e) && std::isfinite(m.f);
}
inline affine2 multiply(const affine2 &a, const affine2 &b)
{
    return {a.a * b.a + a.c * b.b, a.b * b.a + a.d * b.b,       a.a * b.c + a.c * b.d,
            a.b * b.c + a.d * b.d, a.a * b.e + a.c * b.f + a.e, a.b * b.e + a.d * b.f + a.f};
}
inline bool inverse(const affine2 &m, affine2 &out)
{
    const f64 determinant = static_cast<f64>(m.a) * m.d - static_cast<f64>(m.b) * m.c;
    if (!finite(m) || determinant == 0 || !std::isfinite(determinant)) return false;
    out = {
        static_cast<f32>(m.d / determinant),
        static_cast<f32>(-m.b / determinant),
        static_cast<f32>(-m.c / determinant),
        static_cast<f32>(m.a / determinant),
        static_cast<f32>((static_cast<f64>(m.c) * m.f - static_cast<f64>(m.d) * m.e) / determinant),
        static_cast<f32>((static_cast<f64>(m.b) * m.e - static_cast<f64>(m.a) * m.f) /
                         determinant)};
    return finite(out);
}
inline bool fail(vector_error &error, vector_error_code code, const char *message, usize at = 0)
{
    if (error.code == vector_error_code::NONE) error = {code, at, message};
    return false;
}
struct bounds_builder
{
    f64 left = 0, top = 0, right = 0, bottom = 0;
    bool any = false;
    void add(f64 x, f64 y)
    {
        if (!any)
        {
            left = right = x;
            top = bottom = y;
            any = true;
        }
        else
        {
            left = std::min(left, x);
            right = std::max(right, x);
            top = std::min(top, y);
            bottom = std::max(bottom, y);
        }
    }
    rect value() const
    {
        return any ? rect{static_cast<f32>(left), static_cast<f32>(top),
                          static_cast<f32>(right - left), static_cast<f32>(bottom - top)}
                   : rect{};
    }
};
inline f64 bezier(f64 p0, f64 p1, f64 p2, f64 p3, f64 t)
{
    const f64 a = 1 - t;
    return a * a * a * p0 + 3 * a * a * t * p1 + 3 * a * t * t * p2 + t * t * t * p3;
}
inline void curve_bounds(bounds_builder &box, vec2 p0, vec2 p1, vec2 p2, vec2 p3)
{
    box.add(p0.x, p0.y);
    box.add(p3.x, p3.y);
    for (i32 axis = 0; axis < 2; ++axis)
    {
        const f64 a0 = axis ? p0.y : p0.x, a1 = axis ? p1.y : p1.x;
        const f64 a2 = axis ? p2.y : p2.x, a3 = axis ? p3.y : p3.x;
        const f64 a = -a0 + 3 * a1 - 3 * a2 + a3, b = 2 * (a0 - 2 * a1 + a2), c = a1 - a0;
        const auto add = [&](f64 t)
        {
            if (t > 0 && t < 1)
                box.add(bezier(p0.x, p1.x, p2.x, p3.x, t), bezier(p0.y, p1.y, p2.y, p3.y, t));
        };
        if (a == 0)
        {
            if (b != 0) add(-c / b);
        }
        else
        {
            const f64 d = b * b - 4 * a * c;
            if (d >= 0)
            {
                const f64 root = std::sqrt(d);
                add((-b - root) / (2 * a));
                add((-b + root) / (2 * a));
            }
        }
    }
}
inline bool command_bounds(std::span<const path_command> commands, rect &out, vector_error &error)
{
    if (commands.size() > COMMAND_LIMIT)
        return fail(error, vector_error_code::LIMIT, "vector command limit");
    bounds_builder box;
    vec2 current{}, start{};
    bool open = false;
    for (usize i = 0; i < commands.size(); ++i)
    {
        const auto &c = commands[i];
        if (c.verb != path_verb::CLOSE && !finite(c.p1))
            return fail(error, vector_error_code::INVALID_PATH, "nonfinite path coordinate", i);
        if (c.verb == path_verb::MOVE)
        {
            current = start = c.p1;
            open = true;
            continue;
        }
        if (!open)
            return fail(error, vector_error_code::INVALID_PATH, "path must begin with move", i);
        switch (c.verb)
        {
        case path_verb::LINE:
            box.add(current.x, current.y);
            box.add(c.p1.x, c.p1.y);
            current = c.p1;
            break;
        case path_verb::QUADRATIC:
        {
            if (!finite(c.p2))
                return fail(error, vector_error_code::INVALID_PATH, "nonfinite quadratic", i);
            const vec2 a{static_cast<f32>((static_cast<f64>(current.x) + 2.0 * c.p1.x) / 3),
                         static_cast<f32>((static_cast<f64>(current.y) + 2.0 * c.p1.y) / 3)};
            const vec2 b{static_cast<f32>((static_cast<f64>(c.p2.x) + 2.0 * c.p1.x) / 3),
                         static_cast<f32>((static_cast<f64>(c.p2.y) + 2.0 * c.p1.y) / 3)};
            curve_bounds(box, current, a, b, c.p2);
            current = c.p2;
            break;
        }
        case path_verb::CUBIC:
            if (!finite(c.p2) || !finite(c.p3))
                return fail(error, vector_error_code::INVALID_PATH, "nonfinite cubic", i);
            curve_bounds(box, current, c.p1, c.p2, c.p3);
            current = c.p3;
            break;
        case path_verb::CLOSE:
            box.add(current.x, current.y);
            box.add(start.x, start.y);
            current = start;
            break;
        default:
            return fail(error, vector_error_code::INVALID_PATH, "unknown path command", i);
        }
    }
    out = box.value();
    return std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.w) &&
                   std::isfinite(out.h)
               ? true
               : fail(error, vector_error_code::INVALID_PATH, "path bounds overflow");
}
inline bool copy_paint(const vector_paint &src, paint &dst, vector_error &error)
{
    if (src.kind > vector_paint_kind::RADIAL || src.spread > vector_spread::REFLECT ||
        src.units > vector_paint_units::OBJECT_BOUNDS)
        return fail(error, vector_error_code::INVALID_PAINT, "unknown vector paint");
    if (src.stops.size() > STOP_LIMIT)
        return fail(error, vector_error_code::LIMIT, "gradient stop limit");
    if (!finite(src.start) || !finite(src.end) || !finite(src.center) || !finite(src.focal) ||
        !std::isfinite(src.radius) || src.radius < 0 || !finite(src.transform))
        return fail(error, vector_error_code::INVALID_PAINT, "invalid gradient geometry");
    if ((src.kind == vector_paint_kind::LINEAR || src.kind == vector_paint_kind::RADIAL) &&
        src.stops.size() < 2)
        return fail(error, vector_error_code::INVALID_PAINT, "gradient needs two stops");
    f32 previous = 0;
    for (const auto &stop : src.stops)
    {
        if (!std::isfinite(stop.offset) || stop.offset < previous || stop.offset > 1 ||
            !std::isfinite(stop.opacity) || stop.opacity < 0 || stop.opacity > 1)
            return fail(error, vector_error_code::INVALID_PAINT,
                        "gradient stops must be ordered in [0,1]");
        previous = stop.offset;
    }
    static_cast<vector_paint &>(dst) = src;
    dst.vector_paint::stops = {};
    dst.object_bounds = src.units == vector_paint_units::OBJECT_BOUNDS;
    dst.stops.assign(src.stops.begin(), src.stops.end());
    return true;
}
inline bool copy_stroke(const path_stroke &src, stroke &dst, vector_error &error)
{
    if (!std::isfinite(src.width) || src.width < 0 || !std::isfinite(src.miter_limit) ||
        src.miter_limit < 0 || !std::isfinite(src.dash_offset) || src.cap > path_line_cap::SQUARE ||
        src.join > path_line_join::BEVEL)
        return fail(error, vector_error_code::INVALID_PAINT, "invalid path stroke");
    if (src.dashes.size() > DASH_LIMIT) return fail(error, vector_error_code::LIMIT, "dash limit");
    for (f32 dash : src.dashes)
        if (!std::isfinite(dash) || dash < 0)
            return fail(error, vector_error_code::INVALID_PAINT, "invalid dash length");
    static_cast<path_stroke &>(dst) = src;
    dst.path_stroke::dashes = {};
    dst.dashes.assign(src.dashes.begin(), src.dashes.end());
    return true;
}
inline u64 next_identity()
{
    static std::atomic<u64> next{1};
    u64 value = next.load(std::memory_order_relaxed);
    for (;;)
    {
        if (value == std::numeric_limits<u64>::max()) return 0;
        if (next.compare_exchange_weak(value, value + 1, std::memory_order_relaxed)) return value;
    }
}
} // namespace vector_detail

struct vector_resource
{
    std::atomic<u32> references{1};
    u64 identity = vector_detail::next_identity();
    vector_detail::document source;
};
namespace vector_detail
{
inline bool retain(vector_resource *asset)
{
    if (!asset) return false;
    u32 count = asset->references.load(std::memory_order_relaxed);
    for (;;)
    {
        if (count == std::numeric_limits<u32>::max()) return false;
        if (asset->references.compare_exchange_weak(count, count + 1, std::memory_order_relaxed))
            return true;
    }
}
inline void release(vector_resource *asset)
{
    if (asset && asset->references.fetch_sub(1, std::memory_order_acq_rel) == 1) delete asset;
}
} // namespace vector_detail
void destroy_vector(vector_handle &image)
{
    vector_detail::release(image);
    image = nullptr;
}
rect vector_view_box(vector_handle image)
{
    return image ? image->source.view_box : rect{};
}
bool retain_vector(vector_handle image)
{
    return vector_detail::retain(image);
}
namespace vector_detail
{
inline vector_shape_view shape_view(const shape &shape)
{
    const auto paint_view = [](const paint &source)
    {
        vector_paint result = source;
        result.stops = source.stops;
        result.units = source.object_bounds ? vector_paint_units::OBJECT_BOUNDS
                                            : vector_paint_units::USER_SPACE;
        return result;
    };
    path_stroke pen = shape.pen;
    pen.dashes = shape.pen.dashes;
    vector_shape_view result;
    result.path = {.commands = shape.commands,
                   .fill = paint_view(shape.fill),
                   .stroke = paint_view(shape.outline),
                   .stroke_options = pen,
                   .fill_rule = shape.rule,
                   .view_box = shape.bounds};
    result.transform = shape.transform;
    result.clips = shape.clips;
    result.fill_opacity = shape.fill_opacity;
    result.stroke_opacity = shape.stroke_opacity;
    result.stroke_first = shape.stroke_first;
    result.valid = true;
    return result;
}
} // namespace vector_detail
vector_scene_info inspect_vector(vector_handle image)
{
    if (!image) return {};
    const auto &source = image->source;
    return {
        source.view_box,          source.alignment,       source.shapes.size(), source.clips.size(),
        source.operations.size(), source.preserve_aspect, source.slice,         true};
}
vector_shape_view inspect_vector_shape(vector_handle image, usize index)
{
    return image && index < image->source.shapes.size()
               ? vector_detail::shape_view(image->source.shapes[index])
               : vector_shape_view{};
}
vector_clip_view inspect_vector_clip(vector_handle image, usize index)
{
    return image && index < image->source.clips.size()
               ? vector_clip_view{image->source.clips[index].shapes.size(), true}
               : vector_clip_view{};
}
vector_shape_view inspect_vector_clip_shape(vector_handle image, const vector_clip_shape_ref &ref)
{
    if (!image || ref.clip >= image->source.clips.size() ||
        ref.shape >= image->source.clips[ref.clip].shapes.size())
        return {};
    return vector_detail::shape_view(image->source.clips[ref.clip].shapes[ref.shape]);
}
vector_operation_view inspect_vector_operation(vector_handle image, usize index)
{
    if (!image || index >= image->source.operations.size()) return {};
    const auto &source = image->source.operations[index];
    vector_operation_view view = source;
    view.clips = source.clips;
    view.valid = true;
    return view;
}
vector_result create_path(const path_source &source)
try
{
    using namespace vector_detail;
    vector_error error;
    shape built;
    if (source.fill_rule > path_fill_rule::EVENODD ||
        !command_bounds(source.commands, built.bounds, error) ||
        !copy_paint(source.fill, built.fill, error) ||
        !copy_paint(source.stroke, built.outline, error) ||
        !copy_stroke(source.stroke_options, built.pen, error))
    {
        if (error.code == vector_error_code::NONE)
            fail(error, vector_error_code::INVALID_PATH, "unknown fill rule");
        return {nullptr, error};
    }
    rect box = source.view_box;
    if (box.w == 0 && box.h == 0)
    {
        box = built.bounds;
        if (built.outline.kind != vector_paint_kind::NONE && built.pen.width > 0)
            box = box.pad(
                -built.pen.width *
                std::max(built.pen.cap == path_line_cap::SQUARE ? 1.4142135623730951f : 1.0f,
                         built.pen.miter_limit) *
                .5f);
    }
    if (!std::isfinite(box.x) || !std::isfinite(box.y) || !std::isfinite(box.w) ||
        !std::isfinite(box.h) || box.w < 0 || box.h < 0)
        return {nullptr, {vector_error_code::INVALID_PATH, 0, "invalid vector view box"}};
    built.rule = source.fill_rule;
    built.commands.assign(source.commands.begin(), source.commands.end());
    auto *asset = new vector_resource();
    if (!asset->identity)
    {
        delete asset;
        return {nullptr, {vector_error_code::LIMIT, 0, "vector identity exhausted"}};
    }
    try
    {
        asset->source.view_box = box;
        asset->source.shapes.push_back(std::move(built));
        asset->source.operations.push_back({operation_kind::SHAPE, 0, 1});
    }
    catch (...)
    {
        delete asset;
        throw;
    }
    return {asset, {}};
}
catch (const std::bad_alloc &)
{
    return {nullptr, {vector_error_code::LIMIT, 0, "path source allocation"}};
}
