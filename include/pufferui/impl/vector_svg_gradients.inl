// Gradient resolution for svg_compiler; included inside that class.
// Inherited definitions are expanded once while loading, never while drawing.
// Coordinates stay in gradient space, with explicit units/affine/spread.
// Color/stop alpha and currentColor remain independent of text gamma.
bool gradient(u32 index, paint &target, vec2 viewport)
{
    const bool radial = tree[index].name == "radialGradient";
    if (!radial && tree[index].name != "linearGradient")
        return fail(error, vector_error_code::INVALID_SVG, "paint reference is not a gradient",
                    tree[index].offset);
    std::vector<u32> chain;
    for (u32 i = index;;)
    {
        if (!spend(tree[i].offset)) return false;
        if (chain.size() >= 64 || std::find(chain.begin(), chain.end(), i) != chain.end())
            return fail(error, vector_error_code::INVALID_SVG, "cyclic SVG gradient reference",
                        tree[i].offset);
        if (tree[i].name != "radialGradient" && tree[i].name != "linearGradient")
            return fail(error, vector_error_code::INVALID_SVG, "invalid SVG gradient template",
                        tree[i].offset);
        chain.push_back(i);
        const auto *a = href(tree[i]);
        if (!a) break;
        std::string id;
        if (!svg_local_reference(a->value, id, error, a->offset) || !lookup(id, i, a->offset))
            return false;
    }
    target = {};
    target.kind = radial ? vector_paint_kind::RADIAL : vector_paint_kind::LINEAR;
    target.object_bounds = true;
    const char *keys[6] = {radial ? "cx" : "x1",
                           radial ? "cy" : "y1",
                           radial ? "r" : "x2",
                           radial ? "fx" : "y2",
                           "fy",
                           "fr"};
    std::string values[6] = {radial ? "50%" : "0%",
                             radial ? "50%" : "0%",
                             radial ? "50%" : "100%",
                             radial ? "" : "0%",
                             "",
                             "0"};
    u32 stops_owner = index;
    for (auto link = chain.rbegin(); link != chain.rend(); ++link)
    {
        const auto &node = tree[*link];
        if (const auto *a = svg_attr(node, "gradientUnits"))
        {
            if (a->value == "objectBoundingBox")
                target.object_bounds = true;
            else if (a->value == "userSpaceOnUse")
                target.object_bounds = false;
            else
                return fail(error, vector_error_code::INVALID_SVG, "invalid gradient units",
                            a->offset);
        }
        const auto *transform = svg_property(node, "transform");
        if (!transform) transform = svg_attr(node, "gradientTransform");
        if (transform &&
            !svg_transform(transform->value, target.transform, error, transform->offset))
            return false;
        if (const auto *a = svg_attr(node, "spreadMethod"))
        {
            if (a->value == "pad")
                target.spread = vector_spread::PAD;
            else if (a->value == "repeat")
                target.spread = vector_spread::REPEAT;
            else if (a->value == "reflect")
                target.spread = vector_spread::REFLECT;
            else
                return fail(error, vector_error_code::INVALID_SVG, "invalid gradient spread",
                            a->offset);
        }
        for (i32 i = 0; i < (radial ? 6 : 4); ++i)
            if (const auto *a = svg_attr(node, keys[i])) values[i] = a->value;
        for (u32 child : node.children)
            if (tree[child].name == "stop")
            {
                stops_owner = *link;
                break;
            }
    }
    const f32 x = target.object_bounds ? 1 : viewport.x, y = target.object_bounds ? 1 : viewport.y;
    const f32 r = static_cast<f32>(std::hypot(static_cast<f64>(x), y) / std::sqrt(2.0));
    if (radial)
    {
        if (values[3].empty()) values[3] = values[0];
        if (values[4].empty()) values[4] = values[1];
        f32 fr;
        if (!svg_scalar(values[0], target.center.x, error, tree[index].offset, true, x) ||
            !svg_scalar(values[1], target.center.y, error, tree[index].offset, true, y) ||
            !svg_scalar(values[2], target.radius, error, tree[index].offset, true, r) ||
            !svg_scalar(values[3], target.focal.x, error, tree[index].offset, true, x) ||
            !svg_scalar(values[4], target.focal.y, error, tree[index].offset, true, y) ||
            !svg_scalar(values[5], fr, error, tree[index].offset, true, r))
            return false;
        if (target.radius < 0 || fr < 0)
            return fail(error, vector_error_code::INVALID_SVG, "negative SVG gradient radius",
                        tree[index].offset);
        if (fr != 0)
            return fail(error, vector_error_code::UNSUPPORTED, "radial focal radius is unsupported",
                        tree[index].offset);
    }
    else if (!svg_scalar(values[0], target.start.x, error, tree[index].offset, true, x) ||
             !svg_scalar(values[1], target.start.y, error, tree[index].offset, true, y) ||
             !svg_scalar(values[2], target.end.x, error, tree[index].offset, true, x) ||
             !svg_scalar(values[3], target.end.y, error, tree[index].offset, true, y))
        return false;
    f32 previous = 0;
    svg_style host_style;
    if (!inherited_style(index, host_style)) return false;
    for (u32 child : tree[stops_owner].children)
    {
        const auto &node = tree[child];
        if (node.name == "title" || node.name == "desc" || node.name == "metadata") continue;
        if (node.name != "stop")
            return fail(error, vector_error_code::UNSUPPORTED, "unsupported SVG gradient child",
                        node.offset);
        if (target.stops.size() >= STOP_LIMIT)
            return fail(error, vector_error_code::LIMIT, "SVG gradient stop limit", node.offset);
        svg_style style = host_style;
        if (!svg_style_apply(node, style, error)) return false;
        gradient_stop stop;
        const auto *a = svg_attr(node, "offset");
        if (a && !svg_unit(a->value, stop.offset, error, a->offset)) return false;
        stop.offset = std::max(stop.offset, previous);
        previous = stop.offset;
        stop.opacity = style.stop_opacity;
        if (svg_lower(trim(style.stop_color)) == "currentcolor")
        {
            stop.tint = style.current;
            stop.current_color = style.dynamic_color;
        }
        else if (!svg_color(style.stop_color, stop.tint, error, node.offset))
            return false;
        target.stops.push_back(stop);
    }
    if (target.stops.empty())
    {
        target.kind = vector_paint_kind::NONE;
        return true;
    }
    if (target.stops.size() == 1)
    {
        auto stop = target.stops.front();
        stop.offset = 1;
        target.stops.push_back(stop);
    }
    return true;
}
