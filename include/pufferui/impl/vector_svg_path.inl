// SVG numeric/path/affine parsing. No locale, file I/O or external resources.
// Included inside pui::vector_detail by vector_svg.inl.
// Arc endpoint conversion follows SVG 2 implementation notes.
// Work and generated command counts are bounded by the importing document.
inline bool svg_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}
inline bool svg_alpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
inline std::string_view trim(std::string_view s)
{
    while (!s.empty() && svg_space(s.front())) s.remove_prefix(1);
    while (!s.empty() && svg_space(s.back())) s.remove_suffix(1);
    return s;
}
struct svg_numbers
{
    std::string_view input;
    usize at = 0, origin = 0;
    vector_error &error;
    bool previous = false;
    void whitespace()
    {
        while (at < input.size() && svg_space(input[at])) ++at;
    }
    bool available() const
    {
        usize p = at;
        while (p < input.size() && (svg_space(input[p]) || input[p] == ',')) ++p;
        return p < input.size() && (input[p] == '+' || input[p] == '-' || input[p] == '.' ||
                                    (input[p] >= '0' && input[p] <= '9'));
    }
    bool separator()
    {
        whitespace();
        if (at < input.size() && input[at] == ',')
        {
            if (!previous)
                return fail(error, vector_error_code::INVALID_SVG, "unexpected comma", origin + at);
            ++at;
            whitespace();
        }
        return true;
    }
    bool number(f64 &out)
    {
        if (!separator()) return false;
        const usize start = at;
        if (at < input.size() && input[at] == '+')
        {
            ++at;
            if (at < input.size() && (input[at] == '+' || input[at] == '-'))
                return fail(error, vector_error_code::INVALID_SVG, "invalid SVG number sign",
                            origin + start);
        }
        const char *first = input.data() + at, *last = input.data() + input.size();
        const auto result = std::from_chars(first, last, out, std::chars_format::general);
        if (result.ec != std::errc{} || result.ptr == first || !std::isfinite(out))
            return fail(error, vector_error_code::INVALID_SVG, "invalid SVG number",
                        origin + start);
        at = static_cast<usize>(result.ptr - input.data());
        previous = true;
        return true;
    }
    bool flag(bool &out)
    {
        if (!separator()) return false;
        if (at >= input.size() || (input[at] != '0' && input[at] != '1'))
            return fail(error, vector_error_code::INVALID_SVG, "arc flag must be 0 or 1",
                        origin + at);
        out = input[at++] == '1';
        previous = true;
        return true;
    }
};
inline bool svg_command(std::vector<path_command> &out, path_command value, usize limit,
                        vector_error &error, usize at)
{
    if (out.size() >= limit) return fail(error, vector_error_code::LIMIT, "SVG command limit", at);
    if (value.verb != path_verb::CLOSE &&
        (!finite(value.p1) || !finite(value.p2) || !finite(value.p3)))
        return fail(error, vector_error_code::INVALID_SVG, "SVG coordinate overflow", at);
    out.push_back(value);
    return true;
}
inline bool svg_arc(std::vector<path_command> &out, vec2 from, vec2 to, f64 rx, f64 ry, f64 angle,
                    bool large, bool sweep, usize limit, vector_error &error, usize at)
{
    rx = std::abs(rx);
    ry = std::abs(ry);
    if (from.x == to.x && from.y == to.y) return true;
    if (rx == 0 || ry == 0) return svg_command(out, {path_verb::LINE, to}, limit, error, at);
    constexpr f64 pi = 3.1415926535897932384626433832795;
    const f64 phi = std::remainder(angle, 360.0) * pi / 180, cs = std::cos(phi), sn = std::sin(phi);
    const f64 dx = (static_cast<f64>(from.x) - to.x) * .5,
              dy = (static_cast<f64>(from.y) - to.y) * .5;
    const f64 xp = cs * dx + sn * dy, yp = -sn * dx + cs * dy;
    const f64 radius = std::hypot(xp / rx, yp / ry);
    if (!std::isfinite(radius))
        return fail(error, vector_error_code::LIMIT, "SVG arc radius precision", at);
    if (radius > 1)
    {
        rx *= radius;
        ry *= radius;
    }
    const f64 nx = xp / rx, ny = yp / ry, denominator = nx * nx + ny * ny;
    if (!(denominator > 0) || !std::isfinite(rx) || !std::isfinite(ry))
        return fail(error, vector_error_code::LIMIT, "SVG arc center precision", at);
    const f64 factor =
        (large == sweep ? -1.0 : 1.0) * std::sqrt(std::max(0.0, (1 - denominator) / denominator));
    const f64 cxp = factor * rx * ny, cyp = -factor * ry * nx;
    const f64 cx = cs * cxp - sn * cyp + (static_cast<f64>(from.x) + to.x) * .5;
    const f64 cy = sn * cxp + cs * cyp + (static_cast<f64>(from.y) + to.y) * .5;
    const f64 ux = (xp - cxp) / rx, uy = (yp - cyp) / ry, vx = (-xp - cxp) / rx,
              vy = (-yp - cyp) / ry;
    const f64 start = std::atan2(uy, ux);
    f64 delta = std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
    if (!sweep && delta > 0) delta -= 2 * pi;
    if (sweep && delta < 0) delta += 2 * pi;
    if (!std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(delta))
        return fail(error, vector_error_code::INVALID_SVG, "SVG arc overflow", at);
    const i32 segments = std::max(1, static_cast<i32>(std::ceil(std::abs(delta) / (pi * .25))));
    if (segments > 8)
        return fail(error, vector_error_code::INVALID_SVG, "SVG arc sweep overflow", at);
    const f64 step = delta / segments;
    const auto point = [&](f64 x, f64 y)
    {
        return vec2{static_cast<f32>(cx + cs * rx * x - sn * ry * y),
                    static_cast<f32>(cy + sn * rx * x + cs * ry * y)};
    };
    for (i32 i = 0; i < segments; ++i)
    {
        const f64 a = start + i * step, b = a + step, k = 4.0 / 3 * std::tan(step / 4);
        const vec2 p1 = point(std::cos(a) - k * std::sin(a), std::sin(a) + k * std::cos(a));
        const vec2 p2 = point(std::cos(b) + k * std::sin(b), std::sin(b) - k * std::cos(b));
        const vec2 p3 = i + 1 == segments ? to : point(std::cos(b), std::sin(b));
        if (!svg_command(out, {path_verb::CUBIC, p1, p2, p3}, limit, error, at)) return false;
    }
    return true;
}
inline bool svg_path(std::string_view input, std::vector<path_command> &out, usize limit,
                     vector_error &error, usize origin = 0)
{
    svg_numbers n{input, 0, origin, error};
    char command = 0, previous = 0;
    vec2 current{}, start{}, control{};
    bool first = true;
    while (true)
    {
        n.whitespace();
        if (n.at == input.size()) return true;
        if (svg_alpha(input[n.at]))
        {
            command = input[n.at++];
            n.previous = false;
        }
        else if (!command)
            return fail(error, vector_error_code::INVALID_SVG, "expected path command",
                        origin + n.at);
        const bool relative = command >= 'a' && command <= 'z';
        const char verb = relative ? static_cast<char>(command - 'a' + 'A') : command;
        if (first && verb != 'M')
            return fail(error, vector_error_code::INVALID_SVG, "SVG path must start with move",
                        origin + n.at);
        if (verb == 'Z')
        {
            if (!svg_command(out, {path_verb::CLOSE}, limit, error, origin + n.at)) return false;
            current = start;
            previous = verb;
            command = 0;
            continue;
        }
        f64 v[6]{};
        bool large = false, sweep = false;
        const i32 count = verb == 'H' || verb == 'V'
                              ? 1
                              : (verb == 'M' || verb == 'L' || verb == 'T'
                                     ? 2
                                     : (verb == 'Q' || verb == 'S' ? 4 : (verb == 'C' ? 6 : 0)));
        if (verb == 'A')
        {
            if (!n.number(v[0]) || !n.number(v[1]) || !n.number(v[2]) || !n.flag(large) ||
                !n.flag(sweep) || !n.number(v[3]) || !n.number(v[4]))
                return false;
        }
        else
        {
            if (!count)
                return fail(error, vector_error_code::UNSUPPORTED, "unsupported path command",
                            origin + n.at - 1);
            for (i32 i = 0; i < count; ++i)
                if (!n.number(v[i])) return false;
        }
        const auto position = [&](f64 x, f64 y)
        {
            return vec2{static_cast<f32>(x + (relative ? current.x : 0)),
                        static_cast<f32>(y + (relative ? current.y : 0))};
        };
        path_command next;
        switch (verb)
        {
        case 'M':
            next = {path_verb::MOVE, position(v[0], v[1])};
            start = next.p1;
            first = false;
            break;
        case 'L':
            next = {path_verb::LINE, position(v[0], v[1])};
            break;
        case 'H':
            next = {path_verb::LINE,
                    {static_cast<f32>(v[0] + (relative ? current.x : 0)), current.y}};
            break;
        case 'V':
            next = {path_verb::LINE,
                    {current.x, static_cast<f32>(v[0] + (relative ? current.y : 0))}};
            break;
        case 'C':
            next = {path_verb::CUBIC, position(v[0], v[1]), position(v[2], v[3]),
                    position(v[4], v[5])};
            break;
        case 'S':
            next = {path_verb::CUBIC,
                    (previous == 'C' || previous == 'S')
                        ? vec2{2 * current.x - control.x, 2 * current.y - control.y}
                        : current,
                    position(v[0], v[1]), position(v[2], v[3])};
            break;
        case 'Q':
            next = {path_verb::QUADRATIC, position(v[0], v[1]), position(v[2], v[3])};
            break;
        case 'T':
            next = {path_verb::QUADRATIC,
                    (previous == 'Q' || previous == 'T')
                        ? vec2{2 * current.x - control.x, 2 * current.y - control.y}
                        : current,
                    position(v[0], v[1])};
            break;
        case 'A':
        {
            const vec2 to = position(v[3], v[4]);
            if (!finite(to))
                return fail(error, vector_error_code::INVALID_SVG, "SVG coordinate overflow",
                            origin + n.at);
            if (!svg_arc(out, current, to, v[0], v[1], v[2], large, sweep, limit, error,
                         origin + n.at))
                return false;
            current = to;
            previous = verb;
            continue;
        }
        default:
            return false;
        }
        if (!svg_command(out, next, limit, error, origin + n.at)) return false;
        if (verb == 'C' || verb == 'S')
        {
            current = next.p3;
            control = next.p2;
        }
        else if (verb == 'Q' || verb == 'T')
        {
            current = next.p2;
            control = next.p1;
        }
        else
            current = next.p1;
        previous = verb;
        if (verb == 'M') command = relative ? 'l' : 'L';
    }
}
inline bool svg_transform(std::string_view input, affine2 &result, vector_error &error,
                          usize origin = 0)
{
    svg_numbers n{input, 0, origin, error};
    result = {};
    bool first = true;
    while (true)
    {
        n.whitespace();
        if (n.at == input.size()) return true;
        if (input[n.at] == ',')
        {
            if (first)
                return fail(error, vector_error_code::INVALID_SVG, "leading transform comma",
                            origin + n.at);
            ++n.at;
            n.whitespace();
            if (n.at == input.size())
                return fail(error, vector_error_code::INVALID_SVG, "trailing transform comma",
                            origin + n.at);
        }
        first = false;
        const usize begin = n.at;
        while (n.at < input.size() && svg_alpha(input[n.at])) ++n.at;
        const auto name = input.substr(begin, n.at - begin);
        n.whitespace();
        if (n.at == input.size() || input[n.at++] != '(')
            return fail(error, vector_error_code::INVALID_SVG, "expected transform arguments",
                        origin + n.at);
        f64 v[6]{};
        i32 count = 0;
        n.previous = false;
        while (n.available() && count < 6)
            if (!n.number(v[count++])) return false;
        n.whitespace();
        if (n.at == input.size() || input[n.at++] != ')')
            return fail(error, vector_error_code::INVALID_SVG, "invalid transform arguments",
                        origin + n.at);
        affine2 m;
        constexpr f64 radians = 3.1415926535897932384626433832795 / 180;
        if (name == "matrix" && count == 6)
            m = {static_cast<f32>(v[0]), static_cast<f32>(v[1]), static_cast<f32>(v[2]),
                 static_cast<f32>(v[3]), static_cast<f32>(v[4]), static_cast<f32>(v[5])};
        else if (name == "translate" && (count == 1 || count == 2))
        {
            m.e = static_cast<f32>(v[0]);
            m.f = static_cast<f32>(v[1]);
        }
        else if (name == "scale" && (count == 1 || count == 2))
        {
            m.a = static_cast<f32>(v[0]);
            m.d = static_cast<f32>(count == 1 ? v[0] : v[1]);
        }
        else if (name == "rotate" && (count == 1 || count == 3))
        {
            const f64 a = std::remainder(v[0], 360.0) * radians;
            m.a = m.d = static_cast<f32>(std::cos(a));
            m.b = static_cast<f32>(std::sin(a));
            m.c = -m.b;
            if (count == 3)
            {
                m.e = static_cast<f32>(v[1] - m.a * v[1] - m.c * v[2]);
                m.f = static_cast<f32>(v[2] - m.b * v[1] - m.d * v[2]);
            }
        }
        else if (name == "skewX" && count == 1)
            m.c = static_cast<f32>(std::tan(v[0] * radians));
        else if (name == "skewY" && count == 1)
            m.b = static_cast<f32>(std::tan(v[0] * radians));
        else
            return fail(error, vector_error_code::INVALID_SVG,
                        "unknown transform or wrong argument count", origin + begin);
        result = multiply(result, m);
        if (!finite(result))
            return fail(error, vector_error_code::INVALID_SVG, "transform overflow",
                        origin + begin);
    }
}
