// Conversion of SVG shape elements into immutable path commands.
// Included inside pui::vector_detail by vector_svg.inl.
// Ellipse/arc conversion uses cubic Beziers; no tessellation happens here.
// Bounds and command budgets are validated again by the resource compiler.
inline bool svg_length(const svg_element &element, std::string_view key, f32 &out,
                       f32 default_value, f32 basis, vector_error &error)
{
    const auto *a = svg_property(element, key);
    out = default_value;
    if (a && (key == "width" || key == "height") && svg_lower(trim(a->value)) == "auto")
        return true;
    return !a || svg_scalar(a->value, out, error, a->offset, true, basis);
}
inline bool svg_view_box(const svg_element &element, rect &box, bool &present, vector_error &error)
{
    const auto *a = svg_attr(element, "viewBox");
    present = a != nullptr;
    if (!a) return true;
    svg_numbers n{a->value, 0, a->offset, error};
    f64 v[4];
    for (f64 &number : v)
        if (!n.number(number)) return false;
    n.whitespace();
    if (n.at != a->value.size())
        return fail(error, vector_error_code::INVALID_SVG, "invalid viewBox", a->offset + n.at);
    box = {static_cast<f32>(v[0]), static_cast<f32>(v[1]), static_cast<f32>(v[2]),
           static_cast<f32>(v[3])};
    if (!std::isfinite(box.x) || !std::isfinite(box.y) || !std::isfinite(box.w) ||
        !std::isfinite(box.h) || box.w < 0 || box.h < 0)
        return fail(error, vector_error_code::INVALID_SVG, "invalid viewBox bounds", a->offset);
    return true;
}
inline bool svg_aspect(const svg_element &element, vec2 &alignment, bool &preserve, bool &slice,
                       vector_error &error)
{
    alignment = {.5f, .5f};
    preserve = true;
    slice = false;
    const auto *a = svg_attr(element, "preserveAspectRatio");
    if (!a) return true;
    auto input = trim(a->value);
    if (input.starts_with("defer ")) input = trim(input.substr(6));
    const usize space = input.find_first_of(" \t\r\n");
    const auto align = input.substr(0, space);
    auto suffix = space == std::string_view::npos ? std::string_view{} : trim(input.substr(space));
    if (align == "none")
        preserve = false;
    else
    {
        if (align.size() != 8 || align.front() != 'x' || align[4] != 'Y')
            return fail(error, vector_error_code::INVALID_SVG, "invalid SVG aspect alignment",
                        a->offset);
        const auto x = align.substr(1, 3), y = align.substr(5, 3);
        if (x == "Min")
            alignment.x = 0;
        else if (x == "Mid")
            alignment.x = .5f;
        else if (x == "Max")
            alignment.x = 1;
        else
            return fail(error, vector_error_code::INVALID_SVG, "invalid SVG x alignment",
                        a->offset);
        if (y == "Min")
            alignment.y = 0;
        else if (y == "Mid")
            alignment.y = .5f;
        else if (y == "Max")
            alignment.y = 1;
        else
            return fail(error, vector_error_code::INVALID_SVG, "invalid SVG y alignment",
                        a->offset);
    }
    if (suffix == "slice")
        slice = true;
    else if (!suffix.empty() && suffix != "meet")
        return fail(error, vector_error_code::INVALID_SVG, "invalid SVG aspect mode", a->offset);
    return true;
}
inline affine2 svg_viewport(rect box, rect viewport, vec2 align, bool preserve, bool slice)
{
    f32 sx = box.w > 0 ? viewport.w / box.w : 0, sy = box.h > 0 ? viewport.h / box.h : 0;
    if (preserve) sx = sy = slice ? std::max(sx, sy) : std::min(sx, sy);
    return {sx,
            0,
            0,
            sy,
            viewport.x + (viewport.w - box.w * sx) * align.x - box.x * sx,
            viewport.y + (viewport.h - box.h * sy) * align.y - box.y * sy};
}
inline bool svg_geometry(const svg_element &element, vec2 viewport, std::vector<path_command> &out,
                         usize limit, bool &recognized, vector_error &error)
{
    recognized = true;
    const auto add = [&](path_command command)
    { return svg_command(out, command, limit, error, element.offset); };
    const auto length = [&](std::string_view name, f32 &value, f32 initial, f32 basis)
    { return svg_length(element, name, value, initial, basis, error); };
    if (element.name == "path")
    {
        const auto *d = svg_attr(element, "d");
        return !d || svg_path(d->value, out, limit, error, d->offset);
    }
    if (element.name == "line")
    {
        f32 x1, y1, x2, y2;
        if (!length("x1", x1, 0, viewport.x) || !length("y1", y1, 0, viewport.y) ||
            !length("x2", x2, 0, viewport.x) || !length("y2", y2, 0, viewport.y))
            return false;
        return add({path_verb::MOVE, {x1, y1}}) && add({path_verb::LINE, {x2, y2}});
    }
    if (element.name == "polyline" || element.name == "polygon")
    {
        const auto *a = svg_attr(element, "points");
        if (!a) return true;
        svg_numbers n{a->value, 0, a->offset, error};
        bool first = true;
        while (n.available())
        {
            f64 x, y;
            if (!n.number(x) || !n.number(y)) return false;
            if (!add({first ? path_verb::MOVE : path_verb::LINE,
                      {static_cast<f32>(x), static_cast<f32>(y)}}))
                return false;
            first = false;
        }
        n.whitespace();
        if (n.at != a->value.size())
            return fail(error, vector_error_code::INVALID_SVG, "invalid polygon points",
                        a->offset + n.at);
        return first || element.name == "polyline" || add({path_verb::CLOSE});
    }
    if (element.name == "circle" || element.name == "ellipse")
    {
        f32 cx, cy, rx, ry;
        if (!length("cx", cx, 0, viewport.x) || !length("cy", cy, 0, viewport.y)) return false;
        if (element.name == "circle")
        {
            const f32 diagonal = static_cast<f32>(
                std::hypot(static_cast<f64>(viewport.x), viewport.y) / std::sqrt(2.0));
            if (!length("r", rx, 0, diagonal)) return false;
            ry = rx;
        }
        else if (!length("rx", rx, 0, viewport.x) || !length("ry", ry, 0, viewport.y))
            return false;
        if (rx < 0 || ry < 0)
            return fail(error, vector_error_code::INVALID_SVG, "negative SVG radius",
                        element.offset);
        if (rx == 0 || ry == 0) return true;
        const vec2 right{cx + rx, cy}, bottom{cx, cy + ry}, left{cx - rx, cy}, top{cx, cy - ry};
        const auto arc = [&](vec2 a, vec2 b)
        { return svg_arc(out, a, b, rx, ry, 0, false, true, limit, error, element.offset); };
        return add({path_verb::MOVE, right}) && arc(right, bottom) && arc(bottom, left) &&
               arc(left, top) && arc(top, right) && add({path_verb::CLOSE});
    }
    if (element.name == "rect")
    {
        f32 x, y, w, h, rx, ry;
        if (!length("x", x, 0, viewport.x) || !length("y", y, 0, viewport.y) ||
            !length("width", w, 0, viewport.x) || !length("height", h, 0, viewport.y))
            return false;
        if (w < 0 || h < 0)
            return fail(error, vector_error_code::INVALID_SVG, "negative SVG rectangle size",
                        element.offset);
        if (w == 0 || h == 0) return true;
        const bool has_rx = svg_attr(element, "rx"), has_ry = svg_attr(element, "ry");
        if (!length("rx", rx, 0, viewport.x) || !length("ry", ry, 0, viewport.y)) return false;
        if (!has_rx) rx = ry;
        if (!has_ry) ry = rx;
        if (rx < 0 || ry < 0)
            return fail(error, vector_error_code::INVALID_SVG, "negative SVG corner radius",
                        element.offset);
        rx = std::min(rx, w * .5f);
        ry = std::min(ry, h * .5f);
        if (rx == 0 || ry == 0)
            return add({path_verb::MOVE, {x, y}}) && add({path_verb::LINE, {x + w, y}}) &&
                   add({path_verb::LINE, {x + w, y + h}}) && add({path_verb::LINE, {x, y + h}}) &&
                   add({path_verb::CLOSE});
        const auto arc = [&](vec2 a, vec2 b)
        { return svg_arc(out, a, b, rx, ry, 0, false, true, limit, error, element.offset); };
        return add({path_verb::MOVE, {x + rx, y}}) && add({path_verb::LINE, {x + w - rx, y}}) &&
               arc({x + w - rx, y}, {x + w, y + ry}) &&
               add({path_verb::LINE, {x + w, y + h - ry}}) &&
               arc({x + w, y + h - ry}, {x + w - rx, y + h}) &&
               add({path_verb::LINE, {x + rx, y + h}}) && arc({x + rx, y + h}, {x, y + h - ry}) &&
               add({path_verb::LINE, {x, y + ry}}) && arc({x, y + ry}, {x + rx, y}) &&
               add({path_verb::CLOSE});
    }
    recognized = false;
    return true;
}
inline bool svg_transformed_bounds(const shape &source, affine2 matrix, rect &out,
                                   vector_error &error)
{
    std::vector<path_command> commands = source.commands;
    for (auto &c : commands)
    {
        c.p1 = matrix.apply(c.p1);
        c.p2 = matrix.apply(c.p2);
        c.p3 = matrix.apply(c.p3);
    }
    return command_bounds(commands, out, error);
}
