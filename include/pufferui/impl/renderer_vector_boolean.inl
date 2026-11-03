// Bounded preparation-time path normalization; included in vector_geometry.
// Curves are sampled at the declared quality before general winding operations.
// Disjoint trapezoids have unit winding, including at shared interior seams.
// This lets native Slug retain its unchanged nonzero coverage implementation.
enum class winding_mode : u8
{
    NONZERO,
    EVENODD,
    POSITIVE,
    NEGATIVE,
    INTERSECTION
};
inline bool winding_inside(i32 winding, winding_mode mode)
{
    switch (mode)
    {
    case winding_mode::EVENODD:
        return (winding & 1) != 0;
    case winding_mode::POSITIVE:
        return winding > 0;
    case winding_mode::NEGATIVE:
        return winding < 0;
    case winding_mode::INTERSECTION:
        return winding >= 2;
    default:
        return winding != 0;
    }
}
#include "renderer_vector_boundary.inl"
inline bool normalize_contours(const std::vector<contour> &contours, f64 tolerance,
                               winding_mode mode, std::vector<path_command> &out,
                               vector_error &error)
{
    constexpr usize work_limit = 64 * 1024 * 1024;
    if (!(tolerance > 0) || !std::isfinite(tolerance))
        return vector_detail::fail(error, vector_error_code::LIMIT, "vector boolean tolerance");
    vector_detail::bounds_builder bounds;
    for (const auto &path : contours)
        for (vec2 p : path.points) bounds.add(p.x, p.y);
    if (!bounds.any) return true;
    const f64 cx = (bounds.left + bounds.right) * .5, cy = (bounds.top + bounds.bottom) * .5;
    const f64 grid = std::exp2(std::floor(std::log2(tolerance / 64)));
    if (!(grid > 0) || !std::isfinite(grid))
        return vector_detail::fail(error, vector_error_code::LIMIT,
                                   "vector boolean grid precision");
    struct point
    {
        f64 x, y;
    };
    struct segment
    {
        point a, b;
    };
    std::vector<segment> edges;
    std::vector<f64> levels;
    for (const auto &path : contours)
    {
        if (path.points.size() < 2) continue;
        for (usize i = 0; i < path.points.size(); ++i)
        {
            const vec2 p = path.points[i], q = path.points[(i + 1) % path.points.size()];
            const point a{std::round((p.x - cx) / grid), std::round((p.y - cy) / grid)},
                b{std::round((q.x - cx) / grid), std::round((q.y - cy) / grid)};
            if (std::max({std::abs(a.x), std::abs(a.y), std::abs(b.x), std::abs(b.y)}) > 16777216)
                return vector_detail::fail(error, vector_error_code::LIMIT,
                                           "vector boolean coordinate precision");
            if (a.y == b.y) continue;
            if (edges.size() >= POINT_LIMIT)
                return vector_detail::fail(error, vector_error_code::LIMIT,
                                           "vector boolean edge budget");
            edges.push_back({a, b});
            levels.push_back(a.y);
            levels.push_back(b.y);
        }
    }
    usize work = 0;
    for (usize i = 0; i < edges.size(); ++i)
        for (usize j = i + 1; j < edges.size(); ++j)
        {
            if (++work > work_limit)
                return vector_detail::fail(error, vector_error_code::LIMIT,
                                           "vector boolean intersection budget");
            const auto &a = edges[i], &b = edges[j];
            if (std::max(a.a.x, a.b.x) < std::min(b.a.x, b.b.x) ||
                std::max(b.a.x, b.b.x) < std::min(a.a.x, a.b.x) ||
                std::max(a.a.y, a.b.y) < std::min(b.a.y, b.b.y) ||
                std::max(b.a.y, b.b.y) < std::min(a.a.y, a.b.y))
                continue;
            const f64 ax = a.b.x - a.a.x, ay = a.b.y - a.a.y, bx = b.b.x - b.a.x,
                      by = b.b.y - b.a.y;
            const f64 determinant = ax * by - ay * bx;
            if (determinant == 0) continue;
            const f64 dx = b.a.x - a.a.x, dy = b.a.y - a.a.y, t = (dx * by - dy * bx) / determinant,
                      u = (dx * ay - dy * ax) / determinant;
            if (t > 0 && t < 1 && u > 0 && u < 1)
            {
                if (levels.size() >= POINT_LIMIT * 2)
                    return vector_detail::fail(error, vector_error_code::LIMIT,
                                               "vector boolean level budget");
                levels.push_back(std::round(a.a.y + t * ay));
            }
        }
    std::sort(levels.begin(), levels.end());
    levels.erase(std::unique(levels.begin(), levels.end()), levels.end());
    struct crossing
    {
        f64 x;
        usize edge;
        i32 winding;
    };
    std::vector<crossing> hits;
    hits.reserve(edges.size());
    const auto x_at = [&](usize index, f64 y)
    {
        const auto &e = edges[index];
        return e.a.x + (y - e.a.y) * (e.b.x - e.a.x) / (e.b.y - e.a.y);
    };
    const auto point_at = [&](usize index, f64 y)
    {
        return vec2{static_cast<f32>(cx + std::round(x_at(index, y)) * grid),
                    static_cast<f32>(cy + y * grid)};
    };
    for (usize level = 1; level < levels.size(); ++level)
    {
        if (edges.size() > work_limit - work)
            return vector_detail::fail(error, vector_error_code::LIMIT,
                                       "vector boolean strip budget");
        work += edges.size();
        const f64 y0 = levels[level - 1], y1 = levels[level], mid = (y0 + y1) * .5;
        hits.clear();
        for (usize i = 0; i < edges.size(); ++i)
        {
            const auto &e = edges[i];
            if (mid < std::min(e.a.y, e.b.y) || mid >= std::max(e.a.y, e.b.y)) continue;
            // Sweep left to right: entering a positive-area contour crosses
            // its upward left edge. Canonical trapezoids use that orientation.
            hits.push_back({x_at(i, mid), i, e.b.y > e.a.y ? -1 : 1});
        }
        std::sort(hits.begin(), hits.end(),
                  [](const crossing &a, const crossing &b) { return a.x < b.x; });
        usize i = 0, left = 0;
        i32 winding = 0;
        while (i < hits.size())
        {
            const f64 x = hits[i].x;
            const bool before = winding_inside(winding, mode);
            const usize edge = hits[i].edge;
            do
            {
                winding += hits[i++].winding;
            } while (i < hits.size() && std::abs(hits[i].x - x) < 1e-8);
            const bool after = winding_inside(winding, mode);
            if (!before && after)
                left = edge;
            else if (before && !after)
            {
                vec2 p[4] = {point_at(left, y0), point_at(edge, y0), point_at(edge, y1),
                             point_at(left, y1)};
                if (p[0].x > p[1].x)
                {
                    if (p[0].x - p[1].x > grid * 2)
                        return vector_detail::fail(error, vector_error_code::LIMIT,
                                                   "vector boolean crossing precision");
                    p[0].x = p[1].x = static_cast<f32>((static_cast<f64>(p[0].x) + p[1].x) * .5);
                }
                if (p[3].x > p[2].x)
                {
                    if (p[3].x - p[2].x > grid * 2)
                        return vector_detail::fail(error, vector_error_code::LIMIT,
                                                   "vector boolean crossing precision");
                    p[3].x = p[2].x = static_cast<f32>((static_cast<f64>(p[3].x) + p[2].x) * .5);
                }
                if (p[0].y == p[3].y || (p[0].x == p[1].x && p[2].x == p[3].x)) continue;
                if (out.size() > POINT_LIMIT - 5)
                    return vector_detail::fail(error, vector_error_code::LIMIT,
                                               "vector boolean output budget");
                out.push_back({path_verb::MOVE, p[0]});
                for (usize j = 1; j < 4; ++j)
                    if (p[j].x != p[j - 1].x || p[j].y != p[j - 1].y)
                        out.push_back({path_verb::LINE, p[j]});
                out.push_back({path_verb::CLOSE});
            }
        }
    }
    return compact_boundary(out, tolerance, error);
}
inline bool normalize_path(std::span<const path_command> input, f64 tolerance, winding_mode mode,
                           std::vector<path_command> &out, vector_error &error)
{
    std::vector<contour> contours;
    return flatten(input, tolerance, contours, error) &&
           normalize_contours(contours, tolerance, mode, out, error);
}
