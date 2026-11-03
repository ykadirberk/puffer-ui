// Renderer-owned curve preparation shared by explicit portable/Slug painters.
// Included inside namespace pui, after normal mesh primitives.
// Preparation is bounded and occurs outside submission; no core tessellation.
// Curves remain available in the immutable source for native Slug fill encoding.
namespace renderer_detail::vector_geometry
{
constexpr usize POINT_LIMIT = 262144;
struct contour
{
    std::vector<vec2> points;
    bool closed = false, segments = false;
    vec2 tangent{1, 0};
};
inline f64 cross(vec2 a, vec2 b)
{
    return static_cast<f64>(a.x) * b.y - static_cast<f64>(a.y) * b.x;
}
inline vec2 subtract(vec2 a, vec2 b)
{
    return {a.x - b.x, a.y - b.y};
}
inline vec2 middle(vec2 a, vec2 b)
{
    return {static_cast<f32>((static_cast<f64>(a.x) + b.x) * .5),
            static_cast<f32>((static_cast<f64>(a.y) + b.y) * .5)};
}
inline f64 distance_squared(vec2 p, vec2 a, vec2 b)
{
    const f64 x = static_cast<f64>(b.x) - a.x, y = static_cast<f64>(b.y) - a.y;
    const f64 length = x * x + y * y;
    const f64 t =
        length > 0
            ? std::clamp(((static_cast<f64>(p.x) - a.x) * x + (static_cast<f64>(p.y) - a.y) * y) /
                             length,
                         0.0, 1.0)
            : 0;
    const f64 dx = p.x - (a.x + t * x), dy = p.y - (a.y + t * y);
    return dx * dx + dy * dy;
}
inline bool append_point(contour &out, vec2 point, usize &count, vector_error &error)
{
    if (!vector_detail::finite(point))
        return vector_detail::fail(error, vector_error_code::LIMIT,
                                   "vector prepared coordinate overflow");
    if (!out.points.empty() && out.points.back().x == point.x && out.points.back().y == point.y)
        return true;
    if (count >= POINT_LIMIT)
        return vector_detail::fail(error, vector_error_code::LIMIT, "vector prepared point budget");
    ++count;
    out.points.push_back(point);
    return true;
}
inline bool flatten_cubic(contour &out, vec2 from, vec2 p1, vec2 p2, vec2 to, f64 tolerance,
                          usize &count, vector_error &error)
{
    struct curve
    {
        vec2 a, b, c, d;
        u32 depth;
    };
    curve stack[32];
    usize size = 1;
    stack[0] = {from, p1, p2, to, 0};
    const f64 threshold = tolerance * tolerance;
    usize work = 0;
    while (size)
    {
        const curve v = stack[--size];
        if (++work > POINT_LIMIT * 4)
            return vector_detail::fail(error, vector_error_code::LIMIT,
                                       "vector curve subdivision budget");
        if (std::max(distance_squared(v.b, v.a, v.d), distance_squared(v.c, v.a, v.d)) <= threshold)
        {
            if (!append_point(out, v.d, count, error)) return false;
            continue;
        }
        if (v.depth >= 24)
            return vector_detail::fail(error, vector_error_code::LIMIT,
                                       "vector curve precision budget");
        const vec2 ab = middle(v.a, v.b), bc = middle(v.b, v.c), cd = middle(v.c, v.d),
                   abc = middle(ab, bc), bcd = middle(bc, cd), center = middle(abc, bcd);
        stack[size++] = {center, bcd, cd, v.d, v.depth + 1};
        stack[size++] = {v.a, ab, abc, center, v.depth + 1};
    }
    return true;
}
inline bool flatten(std::span<const path_command> commands, f64 tolerance,
                    std::vector<contour> &out, vector_error &error)
{
    if (!(tolerance > 0) || !std::isfinite(tolerance))
        return vector_detail::fail(error, vector_error_code::LIMIT,
                                   "invalid vector curve tolerance");
    usize count = 0;
    vec2 current{}, start{};
    bool open = false;
    for (const auto &command : commands)
    {
        if (command.verb == path_verb::MOVE)
        {
            if (out.size() >= POINT_LIMIT)
                return vector_detail::fail(error, vector_error_code::LIMIT,
                                           "vector contour budget");
            out.push_back({});
            current = start = command.p1;
            open = true;
            if (!append_point(out.back(), current, count, error)) return false;
            continue;
        }
        if (!open)
        {
            if (command.verb == path_verb::CLOSE) continue;
            out.push_back({});
            if (!append_point(out.back(), current, count, error)) return false;
            open = true;
            start = current;
        }
        contour &target = out.back();
        target.segments = true;
        switch (command.verb)
        {
        case path_verb::LINE:
            if (!append_point(target, command.p1, count, error)) return false;
            current = command.p1;
            break;
        case path_verb::QUADRATIC:
        {
            const auto control = [&](vec2 a)
            {
                return vec2{static_cast<f32>((static_cast<f64>(a.x) + 2.0 * command.p1.x) / 3),
                            static_cast<f32>((static_cast<f64>(a.y) + 2.0 * command.p1.y) / 3)};
            };
            if (!flatten_cubic(target, current, control(current), control(command.p2), command.p2,
                               tolerance, count, error))
                return false;
            current = command.p2;
            break;
        }
        case path_verb::CUBIC:
            if (!flatten_cubic(target, current, command.p1, command.p2, command.p3, tolerance,
                               count, error))
                return false;
            current = command.p3;
            break;
        case path_verb::CLOSE:
            target.closed = true;
            current = start;
            open = false;
            if (target.points.size() > 1 && target.points.back().x == start.x &&
                target.points.back().y == start.y)
                target.points.pop_back();
            break;
        default:
            return vector_detail::fail(error, vector_error_code::INVALID_PATH,
                                       "invalid prepared path verb");
        }
    }
    return true;
}
inline vec2 direction(vec2 a, vec2 b)
{
    const f64 dx = static_cast<f64>(b.x) - a.x, dy = static_cast<f64>(b.y) - a.y,
              length = std::hypot(dx, dy);
    return length > 0 ? vec2{static_cast<f32>(dx / length), static_cast<f32>(dy / length)}
                      : vec2{1, 0};
}
inline vec2 offset(vec2 point, vec2 direction, f64 radius)
{
    return {static_cast<f32>(point.x - direction.y * radius),
            static_cast<f32>(point.y + direction.x * radius)};
}
#include "renderer_vector_boolean.inl"
struct outline_builder
{
    std::vector<path_command> &out;
    vector_error &error;
    bool add(path_command c)
    {
        if (out.size() >= POINT_LIMIT || !vector_detail::finite(c.p1) ||
            !vector_detail::finite(c.p2) || !vector_detail::finite(c.p3))
            return vector_detail::fail(error, vector_error_code::LIMIT,
                                       "vector stroke preparation budget");
        out.push_back(c);
        return true;
    }
    bool line(vec2 p) { return add({path_verb::LINE, p}); }
    bool arc(vec2 center, vec2 from, vec2 to, f64 angle, f64 radius)
    {
        constexpr f64 pi = 3.14159265358979323846;
        const i32 count = std::max(1, static_cast<i32>(std::ceil(std::abs(angle) / (pi * .25))));
        const f64 start = std::atan2(static_cast<f64>(from.y) - center.y,
                                     static_cast<f64>(from.x) - center.x),
                  step = angle / count;
        const auto point = [&](f64 x, f64 y)
        {
            return vec2{static_cast<f32>(center.x + radius * x),
                        static_cast<f32>(center.y + radius * y)};
        };
        for (i32 i = 0; i < count; ++i)
        {
            const f64 a = start + i * step, b = a + step, k = 4.0 / 3 * std::tan(step / 4);
            if (!add({path_verb::CUBIC,
                      point(std::cos(a) - k * std::sin(a), std::sin(a) + k * std::cos(a)),
                      point(std::cos(b) + k * std::sin(b), std::sin(b) - k * std::cos(b)),
                      i + 1 == count ? to : point(std::cos(b), std::sin(b))}))
                return false;
        }
        return true;
    }
};
inline bool dash_contour(const contour &source, const vector_detail::stroke &pen,
                         std::vector<contour> &out, vector_error &error)
{
    if (source.points.empty() || !source.segments) return true;
    f64 total = 0;
    for (f32 d : pen.dashes) total += d;
    if (pen.dashes.empty() || total == 0)
    {
        out.push_back(source);
        return true;
    }
    const usize pattern_size = pen.dashes.size() * (pen.dashes.size() % 2 ? 2 : 1);
    if (pen.dashes.size() % 2) total *= 2;
    f64 phase = std::fmod(static_cast<f64>(pen.dash_offset), total);
    if (phase < 0) phase += total;
    usize index = 0;
    f64 remaining = pen.dashes[0];
    while (phase > 0 && phase >= remaining)
    {
        phase -= remaining;
        index = (index + 1) % pattern_size;
        remaining = pen.dashes[index % pen.dashes.size()];
    }
    remaining -= phase;
    if (source.points.size() == 1)
    {
        while (remaining == 0 && index % 2 != 0)
        {
            index = (index + 1) % pattern_size;
            remaining = pen.dashes[index % pen.dashes.size()];
        }
        if (index % 2 == 0) out.push_back(source);
        return true;
    }
    const usize out_start = out.size();
    contour run;
    usize points = 0, work = 0;
    const auto flush = [&]()
    {
        if (!run.points.empty())
        {
            run.segments = true;
            out.push_back(std::move(run));
            run = {};
        }
    };
    const auto zeros = [&](vec2 point, vec2 tangent) -> bool
    {
        for (usize skipped = 0; remaining == 0; ++skipped)
        {
            if (++work > POINT_LIMIT * 4 || skipped > pattern_size)
                return vector_detail::fail(error, vector_error_code::LIMIT,
                                           "vector dash expansion budget");
            if (index % 2 == 0 && pen.dashes[index % pen.dashes.size()] == 0 &&
                pen.cap != path_line_cap::BUTT)
            {
                contour dot;
                dot.segments = true;
                dot.tangent = tangent;
                if (!append_point(dot, point, points, error)) return false;
                out.push_back(std::move(dot));
            }
            index = (index + 1) % pattern_size;
            remaining = pen.dashes[index % pen.dashes.size()];
        }
        return true;
    };
    const usize edges = source.closed ? source.points.size() : source.points.size() - 1;
    vec2 last_tangent{1, 0}, end = source.points[0];
    for (usize edge = 0; edge < edges; ++edge)
    {
        const vec2 a = source.points[edge], b = source.points[(edge + 1) % source.points.size()];
        const f64 dx = static_cast<f64>(b.x) - a.x, dy = static_cast<f64>(b.y) - a.y,
                  length = std::hypot(dx, dy);
        if (length == 0) continue;
        const vec2 tangent{static_cast<f32>(dx / length), static_cast<f32>(dy / length)};
        last_tangent = tangent;
        f64 at = 0;
        const auto point = [&](f64 position)
        {
            return position == length ? b
                                      : vec2{static_cast<f32>(a.x + dx * (position / length)),
                                             static_cast<f32>(a.y + dy * (position / length))};
        };
        while (at < length)
        {
            if (++work > POINT_LIMIT * 4)
                return vector_detail::fail(error, vector_error_code::LIMIT,
                                           "vector dash expansion budget");
            if (!zeros(point(at), tangent)) return false;
            const f64 step = std::min(remaining, length - at);
            if (at + step == at)
                return vector_detail::fail(error, vector_error_code::LIMIT,
                                           "vector dash precision budget");
            if (index % 2 == 0)
            {
                if (run.points.empty()) run.tangent = tangent;
                if (!append_point(run, point(at), points, error) ||
                    !append_point(run, point(at + step), points, error))
                    return false;
            }
            else
                flush();
            at += step;
            remaining = step == remaining ? 0 : remaining - step;
        }
        end = b;
    }
    if (!zeros(end, last_tangent)) return false;
    flush();
    if (source.closed && out.size() == out_start + 1)
    {
        contour &only = out[out_start];
        if (only.points.size() > 1 && only.points.front().x == only.points.back().x &&
            only.points.front().y == only.points.back().y)
        {
            only.closed = true;
            only.points.pop_back();
        }
    }
    // A closed dash continuing through the origin is one joined subpath.
    if (source.closed && out.size() > out_start + 1)
    {
        contour &first = out[out_start], &last = out.back();
        if (!first.points.empty() && !last.points.empty() &&
            first.points.front().x == source.points.front().x &&
            first.points.front().y == source.points.front().y &&
            last.points.back().x == source.points.front().x &&
            last.points.back().y == source.points.front().y)
        {
            contour merged = std::move(last);
            out.pop_back();
            merged.points.insert(merged.points.end(), first.points.begin() + 1, first.points.end());
            first = std::move(merged);
        }
    }
    return true;
}
inline bool stroke_primitives(const contour &source, const vector_detail::stroke &pen,
                              std::vector<path_command> &out, vector_error &error)
{
    if (source.points.empty() || !source.segments || pen.width <= 0) return true;
    outline_builder builder{out, error};
    const f64 r = pen.width * .5;
    const auto polygon = [&](std::span<const vec2> points) -> bool
    {
        f64 area = 0;
        for (usize i = 0; i < points.size(); ++i)
            area += cross(points[i], points[(i + 1) % points.size()]);
        if (area == 0) return true;
        if (!builder.add({path_verb::MOVE, points[0]})) return false;
        if (area > 0)
        {
            for (usize i = 1; i < points.size(); ++i)
                if (!builder.line(points[i])) return false;
        }
        else
        {
            for (usize i = points.size(); i > 1; --i)
                if (!builder.line(points[i - 1])) return false;
        }
        return builder.add({path_verb::CLOSE});
    };
    const auto rectangle = [&](vec2 a, vec2 b) -> bool
    {
        const vec2 d = direction(a, b);
        const vec2 points[4] = {offset(a, d, r), offset(a, d, -r), offset(b, d, -r),
                                offset(b, d, r)};
        return polygon(points);
    };
    const auto sector = [&](vec2 center, vec2 a, vec2 b, f64 angle) -> bool
    {
        if (angle < 0)
        {
            std::swap(a, b);
            angle = -angle;
        }
        return builder.add({path_verb::MOVE, center}) && builder.line(a) &&
               builder.arc(center, a, b, angle, r) && builder.add({path_verb::CLOSE});
    };
    const auto cap = [&](vec2 point, vec2 d, bool beginning) -> bool
    {
        if (pen.cap == path_line_cap::BUTT) return true;
        if (pen.cap == path_line_cap::ROUND)
        {
            const vec2 left = offset(point, d, r), right = offset(point, d, -r);
            return sector(point, beginning ? left : right, beginning ? right : left,
                          3.14159265358979323846);
        }
        const f64 amount = beginning ? -r : r;
        const vec2 end{static_cast<f32>(point.x + d.x * amount),
                       static_cast<f32>(point.y + d.y * amount)};
        return rectangle(point, end);
    };
    if (source.points.size() == 1)
    {
        if (pen.cap == path_line_cap::BUTT) return true;
        const vec2 p = source.points[0], d = source.tangent;
        if (pen.cap == path_line_cap::ROUND) return cap(p, d, true) && cap(p, d, false);
        const vec2 a{static_cast<f32>(p.x - r * d.x), static_cast<f32>(p.y - r * d.y)},
            b{static_cast<f32>(p.x + r * d.x), static_cast<f32>(p.y + r * d.y)};
        return rectangle(a, b);
    }
    const auto &points = source.points;
    const usize n = points.size(), edge_count = source.closed ? n : n - 1;
    for (usize i = 0; i < edge_count; ++i)
        if (!rectangle(points[i], points[(i + 1) % n])) return false;
    const usize first = source.closed ? 0 : 1, end = source.closed ? n : n - 1;
    for (usize i = first; i < end; ++i)
    {
        const vec2 p = points[i], a = direction(points[(i + n - 1) % n], p),
                   b = direction(p, points[(i + 1) % n]);
        const f64 turn = cross(a, b),
                  dot = static_cast<f64>(a.x) * b.x + static_cast<f64>(a.y) * b.y;
        if (std::abs(turn) < 1e-12)
        {
            if (dot < 0 && pen.join == path_line_join::ROUND &&
                !sector(p, offset(p, a, r), offset(p, a, -r), -3.14159265358979323846))
                return false;
            continue;
        }
        const f64 side = turn > 0 ? -1.0 : 1.0;
        const vec2 left = offset(p, a, r * side), right = offset(p, b, r * side);
        if (pen.join == path_line_join::ROUND)
        {
            if (!sector(p, left, right, std::atan2(turn, dot))) return false;
            continue;
        }
        const f64 t = ((static_cast<f64>(right.x) - left.x) * b.y -
                       (static_cast<f64>(right.y) - left.y) * b.x) /
                      turn;
        const f64 mx = left.x + t * a.x, my = left.y + t * a.y;
        if (pen.join == path_line_join::MITER &&
            std::hypot(mx - p.x, my - p.y) <= r * pen.miter_limit)
        {
            const vec2 corners[4] = {p, left, {static_cast<f32>(mx), static_cast<f32>(my)}, right};
            if (!polygon(corners)) return false;
        }
        else
        {
            const vec2 corners[3] = {p, left, right};
            if (!polygon(corners)) return false;
        }
    }
    return source.closed || (cap(points.front(), direction(points[0], points[1]), true) &&
                             cap(points.back(), direction(points[n - 2], points.back()), false));
}
inline bool stroke_contour(const contour &source, const vector_detail::stroke &pen,
                           std::vector<path_command> &out, vector_error &error,
                           f64 tolerance = .00025)
{
    std::vector<path_command> primitives;
    return stroke_primitives(source, pen, primitives, error) &&
           normalize_path(primitives, tolerance, winding_mode::NONZERO, out, error);
}
inline bool stroke_path(const std::vector<contour> &contours, const vector_detail::stroke &pen,
                        std::vector<path_command> &out, vector_error &error, f64 tolerance = .00025)
{
    if (pen.width <= 0) return true;
    std::vector<path_command> primitives;
    for (const auto &path : contours)
    {
        if (pen.dashes.empty())
        {
            if (!stroke_primitives(path, pen, primitives, error)) return false;
        }
        else
        {
            std::vector<contour> dashed;
            if (!dash_contour(path, pen, dashed, error)) return false;
            for (const auto &run : dashed)
                if (!stroke_primitives(run, pen, primitives, error)) return false;
        }
    }
    return normalize_path(primitives, tolerance, winding_mode::NONZERO, out, error);
}
} // namespace renderer_detail::vector_geometry
