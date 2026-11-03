// Immutable font-independent Slug paths and paint metadata for GPU renderers.
// Construction is resource preparation; drawing only reads the retained result.
// Words are explicitly packed, independent of C++ padding and SDK layouts.
// Included inside pui after renderer_vector_slug.inl.
#if defined(PUFFERUI_ENABLE_HARFBUZZ)
namespace renderer_detail::vector_slug
{
constexpr u32 NO_CLIP = 0xffffffffu, EMPTY_CLIP = 0xfffffffeu;
struct scene_step
{
    enum kind : u32
    {
        PAINT,
        PUSH,
        POP
    } type = PAINT;
    rect bounds{};
    u32 path = NO_CLIP, paint = NO_CLIP, clip = NO_CLIP, pair = 0;
    f32 opacity = 1;
};
struct prepared_scene
{
    std::vector<u32> words;
    std::vector<scene_step> steps;
    rect bounds{};
    u32 max_depth = 0, quads = 0;
};
inline u32 float_word(f32 value)
{
    u32 result;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}
inline std::vector<path_command> map_commands(std::span<const path_command> commands,
                                              affine2 matrix)
{
    std::vector<path_command> out(commands.begin(), commands.end());
    for (auto &command : out)
    {
        if (command.verb == path_verb::CLOSE) continue;
        command.p1 = matrix.apply(command.p1);
        if (command.verb == path_verb::QUADRATIC || command.verb == path_verb::CUBIC)
            command.p2 = matrix.apply(command.p2);
        if (command.verb == path_verb::CUBIC) command.p3 = matrix.apply(command.p3);
    }
    return out;
}
struct scene_builder
{
    const vector_detail::document &source;
    const vector_prepare_opts &options;
    vector_error &error;
    prepared_scene built;
    encoder encode;
    path_data encoded;
    std::vector<std::vector<path_command>> clips;
    std::vector<u32> clip_locations;
    std::vector<u32> groups;
    f64 tolerance = 0;
    usize clip_bytes = 0;

    bool bounded()
    {
        const usize bytes =
            built.words.capacity() * sizeof(u32) + built.steps.capacity() * sizeof(scene_step);
        if (bytes > options.resource_budget_bytes ||
            clip_bytes > options.resource_budget_bytes - bytes)
            return vector_detail::fail(error, vector_error_code::LIMIT,
                                       "native vector metadata budget");
        return true;
    }
    bool path(std::span<const path_command> commands, affine2 matrix, path_fill_rule rule,
              bool canonical, u32 &location, rect &bounds)
    {
        location = NO_CLIP;
        bounds = {};
        if (!encode.encode(commands,
                           {.transform = matrix,
                            .view_box = source.view_box,
                            .max_pixels = options.max_pixel_size,
                            .rule = rule,
                            .canonical = canonical,
                            .budget_bytes = options.resource_budget_bytes},
                           encoded, error))
            return false;
        if (encoded.bytes.empty()) return true;
        const usize words = encoded.bytes.size() / sizeof(u32);
        if (words > options.resource_budget_bytes / sizeof(u32) ||
            built.words.size() > options.resource_budget_bytes / sizeof(u32) - words)
            return vector_detail::fail(error, vector_error_code::LIMIT,
                                       "native vector outline budget");
        if (built.words.size() & 1) built.words.push_back(0);
        const u32 blob = static_cast<u32>(built.words.size() / 2);
        const usize start = built.words.size();
        built.words.resize(start + words);
        std::memcpy(built.words.data() + start, encoded.bytes.data(), encoded.bytes.size());
        location = static_cast<u32>(built.words.size());
        built.words.insert(built.words.end(),
                           {blob, float_word(encoded.scale.x), float_word(encoded.scale.y),
                            float_word(encoded.center.x), float_word(encoded.center.y), 0, 0, 0});
        bounds = encoded.bounds;
        return bounded();
    }
    bool combine(std::span<const path_command> first, std::span<const path_command> second,
                 vector_geometry::winding_mode mode, std::vector<path_command> &out)
    {
        if (first.size() >
            vector_detail::COMMAND_LIMIT - std::min(second.size(), vector_detail::COMMAND_LIMIT))
            return vector_detail::fail(error, vector_error_code::LIMIT,
                                       "native vector boolean input budget");
        std::vector<path_command> both(first.begin(), first.end());
        both.insert(both.end(), second.begin(), second.end());
        return vector_geometry::normalize_path(both, tolerance, mode, out, error);
    }
    bool clip_geometry(std::span<const u32> ids, std::vector<path_command> &out)
    {
        bool first = true;
        for (u32 id : ids)
        {
            if (id >= clips.size())
                return vector_detail::fail(error, vector_error_code::INVALID_PATH,
                                           "native vector clip ordering");
            if (first)
                out = clips[id];
            else
            {
                std::vector<path_command> intersection;
                if (!combine(out, clips[id], vector_geometry::winding_mode::INTERSECTION,
                             intersection))
                    return false;
                out.swap(intersection);
            }
            first = false;
        }
        return true;
    }
    bool clip_metadata(std::span<const u32> ids, u32 &out)
    {
        out = NO_CLIP;
        if (ids.empty()) return true;
        if (ids.size() == 1)
        {
            if (ids[0] >= clip_locations.size())
                return vector_detail::fail(error, vector_error_code::INVALID_PATH,
                                           "native vector clip index");
            out = clip_locations[ids[0]];
            return true;
        }
        std::vector<path_command> intersection;
        rect bounds;
        if (!clip_geometry(ids, intersection) ||
            !path(intersection, {}, path_fill_rule::NONZERO, true, out, bounds))
            return false;
        if (out == NO_CLIP) out = EMPTY_CLIP;
        return true;
    }
    bool prepare_clips()
    {
        for (const auto &clip : source.clips)
        {
            std::vector<path_command> compound;
            for (const auto &shape : clip.shapes)
            {
                std::vector<path_command> child,
                    transformed = map_commands(shape.commands, shape.transform);
                if (!vector_geometry::normalize_path(transformed, tolerance,
                                                     shape.rule == path_fill_rule::EVENODD
                                                         ? vector_geometry::winding_mode::EVENODD
                                                         : vector_geometry::winding_mode::NONZERO,
                                                     child, error))
                    return false;
                if (!shape.clips.empty())
                {
                    std::vector<path_command> mask, intersection;
                    if (!clip_geometry(shape.clips, mask) ||
                        !combine(child, mask, vector_geometry::winding_mode::INTERSECTION,
                                 intersection))
                        return false;
                    child.swap(intersection);
                }
                if (child.size() > vector_detail::COMMAND_LIMIT - compound.size())
                    return vector_detail::fail(error, vector_error_code::LIMIT,
                                               "native vector clip union budget");
                compound.insert(compound.end(), child.begin(), child.end());
            }
            std::vector<path_command> normalized;
            if (!vector_geometry::normalize_path(compound, tolerance,
                                                 vector_geometry::winding_mode::POSITIVE,
                                                 normalized, error))
                return false;
            u32 location;
            rect bounds;
            if (!path(normalized, {}, path_fill_rule::NONZERO, true, location, bounds))
                return false;
            clip_locations.push_back(location == NO_CLIP ? EMPTY_CLIP : location);
            clip_bytes += normalized.capacity() * sizeof(path_command);
            clips.push_back(std::move(normalized));
            if (!bounded()) return false;
        }
        return true;
    }
    bool paint(const vector_detail::shape &shape, const vector_detail::paint &paint, f32 opacity,
               u32 &location)
    {
        const auto sampler = vector_raster::prepare_paint(paint, shape.bounds, shape.transform);
        location = static_cast<u32>(built.words.size());
        const auto solid = vector_raster::color_value(paint.tint);
        const auto &m = sampler.inverse;
        built.words.insert(built.words.end(), {static_cast<u32>(paint.kind),
                                               static_cast<u32>(paint.spread),
                                               static_cast<u32>(paint.stops.size()),
                                               paint.current_color ? 1u : 0u,
                                               float_word(solid.r),
                                               float_word(solid.g),
                                               float_word(solid.b),
                                               float_word(solid.a),
                                               float_word(m.a),
                                               float_word(m.c),
                                               float_word(m.e),
                                               0,
                                               float_word(m.b),
                                               float_word(m.d),
                                               float_word(m.f),
                                               0,
                                               float_word(paint.start.x),
                                               float_word(paint.start.y),
                                               float_word(paint.end.x),
                                               float_word(paint.end.y),
                                               float_word(paint.center.x),
                                               float_word(paint.center.y),
                                               float_word(paint.focal.x),
                                               float_word(paint.focal.y),
                                               float_word(paint.radius),
                                               float_word(opacity),
                                               sampler.valid ? 1u : 0u,
                                               0,
                                               0,
                                               0,
                                               0,
                                               0});
        for (const auto &stop : paint.stops)
        {
            const auto c = vector_raster::color_value(stop.tint);
            built.words.insert(built.words.end(),
                               {float_word(stop.offset), stop.current_color ? 1u : 0u,
                                float_word(stop.opacity), 0, float_word(c.r), float_word(c.g),
                                float_word(c.b), float_word(c.a)});
        }
        return bounded();
    }
    bool shape(const vector_detail::shape &shape, bool outline)
    {
        const auto &brush = outline ? shape.outline : shape.fill;
        const f32 opacity = outline ? shape.stroke_opacity : shape.fill_opacity;
        if (brush.kind == vector_paint_kind::NONE || opacity == 0 ||
            (outline && shape.pen.width <= 0))
            return true;
        scene_step step;
        const auto commands = [&](std::span<const path_command> input, bool canonical)
        {
            return path(input, shape.transform, outline ? path_fill_rule::NONZERO : shape.rule,
                        canonical, step.path, step.bounds);
        };
        if (outline)
        {
            const affine2 pixels =
                vector_detail::multiply({options.max_pixel_size.x / source.view_box.w, 0, 0,
                                         options.max_pixel_size.y / source.view_box.h, 0, 0},
                                        shape.transform);
            const f64 scale = vector_raster::transform_scale(pixels);
            if (scale == 0) return true;
            std::vector<vector_geometry::contour> contours;
            std::vector<path_command> stroked;
            if (!vector_geometry::flatten(shape.commands, .015625 / scale, contours, error) ||
                !vector_geometry::stroke_path(contours, shape.pen, stroked, error,
                                              .015625 / scale) ||
                !commands(stroked, true))
                return false;
        }
        else if (!commands(shape.commands, false))
            return false;
        if (step.path == NO_CLIP) return true;
        if (!clip_metadata(shape.clips, step.clip) || !paint(shape, brush, opacity, step.paint))
            return false;
        built.steps.push_back(step);
        ++built.quads;
        return bounded();
    }
    bool build()
    {
        if (!(source.view_box.w > 0 && source.view_box.h > 0)) return true;
        const f64 scale = std::max(static_cast<f64>(options.max_pixel_size.x) / source.view_box.w,
                                   static_cast<f64>(options.max_pixel_size.y) / source.view_box.h);
        if (!(scale > 0) || !std::isfinite(scale) ||
            options.resource_budget_bytes > VECTOR_CACHE_BYTES)
            return vector_detail::fail(error, vector_error_code::LIMIT,
                                       "native vector scene preparation scale/budget");
        tolerance = .015625 / scale;
        if (!prepare_clips()) return false;
        for (const auto &op : source.operations)
        {
            if (op.kind == vector_operation_kind::SHAPE)
            {
                if (op.index >= source.shapes.size())
                    return vector_detail::fail(error, vector_error_code::INVALID_PATH,
                                               "native vector shape index");
                const auto &s = source.shapes[op.index];
                if (!shape(s, s.stroke_first) || !shape(s, !s.stroke_first)) return false;
            }
            else if (op.kind == vector_operation_kind::BEGIN_GROUP)
            {
                if (groups.size() == 64)
                    return vector_detail::fail(error, vector_error_code::LIMIT,
                                               "native vector group depth");
                scene_step step;
                step.type = scene_step::PUSH;
                step.opacity = op.opacity;
                if (!clip_metadata(op.clips, step.clip)) return false;
                groups.push_back(static_cast<u32>(built.steps.size()));
                built.steps.push_back(step);
                built.max_depth = std::max(built.max_depth, static_cast<u32>(groups.size()));
            }
            else if (op.kind == vector_operation_kind::END_GROUP)
            {
                if (groups.empty())
                    return vector_detail::fail(error, vector_error_code::INVALID_PATH,
                                               "native vector unmatched group end");
                const u32 begin = groups.back();
                groups.pop_back();
                vector_detail::bounds_builder box;
                for (usize i = begin + 1; i < built.steps.size(); ++i)
                    if (built.steps[i].type == scene_step::PAINT)
                    {
                        const rect r = built.steps[i].bounds;
                        box.add(r.x, r.y);
                        box.add(r.right(), r.bottom());
                    }
                auto &push = built.steps[begin];
                push.bounds = box.value();
                push.pair = static_cast<u32>(built.steps.size());
                scene_step pop = push;
                pop.type = scene_step::POP;
                pop.pair = begin;
                built.steps.push_back(pop);
                ++built.quads;
            }
            else
                return vector_detail::fail(error, vector_error_code::INVALID_PATH,
                                           "native vector operation kind");
            if (!bounded()) return false;
        }
        if (!groups.empty())
            return vector_detail::fail(error, vector_error_code::INVALID_PATH,
                                       "native vector unclosed group");
        vector_detail::bounds_builder box;
        bool visible = false;
        for (const auto &step : built.steps)
            if (step.type == scene_step::PAINT)
            {
                visible = true;
                box.add(step.bounds.x, step.bounds.y);
                box.add(step.bounds.right(), step.bounds.bottom());
            }
        built.bounds = box.value();
        if (!visible)
        {
            built.words.clear();
            built.steps.clear();
            built.quads = 0;
            built.max_depth = 0;
        }
        return true;
    }
};
inline bool prepare_scene(const vector_detail::document &source, const vector_prepare_opts &options,
                          prepared_scene &out, vector_error &error)
{
    try
    {
        scene_builder builder{source, options, error};
        if (!builder.build()) return false;
        out = std::move(builder.built);
        return true;
    }
    catch (const std::bad_alloc &)
    {
        return vector_detail::fail(error, vector_error_code::LIMIT,
                                   "native vector preparation allocation");
    }
}
} // namespace renderer_detail::vector_slug
#endif
