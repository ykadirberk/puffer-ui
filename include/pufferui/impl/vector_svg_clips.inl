// Clip references and viewport masks for svg_compiler (included in its class).
// Object-bounding-box coordinates are resolved against un-stroked geometry.
// Referenced paths retain fill rules and affine transforms, without UI geometry.
// A cycle or an exhausted expansion budget rejects the whole asset.
bool clip_walk(u32 index, const svg_style &parent, affine2 parent_matrix, vec2 viewport,
               clip &result)
{
    const auto &node = tree[index];
    if (!spend(node.offset)) return false;
    if (active.size() >= 64 || std::find(active.begin(), active.end(), index) != active.end())
        return fail(error, vector_error_code::INVALID_SVG, "cyclic SVG clip/use reference",
                    node.offset);
    active.push_back(index);
    struct pop
    {
        std::vector<u32> &v;
        ~pop() { v.pop_back(); }
    } guard{active};
    svg_style style = parent;
    if (!svg_style_apply(node, style, error, viewport)) return false;
    if (style.hidden) return true;
    affine2 local;
    if (const auto *a = svg_property(node, "transform"))
        if (!svg_transform(a->value, local, error, a->offset)) return false;
    const affine2 matrix = multiply(parent_matrix, local);
    if (!finite(matrix))
        return fail(error, vector_error_code::INVALID_SVG, "clip transform overflow", node.offset);
    if (node.name == "use")
    {
        const auto *a = href(node);
        if (!a)
            return fail(error, vector_error_code::INVALID_SVG, "SVG use requires href",
                        node.offset);
        std::string id;
        u32 target;
        f32 x, y;
        if (!svg_local_reference(a->value, id, error, a->offset) ||
            !lookup(id, target, a->offset) || !svg_length(node, "x", x, 0, viewport.x, error) ||
            !svg_length(node, "y", y, 0, viewport.y, error))
            return false;
        return clip_walk(target, style, multiply(matrix, {1, 0, 0, 1, x, y}), viewport, result);
    }
    shape built;
    bool recognized;
    if (!append_geometry(node, viewport, built, recognized)) return false;
    if (recognized)
    {
        if (!style.visible || built.commands.empty()) return true;
        built.transform = matrix;
        built.rule = style.clip_rule;
        if (!style.clip_id.empty())
        {
            u32 id;
            if (!resolve_clip(style.clip_id, matrix, built.bounds, viewport, id, node.offset))
                return false;
            built.clips.push_back(id);
        }
        result.shapes.push_back(std::move(built));
        return true;
    }
    if (node.name != "g" && node.name != "clipPath")
        return fail(error, vector_error_code::UNSUPPORTED, "unsupported clipping element",
                    node.offset);
    const usize start = result.shapes.size();
    for (u32 child : node.children)
        if (!clip_walk(child, style, matrix, viewport, result)) return false;
    if (!style.clip_id.empty())
    {
        rect bounds;
        if (!range_bounds(result.shapes, start, matrix, bounds)) return false;
        u32 id;
        if (!resolve_clip(style.clip_id, matrix, bounds, viewport, id, node.offset)) return false;
        for (usize i = start; i < result.shapes.size(); ++i) result.shapes[i].clips.push_back(id);
    }
    return true;
}
bool resolve_clip(std::string_view id, affine2 matrix, rect bounds, vec2 viewport, u32 &result,
                  usize at)
{
    u32 target;
    if (!lookup(id, target, at)) return false;
    if (tree[target].name != "clipPath")
        return fail(error, vector_error_code::INVALID_SVG, "clip reference is not a clipPath", at);
    if (clip_active.size() >= 64 ||
        std::find(clip_active.begin(), clip_active.end(), target) != clip_active.end())
        return fail(error, vector_error_code::INVALID_SVG, "cyclic SVG clip reference", at);
    clip_active.push_back(target);
    struct pop
    {
        std::vector<u32> &v;
        ~pop() { v.pop_back(); }
    } guard{clip_active};
    const auto &node = tree[target];
    bool object = false;
    if (const auto *a = svg_attr(node, "clipPathUnits"))
    {
        if (a->value == "objectBoundingBox")
            object = true;
        else if (a->value != "userSpaceOnUse")
            return fail(error, vector_error_code::INVALID_SVG, "invalid clip units", a->offset);
    }
    clip built;
    svg_style style;
    if (!inherited_style(node.parent, style)) return false;
    style.hidden = false;
    if (object) matrix = multiply(matrix, {bounds.w, 0, 0, bounds.h, bounds.x, bounds.y});
    if (!object || (bounds.w > 0 && bounds.h > 0))
        if (!clip_walk(target, style, matrix, object ? vec2{1, 1} : viewport, built)) return false;
    if (out.clips.size() >= 32768)
        return fail(error, vector_error_code::LIMIT, "SVG clip expansion limit", at);
    result = static_cast<u32>(out.clips.size());
    out.clips.push_back(std::move(built));
    return true;
}
bool viewport_clip(rect viewport, affine2 matrix, u32 &index, usize at)
{
    if (out.clips.size() >= 32768)
        return fail(error, vector_error_code::LIMIT, "SVG viewport clip limit", at);
    shape s;
    s.commands = {{path_verb::MOVE, {viewport.x, viewport.y}},
                  {path_verb::LINE, {viewport.x + viewport.w, viewport.y}},
                  {path_verb::LINE, {viewport.x + viewport.w, viewport.y + viewport.h}},
                  {path_verb::LINE, {viewport.x, viewport.y + viewport.h}},
                  {path_verb::CLOSE}};
    if (commands > options.max_commands || s.commands.size() > options.max_commands - commands)
        return fail(error, vector_error_code::LIMIT, "SVG viewport command limit", at);
    commands += s.commands.size();
    s.bounds = viewport;
    s.transform = matrix;
    clip c;
    c.shapes.push_back(std::move(s));
    index = static_cast<u32>(out.clips.size());
    out.clips.push_back(std::move(c));
    return true;
}
