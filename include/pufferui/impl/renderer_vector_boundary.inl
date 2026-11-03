// Recover boundary contours from canonical, disjoint scanline trapezoids.
// Shared interior edges cancel before Slug encoding; holes keep their direction.
// This bounds encoded curve/band data by the actual boundary, not filled strips.
// Included inside renderer_detail::vector_geometry before boolean normalization.
inline bool compact_boundary(std::vector<path_command> &path, f64 tolerance, vector_error &error)
{
    if (path.empty()) return true;
    struct event
    {
        f32 y, x;
        i32 delta;
    };
    struct edge
    {
        u32 from, to, next = UINT32_MAX;
        bool removed = false, used = false;
    };
    std::vector<event> horizontal;
    std::vector<vec2> vertices;
    std::vector<edge> edges;
    std::unordered_map<u64, u32> points, segments;
    const auto vertex = [&](vec2 point) -> u32
    {
        if (point.x == 0) point.x = 0;
        if (point.y == 0) point.y = 0;
        u32 x, y;
        std::memcpy(&x, &point.x, 4);
        std::memcpy(&y, &point.y, 4);
        const u64 key = (static_cast<u64>(x) << 32) | y;
        const auto found = points.find(key);
        if (found != points.end()) return found->second;
        const u32 result = static_cast<u32>(vertices.size());
        vertices.push_back(point);
        points.emplace(key, result);
        return result;
    };
    const auto add = [&](vec2 a, vec2 b) -> bool
    {
        if (a.x == b.x && a.y == b.y) return true;
        if (edges.size() >= POINT_LIMIT)
            return vector_detail::fail(error, vector_error_code::LIMIT,
                                       "vector boundary edge budget");
        const u32 from = vertex(a), to = vertex(b);
        const u64 key = (static_cast<u64>(from) << 32) | to,
                  reverse = (static_cast<u64>(to) << 32) | from;
        const auto opposite = segments.find(reverse);
        if (opposite != segments.end())
        {
            edges[opposite->second].removed = true;
            segments.erase(opposite);
            return true;
        }
        if (segments.find(key) != segments.end())
            return vector_detail::fail(error, vector_error_code::LIMIT,
                                       "non-manifold canonical vector boundary");
        segments.emplace(key, static_cast<u32>(edges.size()));
        edges.push_back({from, to});
        return true;
    };
    vec2 current{}, start{};
    bool open = false;
    const auto segment = [&](vec2 a, vec2 b) -> bool
    {
        if (a.y != b.y) return add(a, b);
        if (a.x == b.x) return true;
        const i32 direction = a.x < b.x ? 1 : -1;
        horizontal.push_back({a.y, std::min(a.x, b.x), direction});
        horizontal.push_back({a.y, std::max(a.x, b.x), -direction});
        return true;
    };
    for (const auto &command : path)
    {
        if (command.verb == path_verb::MOVE)
        {
            if (open && !segment(current, start)) return false;
            current = start = command.p1;
            open = true;
        }
        else if (command.verb == path_verb::LINE)
        {
            if (!segment(current, command.p1)) return false;
            current = command.p1;
        }
        else if (command.verb == path_verb::CLOSE)
        {
            if (open && !segment(current, start)) return false;
            open = false;
        }
        else
            return vector_detail::fail(error, vector_error_code::INVALID_PATH,
                                       "canonical boundary must be linear");
    }
    if (open && !segment(current, start)) return false;
    std::sort(horizontal.begin(), horizontal.end(), [](const event &a, const event &b)
              { return a.y < b.y || (a.y == b.y && a.x < b.x); });
    for (usize at = 0; at < horizontal.size();)
    {
        const f32 y = horizontal[at].y;
        f32 previous = horizontal[at].x;
        i32 winding = 0;
        while (at < horizontal.size() && horizontal[at].y == y)
        {
            const f32 x = horizontal[at].x;
            if (x > previous && winding)
                if (!add(winding > 0 ? vec2{previous, y} : vec2{x, y},
                         winding > 0 ? vec2{x, y} : vec2{previous, y}))
                    return false;
            do
            {
                winding += horizontal[at++].delta;
            } while (at < horizontal.size() && horizontal[at].y == y && horizontal[at].x == x);
            previous = x;
        }
        if (winding)
            return vector_detail::fail(error, vector_error_code::INVALID_PATH,
                                       "unbalanced canonical horizontal boundary");
    }
    std::vector<u32> first(vertices.size(), UINT32_MAX);
    for (u32 i = 0; i < edges.size(); ++i)
        if (!edges[i].removed)
        {
            edges[i].next = first[edges[i].from];
            first[edges[i].from] = i;
        }
    const auto redundant = [&](vec2 a, vec2 p, vec2 b)
    {
        const f64 x = static_cast<f64>(b.x) - a.x, y = static_cast<f64>(b.y) - a.y,
                  px = static_cast<f64>(p.x) - a.x, py = static_cast<f64>(p.y) - a.y;
        const f64 cross = px * y - py * x, limit = tolerance / 16;
        return px * (b.x - p.x) + py * (b.y - p.y) >= 0 &&
               cross * cross <= limit * limit * (x * x + y * y);
    };
    std::vector<path_command> result;
    std::vector<vec2> contour;
    usize work = 0;
    for (u32 initial = 0; initial < edges.size(); ++initial)
    {
        if (edges[initial].removed || edges[initial].used) continue;
        contour.clear();
        u32 at = initial;
        const u32 start_vertex = edges[at].from;
        for (;;)
        {
            if (++work > 64 * 1024 * 1024)
                return vector_detail::fail(error, vector_error_code::LIMIT,
                                           "vector boundary traversal budget");
            auto &edge = edges[at];
            if (edge.used)
                return vector_detail::fail(error, vector_error_code::INVALID_PATH,
                                           "non-closing vector boundary");
            edge.used = true;
            const vec2 point = vertices[edge.from];
            while (contour.size() > 1 &&
                   redundant(contour[contour.size() - 2], contour.back(), point))
                contour.pop_back();
            contour.push_back(point);
            if (edge.to == start_vertex) break;
            const vec2 a = vertices[edge.from], b = vertices[edge.to];
            const f64 dx = static_cast<f64>(b.x) - a.x, dy = static_cast<f64>(b.y) - a.y;
            u32 next = UINT32_MAX;
            f64 best = -4;
            for (u32 candidate = first[edge.to]; candidate != UINT32_MAX;
                 candidate = edges[candidate].next)
            {
                if (++work > 64 * 1024 * 1024)
                    return vector_detail::fail(error, vector_error_code::LIMIT,
                                               "vector boundary junction budget");
                if (edges[candidate].removed || edges[candidate].used) continue;
                const auto c = vertices[edges[candidate].to];
                const f64 x = static_cast<f64>(c.x) - b.x, y = static_cast<f64>(c.y) - b.y;
                const f64 turn = std::atan2(dx * y - dy * x, dx * x + dy * y);
                if (turn > best)
                {
                    best = turn;
                    next = candidate;
                }
            }
            if (next == UINT32_MAX)
                return vector_detail::fail(error, vector_error_code::INVALID_PATH,
                                           "open canonical vector boundary");
            at = next;
        }
        usize begin = 0;
        while (contour.size() - begin > 3)
        {
            if (redundant(contour[contour.size() - 2], contour.back(), contour[begin]))
                contour.pop_back();
            else if (redundant(contour.back(), contour[begin], contour[begin + 1]))
                ++begin;
            else
                break;
        }
        if (contour.size() - begin < 3) continue;
        if (contour.size() - begin + 1 > POINT_LIMIT - result.size())
            return vector_detail::fail(error, vector_error_code::LIMIT,
                                       "vector boundary command budget");
        usize origin = begin;
        for (usize i = begin + 1; i < contour.size(); ++i)
            if (contour[i].y < contour[origin].y ||
                (contour[i].y == contour[origin].y && contour[i].x < contour[origin].x))
                origin = i;
        result.push_back({path_verb::MOVE, contour[origin]});
        const usize count = contour.size() - begin;
        for (usize i = 1; i < count; ++i)
            result.push_back({path_verb::LINE, contour[begin + (origin - begin + i) % count]});
        result.push_back({path_verb::CLOSE});
    }
    path.swap(result);
    return true;
}
