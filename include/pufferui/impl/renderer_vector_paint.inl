// Vector paint sampling for renderer resource preparation and reference tests.
// Included inside namespace pui; independent of font/glyph gamma and native SDK.
// SVG interpolates straight sRGB/alpha; compositing uses premultiplied values.
// Backend shaders implement the same coordinates, stops and spread contract.
namespace renderer_detail::vector_raster
{
struct rgba
{
    f32 r = 0, g = 0, b = 0, a = 0;
};
inline rgba color_value(color c, f32 opacity = 1)
{
    return {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f * opacity};
}
inline rgba stop_value(const gradient_stop &stop, color current)
{
    return color_value(stop.current_color ? current : stop.tint, stop.opacity);
}
struct paint_sampler
{
    const vector_detail::paint *source = nullptr;
    affine2 inverse{};
    bool valid = true;
};
inline paint_sampler prepare_paint(const vector_detail::paint &paint, rect bounds, affine2 pixels)
{
    paint_sampler result{&paint};
    if (paint.kind == vector_paint_kind::NONE || paint.kind == vector_paint_kind::SOLID)
        return result;
    if (paint.object_bounds)
    {
        if (bounds.w <= 0 || bounds.h <= 0)
        {
            result.valid = false;
            return result;
        }
        pixels = vector_detail::multiply(pixels, {bounds.w, 0, 0, bounds.h, bounds.x, bounds.y});
    }
    result.valid =
        vector_detail::inverse(vector_detail::multiply(pixels, paint.transform), result.inverse);
    return result;
}
inline rgba sample_paint(const paint_sampler &sampler, vec2 point, color current)
{
    const auto &paint = *sampler.source;
    if (!sampler.valid || paint.kind == vector_paint_kind::NONE) return {};
    if (paint.kind == vector_paint_kind::SOLID)
        return color_value(paint.current_color ? current : paint.tint);
    if (paint.stops.empty()) return {};
    point = sampler.inverse.apply(point);
    f64 t = 1;
    if (paint.kind == vector_paint_kind::LINEAR)
    {
        const f64 x = static_cast<f64>(paint.end.x) - paint.start.x,
                  y = static_cast<f64>(paint.end.y) - paint.start.y, length = x * x + y * y;
        if (length > 0)
            t = ((point.x - paint.start.x) * x + (point.y - paint.start.y) * y) / length;
        else
            return stop_value(paint.stops.back(), current);
    }
    else if (paint.radius > 0)
    {
        const f64 x = static_cast<f64>(point.x) - paint.focal.x,
                  y = static_cast<f64>(point.y) - paint.focal.y;
        const f64 dx = static_cast<f64>(paint.center.x) - paint.focal.x,
                  dy = static_cast<f64>(paint.center.y) - paint.focal.y;
        const f64 a = dx * dx + dy * dy - static_cast<f64>(paint.radius) * paint.radius,
                  b = -2 * (x * dx + y * dy), c = x * x + y * y;
        if (c == 0)
            t = 0;
        else if (std::abs(a) < 1e-20)
        {
            if (b >= 0) return {};
            t = -c / b;
        }
        else
        {
            const f64 discriminant = b * b - 4 * a * c;
            if (discriminant < 0) return {};
            const f64 root = std::sqrt(discriminant), first = (-b - root) / (2 * a),
                      second = (-b + root) / (2 * a);
            t = std::max(first, second);
            if (t < 0) return {};
        }
    }
    else
        return stop_value(paint.stops.back(), current);
    if (!std::isfinite(t)) return {};
    if (paint.spread == vector_spread::REPEAT)
        t -= std::floor(t);
    else if (paint.spread == vector_spread::REFLECT)
    {
        t = std::fmod(t, 2);
        if (t < 0) t += 2;
        if (t > 1) t = 2 - t;
    }
    else
        t = std::clamp(t, 0.0, 1.0);
    const auto upper = std::upper_bound(paint.stops.begin(), paint.stops.end(), t,
                                        [](f64 position, const gradient_stop &stop)
                                        { return position < stop.offset; });
    if (upper == paint.stops.begin()) return stop_value(*upper, current);
    if (upper == paint.stops.end()) return stop_value(paint.stops.back(), current);
    const auto &a = upper[-1], &b = *upper;
    const rgba first = stop_value(a, current), second = stop_value(b, current);
    const f32 weight = static_cast<f32>((t - a.offset) / (b.offset - a.offset));
    return {first.r + (second.r - first.r) * weight, first.g + (second.g - first.g) * weight,
            first.b + (second.b - first.b) * weight, first.a + (second.a - first.a) * weight};
}
inline void over(rgba &destination, rgba source)
{
    const f32 remaining = 1 - source.a;
    destination = {source.r + destination.r * remaining, source.g + destination.g * remaining,
                   source.b + destination.b * remaining, source.a + destination.a * remaining};
}
} // namespace renderer_detail::vector_raster
