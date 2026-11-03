// SVG icon scene compilation; immutable resources are resolved before drawing.
// Included inside pui::vector_detail. No renderer or file-system dependency.
// Explicit operations preserve group opacity and clipping across child overlap.
// Reference traversal, expansion, path commands and layer counts are bounded.
struct svg_compiler
{
    const std::vector<svg_element> &tree;
    const svg_load_opts &options;
    document &out;
    vector_error &error;
    std::unordered_map<std::string, u32> ids;
    std::vector<u32> active, clip_active;
    usize commands = 0, work = 0;
    bool spend(usize at)
    {
        return ++work <= 131072 ||
               fail(error, vector_error_code::LIMIT, "SVG reference expansion budget", at);
    }
    bool index()
    {
        for (u32 i = 0; i < tree.size(); ++i)
        {
            const auto &node = tree[i];
            for (std::string_view name :
                 {"pathLength", "requiredFeatures", "requiredExtensions", "systemLanguage"})
                if (const auto *a = svg_attr(node, name))
                    return fail(error, vector_error_code::UNSUPPORTED,
                                "unsupported SVG geometry/conditional attribute", a->offset);
            if (const auto *a = svg_attr(node, "id"))
                if (a->value.empty() || !ids.emplace(a->value, i).second)
                    return fail(error, vector_error_code::INVALID_SVG, "duplicate/empty SVG id",
                                a->offset);
            for (std::string_view unsupported :
                 {"script", "style", "foreignObject", "image", "text", "animate",
                  "animateTransform", "animateMotion", "set"})
                if (node.name == unsupported)
                    return fail(error, vector_error_code::UNSUPPORTED, "unsupported SVG element",
                                node.offset);
            for (const auto &a : node.attributes)
                if (a.name.starts_with("on"))
                    return fail(error, vector_error_code::UNSUPPORTED,
                                "SVG event handlers are unsupported", a.offset);
        }
        return true;
    }
    bool lookup(std::string_view id, u32 &value, usize at)
    {
        const auto found = ids.find(std::string(id));
        if (found == ids.end())
            return fail(error, vector_error_code::INVALID_SVG, "unresolved SVG reference", at);
        value = found->second;
        return true;
    }
    const svg_attribute *href(const svg_element &node)
    {
        const auto *a = svg_attr(node, "href");
        return a ? a : svg_attr(node, "xlink:href");
    }
    bool inherited_style(u32 index, svg_style &style)
    {
        std::vector<u32> parents;
        for (u32 i = index;; i = tree[i].parent)
        {
            parents.push_back(i);
            if (i == 0) break;
        }
        for (auto i = parents.rbegin(); i != parents.rend(); ++i)
            if (!svg_style_apply(tree[*i], style, error)) return false;
        return true;
    }
#include "vector_svg_gradients.inl"
    bool paint_value(std::string_view value, const svg_style &style, paint &result, vec2 viewport,
                     usize at)
    {
        value = trim(value);
        result = {};
        if (value == "none")
        {
            result.kind = vector_paint_kind::NONE;
            return true;
        }
        if (svg_lower(value) == "currentcolor")
        {
            result.tint = style.current;
            result.current_color = style.dynamic_color;
            return true;
        }
        if (svg_lower(value.substr(0, 4)) == "url(")
        {
            std::string id;
            u32 reference;
            return svg_local_reference(value, id, error, at) && lookup(id, reference, at) &&
                   gradient(reference, result, viewport);
        }
        return svg_color(value, result.tint, error, at);
    }
    bool append_geometry(const svg_element &node, vec2 viewport, shape &built, bool &recognized)
    {
        if (!svg_geometry(node, viewport, built.commands, options.max_commands - commands,
                          recognized, error))
            return false;
        commands += built.commands.size();
        return !recognized || command_bounds(built.commands, built.bounds, error);
    }
    bool range_bounds(const std::vector<shape> &shapes, usize start, affine2 matrix, rect &bounds)
    {
        affine2 inv;
        if (!inverse(matrix, inv))
        {
            bounds = {};
            return true;
        }
        bounds_builder builder;
        for (usize i = start; i < shapes.size(); ++i)
        {
            rect box;
            if (!svg_transformed_bounds(shapes[i], multiply(inv, shapes[i].transform), box, error))
                return false;
            builder.add(box.x, box.y);
            builder.add(static_cast<f64>(box.x) + box.w, static_cast<f64>(box.y) + box.h);
        }
        bounds = builder.value();
        return true;
    }
#include "vector_svg_clips.inl"
    bool walk(u32 index, const svg_style &parent, affine2 parent_matrix, vec2 viewport,
              bool referenced = false, const rect *forced = nullptr)
    {
        const auto &node = tree[index];
        if (!spend(node.offset)) return false;
        if (active.size() >= 64 || std::find(active.begin(), active.end(), index) != active.end())
            return fail(error, vector_error_code::INVALID_SVG, "cyclic SVG use reference",
                        node.offset);
        active.push_back(index);
        struct pop
        {
            std::vector<u32> &v;
            ~pop() { v.pop_back(); }
        } guard{active};
        if (node.name == "defs" || node.name == "linearGradient" || node.name == "radialGradient" ||
            node.name == "clipPath" || node.name == "title" || node.name == "desc" ||
            node.name == "metadata" || (node.name == "symbol" && !referenced))
            return true;
        svg_style style = parent;
        if (!svg_style_apply(node, style, error, viewport)) return false;
        if (style.hidden) return true;
        affine2 local;
        if (const auto *a = svg_property(node, "transform"))
            if (!svg_transform(a->value, local, error, a->offset)) return false;
        affine2 matrix = multiply(parent_matrix, local);
        if (node.name == "use")
        {
            f32 x, y;
            if (!svg_length(node, "x", x, 0, viewport.x, error) ||
                !svg_length(node, "y", y, 0, viewport.y, error))
                return false;
            // SVG2 applies this translation to use itself, including its clip.
            matrix = multiply(matrix, {1, 0, 0, 1, x, y});
        }
        if (!finite(matrix))
            return fail(error, vector_error_code::INVALID_SVG, "SVG transform overflow",
                        node.offset);
        u32 viewport_id = 0;
        bool clipped_viewport = false;
        if (index != 0 && (node.name == "svg" || node.name == "symbol"))
        {
            rect area;
            bool box_present;
            rect box;
            vec2 align;
            bool preserve, slice;
            if (forced)
                area = *forced;
            else if (!svg_length(node, "x", area.x, 0, viewport.x, error) ||
                     !svg_length(node, "y", area.y, 0, viewport.y, error) ||
                     !svg_length(node, "width", area.w, viewport.x, viewport.x, error) ||
                     !svg_length(node, "height", area.h, viewport.y, viewport.y, error))
                return false;
            if (area.w < 0 || area.h < 0)
                return fail(error, vector_error_code::INVALID_SVG, "negative SVG viewport",
                            node.offset);
            if (area.w == 0 || area.h == 0) return true;
            if (!svg_view_box(node, box, box_present, error) ||
                !svg_aspect(node, align, preserve, slice, error))
                return false;
            if (box_present && (box.w == 0 || box.h == 0)) return true;
            const auto *overflow = svg_property(node, "overflow");
            if (!overflow || svg_lower(trim(overflow->value)) != "visible")
            {
                if (!viewport_clip(area, matrix, viewport_id, node.offset)) return false;
                clipped_viewport = true;
            }
            if (!box_present) box = {0, 0, area.w, area.h};
            matrix = multiply(matrix, svg_viewport(box, area, align, preserve, slice));
            viewport = {box.w, box.h};
            if (!finite(matrix))
                return fail(error, vector_error_code::INVALID_SVG, "SVG viewport overflow",
                            node.offset);
        }
        const bool group = style.opacity != 1 || !style.clip_id.empty() || clipped_viewport;
        const usize group_at = out.operations.size(), shape_start = out.shapes.size();
        if (out.operations.size() >= 65532)
            return fail(error, vector_error_code::LIMIT, "SVG operation limit", node.offset);
        if (group) out.operations.push_back({operation_kind::BEGIN_GROUP, 0, style.opacity});
        bool recognized = false;
        shape built;
        if (!append_geometry(node, viewport, built, recognized)) return false;
        if (recognized)
        {
            if (style.visible && !built.commands.empty())
            {
                built.transform = matrix;
                built.pen = style.pen;
                built.rule = style.rule;
                built.stroke_first = style.stroke_first;
                built.fill_opacity = style.fill_opacity;
                built.stroke_opacity = style.stroke_opacity;
                if (!paint_value(style.fill, style, built.fill, viewport, node.offset) ||
                    !paint_value(style.outline, style, built.outline, viewport, node.offset))
                    return false;
                if (out.shapes.size() >= 32768)
                    return fail(error, vector_error_code::LIMIT, "SVG shape expansion limit",
                                node.offset);
                const u32 shape_index = static_cast<u32>(out.shapes.size());
                out.shapes.push_back(std::move(built));
                out.operations.push_back({operation_kind::SHAPE, shape_index});
            }
        }
        else if (node.name == "use")
        {
            const auto *a = href(node);
            if (!a)
                return fail(error, vector_error_code::INVALID_SVG, "SVG use requires href",
                            node.offset);
            std::string id;
            u32 target;
            rect area;
            if (!svg_local_reference(a->value, id, error, a->offset) ||
                !lookup(id, target, a->offset))
                return false;
            if (tree[target].name == "svg" || tree[target].name == "symbol")
            {
                const auto &referenced_node = tree[target];
                if (!svg_length(referenced_node, "x", area.x, 0, viewport.x, error) ||
                    !svg_length(referenced_node, "y", area.y, 0, viewport.y, error) ||
                    !svg_length(referenced_node, "width", area.w, viewport.x, viewport.x, error) ||
                    !svg_length(referenced_node, "height", area.h, viewport.y, viewport.y, error))
                    return false;
                const auto dimension = [&](std::string_view key, f32 &out, f32 basis)
                {
                    const auto *value = svg_property(node, key);
                    return !value || svg_lower(trim(value->value)) == "auto" ||
                           svg_scalar(value->value, out, error, value->offset, true, basis);
                };
                if (!dimension("width", area.w, viewport.x) ||
                    !dimension("height", area.h, viewport.y))
                    return false;
                if (!walk(target, style, matrix, viewport, true, &area)) return false;
            }
            else if (!walk(target, style, matrix, viewport, true))
                return false;
        }
        else if (node.name == "svg" || node.name == "g" || node.name == "a" ||
                 node.name == "symbol")
        {
            for (u32 child : node.children)
                if (!walk(child, style, matrix, viewport)) return false;
        }
        else
            return fail(error, vector_error_code::UNSUPPORTED,
                        "unsupported SVG shape/group element", node.offset);
        if (group)
        {
            if (!style.clip_id.empty())
            {
                rect box;
                if (!range_bounds(out.shapes, shape_start, matrix, box)) return false;
                u32 clip_id;
                if (!resolve_clip(style.clip_id, matrix, box, viewport, clip_id, node.offset))
                    return false;
                out.operations[group_at].clips.push_back(clip_id);
            }
            if (clipped_viewport) out.operations[group_at].clips.push_back(viewport_id);
            out.operations.push_back({operation_kind::END_GROUP});
        }
        return true;
    }
    bool compile()
    {
        if (!index()) return false;
        const auto &root = tree[0];
        bool box_present;
        if (!svg_view_box(root, out.view_box, box_present, error) ||
            !svg_aspect(root, out.alignment, out.preserve_aspect, out.slice, error))
            return false;
        f32 width, height;
        if (!svg_length(root, "width", width, box_present ? out.view_box.w : 300, 300, error) ||
            !svg_length(root, "height", height, box_present ? out.view_box.h : 150, 150, error))
            return false;
        if (width < 0 || height < 0)
            return fail(error, vector_error_code::INVALID_SVG, "negative SVG dimensions",
                        root.offset);
        if (!box_present) out.view_box = {0, 0, width, height};
        if (width == 0 || height == 0 || out.view_box.w == 0 || out.view_box.h == 0) return true;
        return walk(0, {}, {}, {out.view_box.w, out.view_box.h});
    }
};
