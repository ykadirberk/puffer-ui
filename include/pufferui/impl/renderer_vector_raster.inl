// Portable vector raster preparation, selected explicitly by mesh painters.
// Included inside pui after renderer_vector_geometry/paint; no UI tessellation.
// Scanline spans preserve winding rules, clip unions and isolated group alpha.
// The completed immutable RGBA image is uploaded/cached by the renderer.
namespace renderer_detail::vector_raster
{
constexpr i32 SAMPLES = 8;
constexpr usize WORK_LIMIT = 64 * 1024 * 1024;
struct edge
{
    f64 x0, y0, x1, y1;
};
struct interval
{
    f64 left, right;
};
struct hit
{
    f64 x;
    i32 winding;
};
struct geometry
{
    std::vector<edge> edges;
    path_fill_rule rule = path_fill_rule::NONZERO;
    rect bounds{};
};
struct prepared_shape
{
    geometry fill, stroke;
    paint_sampler fill_paint, stroke_paint;
};
struct prepared_clip
{
    std::vector<geometry> shapes;
};
inline bool make_geometry(const std::vector<vector_geometry::contour> &contours, affine2 matrix,
                          path_fill_rule rule, geometry &out, vector_error &error)
{
    out.rule = rule;
    vector_detail::bounds_builder bounds;
    for (const auto &path : contours)
    {
        if (path.points.size() < 2) continue;
        for (usize i = 0; i < path.points.size(); ++i)
        {
            const vec2 a = matrix.apply(path.points[i]),
                       b = matrix.apply(path.points[(i + 1) % path.points.size()]);
            if (!vector_detail::finite(a) || !vector_detail::finite(b))
                return vector_detail::fail(error, vector_error_code::LIMIT,
                                           "vector raster coordinate precision");
            bounds.add(a.x, a.y);
            bounds.add(b.x, b.y);
            if (a.y != b.y) out.edges.push_back({a.x, a.y, b.x, b.y});
        }
    }
    out.bounds = bounds.value();
    return true;
}
inline bool intersections(const geometry &geometry, f64 y, std::vector<hit> &hits,
                          std::vector<interval> &spans, usize &work, vector_error &error)
{
    hits.clear();
    spans.clear();
    if (geometry.edges.size() > WORK_LIMIT - work)
        return vector_detail::fail(error, vector_error_code::LIMIT, "vector raster work budget");
    work += geometry.edges.size();
    for (const auto &e : geometry.edges)
    {
        if (y < std::min(e.y0, e.y1) || y >= std::max(e.y0, e.y1)) continue;
        hits.push_back({e.x0 + (y - e.y0) * (e.x1 - e.x0) / (e.y1 - e.y0), e.y1 > e.y0 ? 1 : -1});
    }
    std::sort(hits.begin(), hits.end(), [](const hit &a, const hit &b) { return a.x < b.x; });
    i32 winding = 0;
    f64 start = 0;
    usize i = 0;
    while (i < hits.size())
    {
        const f64 x = hits[i].x;
        const bool before =
            geometry.rule == path_fill_rule::EVENODD ? (winding & 1) != 0 : winding != 0;
        do
        {
            winding += hits[i++].winding;
        } while (i < hits.size() && hits[i].x == x);
        const bool after =
            geometry.rule == path_fill_rule::EVENODD ? (winding & 1) != 0 : winding != 0;
        if (!before && after)
            start = x;
        else if (before && !after && x > start)
            spans.push_back({start, x});
    }
    return true;
}
inline void merge(std::vector<interval> &spans)
{
    std::sort(spans.begin(), spans.end(),
              [](const interval &a, const interval &b) { return a.left < b.left; });
    usize size = 0;
    for (const auto &span : spans)
    {
        if (span.right <= span.left) continue;
        if (size && span.left <= spans[size - 1].right)
            spans[size - 1].right = std::max(spans[size - 1].right, span.right);
        else
            spans[size++] = span;
    }
    spans.resize(size);
}
inline void intersect(const std::vector<interval> &a, const std::vector<interval> &b,
                      std::vector<interval> &out)
{
    out.clear();
    usize i = 0, j = 0;
    while (i < a.size() && j < b.size())
    {
        const f64 left = std::max(a[i].left, b[j].left), right = std::min(a[i].right, b[j].right);
        if (right > left) out.push_back({left, right});
        if (a[i].right < b[j].right)
            ++i;
        else
            ++j;
    }
}
template <class Function>
inline bool visit_pixels(const std::vector<interval> &spans, i32 width, usize &work,
                         vector_error &error, Function &&function)
{
    for (const auto &span : spans)
    {
        const f64 left = std::clamp(span.left, 0.0, static_cast<f64>(width)),
                  right = std::clamp(span.right, 0.0, static_cast<f64>(width));
        const i32 first = static_cast<i32>(std::floor(left)),
                  end = static_cast<i32>(std::ceil(right));
        if (static_cast<usize>(end - first) > WORK_LIMIT - work)
            return vector_detail::fail(error, vector_error_code::LIMIT,
                                       "vector pixel sampling budget");
        work += static_cast<usize>(end - first);
        for (i32 x = first; x < end; ++x)
        {
            const f64 lo = std::max(left, static_cast<f64>(x)),
                      hi = std::min(right, static_cast<f64>(x + 1));
            if (hi > lo)
                function(x, static_cast<f32>((lo + hi) * .5),
                         static_cast<f32>((hi - lo) / SAMPLES));
        }
    }
    return true;
}
inline f64 transform_scale(affine2 matrix)
{
    return std::hypot(static_cast<f64>(matrix.a), matrix.b) +
           std::hypot(static_cast<f64>(matrix.c), matrix.d);
}
inline bool raster_document(const vector_detail::document &source, i32 width, i32 height,
                            color current, usize budget, std::vector<u8> &pixels,
                            vector_error &error)
{
    if (width < 1 || height < 1 || width > 4096 || height > 4096 || budget > 256 * 1024 * 1024)
        return vector_detail::fail(error, vector_error_code::LIMIT,
                                   "vector raster dimensions/budget");
    const usize count = static_cast<usize>(width) * height;
    usize depth = 0, max_depth = 0, group_mask_count = 0;
    for (const auto &op : source.operations)
    {
        if (op.kind == vector_detail::operation_kind::BEGIN_GROUP)
        {
            max_depth = std::max(max_depth, ++depth);
            group_mask_count += op.clips.size() > 1;
            for (u32 clip : op.clips)
                if (clip >= source.clips.size())
                    return vector_detail::fail(error, vector_error_code::INVALID_PATH,
                                               "invalid vector group clip");
        }
        else if (op.kind == vector_detail::operation_kind::END_GROUP)
        {
            if (!depth)
                return vector_detail::fail(error, vector_error_code::INVALID_PATH,
                                           "unbalanced vector groups");
            --depth;
        }
    }
    if (depth || max_depth > 64)
        return vector_detail::fail(error, vector_error_code::LIMIT, "vector layer depth");
    const usize per_pixel =
        (max_depth + 2) * sizeof(rgba) + (source.clips.size() + group_mask_count) * sizeof(f32) + 4;
    if (count > budget / per_pixel)
        return vector_detail::fail(error, vector_error_code::LIMIT,
                                   "vector raster layer/mask budget");
    usize bytes = count * per_pixel;
    const usize metadata = source.shapes.size() * sizeof(prepared_shape) +
                           source.clips.size() * (sizeof(prepared_clip) + sizeof(std::vector<f32>) +
                                                  sizeof(std::vector<interval>)) +
                           group_mask_count * (sizeof(std::vector<f32>) + sizeof(usize)) +
                           source.operations.size() * sizeof(i32) + max_depth * sizeof(void *);
    if (metadata > budget - bytes)
        return vector_detail::fail(error, vector_error_code::LIMIT,
                                   "vector raster metadata budget");
    bytes += metadata;
    pixels.assign(count * 4, 0);
    if (source.view_box.w <= 0 || source.view_box.h <= 0) return true;
    const affine2 root{width / source.view_box.w,
                       0,
                       0,
                       height / source.view_box.h,
                       -source.view_box.x * width / source.view_box.w,
                       -source.view_box.y * height / source.view_box.h};
    if (!vector_detail::finite(root))
        return vector_detail::fail(error, vector_error_code::LIMIT,
                                   "vector raster viewport precision");
    std::vector<prepared_shape> shapes(source.shapes.size());
    std::vector<prepared_clip> clips(source.clips.size());
    const auto account = [&](const geometry &geometry)
    {
        const usize extra = geometry.edges.capacity() * sizeof(edge);
        if (extra > budget - bytes)
            return vector_detail::fail(error, vector_error_code::LIMIT,
                                       "vector raster geometry budget");
        bytes += extra;
        return true;
    };
    for (usize i = 0; i < source.shapes.size(); ++i)
    {
        const auto &shape = source.shapes[i];
        auto &prepared = shapes[i];
        const affine2 matrix = vector_detail::multiply(root, shape.transform);
        const f64 scale = transform_scale(matrix);
        if (!std::isfinite(scale))
            return vector_detail::fail(error, vector_error_code::LIMIT,
                                       "vector raster transform scale");
        if (scale == 0) continue;
        std::vector<vector_geometry::contour> contours;
        if (!vector_geometry::flatten(shape.commands, .03125 / scale, contours, error) ||
            !make_geometry(contours, matrix, shape.rule, prepared.fill, error) ||
            !account(prepared.fill))
            return false;
        prepared.fill_paint = prepare_paint(shape.fill, shape.bounds, matrix);
        prepared.stroke_paint = prepare_paint(shape.outline, shape.bounds, matrix);
        if (shape.outline.kind != vector_paint_kind::NONE && shape.pen.width > 0)
        {
            std::vector<path_command> outline;
            std::vector<vector_geometry::contour> stroke;
            if (!vector_geometry::stroke_path(contours, shape.pen, outline, error,
                                              .03125 / scale) ||
                !vector_geometry::flatten(outline, .03125 / scale, stroke, error) ||
                !make_geometry(stroke, matrix, path_fill_rule::NONZERO, prepared.stroke, error) ||
                !account(prepared.stroke))
                return false;
        }
    }
    for (usize i = 0; i < source.clips.size(); ++i)
    {
        const auto &clip = source.clips[i];
        auto &prepared = clips[i];
        prepared.shapes.resize(clip.shapes.size());
        for (usize j = 0; j < clip.shapes.size(); ++j)
        {
            const auto &shape = clip.shapes[j];
            const affine2 matrix = vector_detail::multiply(root, shape.transform);
            const f64 scale = transform_scale(matrix);
            if (!std::isfinite(scale))
                return vector_detail::fail(error, vector_error_code::LIMIT,
                                           "vector clip transform scale");
            if (scale == 0) continue;
            std::vector<vector_geometry::contour> contours;
            if (!vector_geometry::flatten(shape.commands, .0625 / scale, contours, error) ||
                !make_geometry(contours, matrix, shape.rule, prepared.shapes[j], error) ||
                !account(prepared.shapes[j]))
                return false;
            for (u32 child : shape.clips)
                if (child >= i)
                    return vector_detail::fail(error, vector_error_code::INVALID_PATH,
                                               "invalid vector clip ordering");
        }
    }
    std::vector<std::vector<f32>> masks(source.clips.size());
    for (auto &mask : masks) mask.resize(count);
    std::vector<std::vector<f32>> group_masks(group_mask_count);
    for (auto &mask : group_masks) mask.resize(count);
    std::vector<i32> group_mask_indices(source.operations.size(), -1);
    std::vector<usize> group_mask_operations;
    group_mask_operations.reserve(group_mask_count);
    for (usize i = 0, at = 0; i < source.operations.size(); ++i)
        if (source.operations[i].kind == vector_detail::operation_kind::BEGIN_GROUP &&
            source.operations[i].clips.size() > 1)
        {
            group_mask_indices[i] = static_cast<i32>(at++);
            group_mask_operations.push_back(i);
        }
    std::vector<std::vector<interval>> clip_spans(source.clips.size());
    std::vector<interval> spans, temp;
    std::vector<hit> hits;
    usize work = 0;
    for (i32 row = 0; row < height; ++row)
        for (i32 sample = 0; sample < SAMPLES; ++sample)
        {
            const f64 y = row + (sample + .5) / SAMPLES;
            for (usize i = 0; i < clips.size(); ++i)
            {
                auto &combined = clip_spans[i];
                combined.clear();
                for (usize j = 0; j < clips[i].shapes.size(); ++j)
                {
                    if (!intersections(clips[i].shapes[j], y, hits, spans, work, error))
                        return false;
                    for (u32 child : source.clips[i].shapes[j].clips)
                    {
                        intersect(spans, clip_spans[child], temp);
                        spans.swap(temp);
                    }
                    combined.insert(combined.end(), spans.begin(), spans.end());
                }
                merge(combined);
                if (!visit_pixels(combined, width, work, error, [&](i32 x, f32, f32 weight)
                                  { masks[i][static_cast<usize>(row) * width + x] += weight; }))
                    return false;
            }
            // Intersect geometric spans before sample integration. Multiplying
            // averaged masks would square AA at coincident clip boundaries.
            for (usize i : group_mask_operations)
            {
                const auto &ids = source.operations[i].clips;
                spans = clip_spans[ids.front()];
                for (usize j = 1; j < ids.size(); ++j)
                {
                    const auto &next = clip_spans[ids[j]];
                    const usize cost = spans.size() + next.size() + 1;
                    if (cost > WORK_LIMIT - work)
                        return vector_detail::fail(error, vector_error_code::LIMIT,
                                                   "vector group clip intersection budget");
                    work += cost;
                    intersect(spans, next, temp);
                    spans.swap(temp);
                }
                auto &mask = group_masks[static_cast<usize>(group_mask_indices[i])];
                if (!visit_pixels(spans, width, work, error, [&](i32 x, f32, f32 weight)
                                  { mask[static_cast<usize>(row) * width + x] += weight; }))
                    return false;
            }
        }
    std::vector<std::vector<rgba>> layers(max_depth + 1);
    for (auto &layer : layers) layer.resize(count);
    std::vector<rgba> scratch(count);
    std::vector<const vector_detail::operation *> groups;
    groups.reserve(max_depth);
    const auto paint_geometry = [&](const geometry &geometry, const paint_sampler &paint,
                                    f32 opacity) -> bool
    {
        if (geometry.edges.empty() || !paint.source ||
            paint.source->kind == vector_paint_kind::NONE || opacity == 0)
            return true;
        const i32 x0 = static_cast<i32>(std::floor(
            std::clamp(static_cast<f64>(geometry.bounds.x), 0.0, static_cast<f64>(width))));
        const i32 x1 = static_cast<i32>(
            std::ceil(std::clamp(static_cast<f64>(geometry.bounds.x) + geometry.bounds.w, 0.0,
                                 static_cast<f64>(width))));
        const i32 y0 = static_cast<i32>(std::floor(
            std::clamp(static_cast<f64>(geometry.bounds.y), 0.0, static_cast<f64>(height))));
        const i32 y1 = static_cast<i32>(
            std::ceil(std::clamp(static_cast<f64>(geometry.bounds.y) + geometry.bounds.h, 0.0,
                                 static_cast<f64>(height))));
        const usize area = static_cast<usize>(x1 - x0) * (y1 - y0);
        if (area > (WORK_LIMIT - work) / 2)
            return vector_detail::fail(error, vector_error_code::LIMIT,
                                       "vector raster composition budget");
        work += area * 2;
        for (i32 row = y0; row < y1; ++row)
            std::fill(scratch.begin() + static_cast<usize>(row) * width + x0,
                      scratch.begin() + static_cast<usize>(row) * width + x1, rgba{});
        for (i32 row = y0; row < y1; ++row)
            for (i32 sample = 0; sample < SAMPLES; ++sample)
            {
                const f64 y = row + (sample + .5) / SAMPLES;
                if (!intersections(geometry, y, hits, spans, work, error)) return false;
                if (!visit_pixels(spans, width, work, error,
                                  [&](i32 x, f32 position, f32 weight)
                                  {
                                      const rgba color = sample_paint(
                                          paint, {position, static_cast<f32>(y)}, current);
                                      const f32 alpha = color.a * opacity * weight;
                                      rgba &pixel = scratch[static_cast<usize>(row) * width + x];
                                      pixel.r += color.r * alpha;
                                      pixel.g += color.g * alpha;
                                      pixel.b += color.b * alpha;
                                      pixel.a += alpha;
                                  }))
                    return false;
            }
        auto &layer = layers[groups.size()];
        for (i32 row = y0; row < y1; ++row)
            for (i32 x = x0; x < x1; ++x)
            {
                const usize i = static_cast<usize>(row) * width + x;
                over(layer[i], scratch[i]);
            }
        return true;
    };
    for (const auto &op : source.operations)
    {
        if (op.kind == vector_detail::operation_kind::BEGIN_GROUP)
        {
            if (count > WORK_LIMIT - work)
                return vector_detail::fail(error, vector_error_code::LIMIT,
                                           "vector group composition budget");
            work += count;
            groups.push_back(&op);
            std::fill(layers[groups.size()].begin(), layers[groups.size()].end(), rgba{});
        }
        else if (op.kind == vector_detail::operation_kind::END_GROUP)
        {
            const auto *group = groups.back();
            auto &child = layers[groups.size()];
            groups.pop_back();
            auto &parent = layers[groups.size()];
            if (count > WORK_LIMIT - work)
                return vector_detail::fail(error, vector_error_code::LIMIT,
                                           "vector group composition budget");
            work += count;
            const i32 mask_index =
                group_mask_indices[static_cast<usize>(group - source.operations.data())];
            const std::vector<f32> *mask =
                mask_index >= 0 ? &group_masks[static_cast<usize>(mask_index)]
                                : (group->clips.empty() ? nullptr : &masks[group->clips.front()]);
            for (usize i = 0; i < count; ++i)
            {
                const f32 alpha = group->opacity * (mask ? (*mask)[i] : 1.0f);
                over(parent[i], {child[i].r * alpha, child[i].g * alpha, child[i].b * alpha,
                                 child[i].a * alpha});
            }
        }
        else
        {
            if (op.index >= shapes.size())
                return vector_detail::fail(error, vector_error_code::INVALID_PATH,
                                           "invalid vector shape index");
            const auto &prepared = shapes[op.index];
            const auto &shape = source.shapes[op.index];
            const auto fill = [&]()
            { return paint_geometry(prepared.fill, prepared.fill_paint, shape.fill_opacity); };
            const auto stroke = [&]()
            {
                return paint_geometry(prepared.stroke, prepared.stroke_paint, shape.stroke_opacity);
            };
            if (shape.stroke_first ? (!stroke() || !fill()) : (!fill() || !stroke())) return false;
        }
    }
    const auto byte = [](f32 value)
    { return static_cast<u8>(std::floor(std::clamp(value, 0.0f, 1.0f) * 255 + .5f)); };
    for (usize i = 0; i < count; ++i)
    {
        const rgba &color = layers[0][i];
        const f32 inverse = color.a > 0 ? 1 / color.a : 0;
        pixels[i * 4] = byte(color.r * inverse);
        pixels[i * 4 + 1] = byte(color.g * inverse);
        pixels[i * 4 + 2] = byte(color.b * inverse);
        pixels[i * 4 + 3] = byte(color.a);
    }
    return true;
}
} // namespace renderer_detail::vector_raster
