// Explicit portable vector implementation for mesh/SDL/null/backend recorders.
// Included inside pui after renderer vector geometry and raster preparation.
// Sources and textures are retained by the renderer; submission only draws quads.
// Preparation/release occur between frames; cache and batch storage are bounded.
namespace renderer_detail
{
constexpr usize VECTOR_CACHE_BYTES = 64 * 1024 * 1024, VECTOR_CACHE_ENTRIES = 256;
inline vector_status vector_failure(vector_error_code code, const char *message)
{
    return {{code, 0, message}};
}
inline u32 vector_palette(color c)
{
    return static_cast<u32>(c.r) | (static_cast<u32>(c.g) << 8) | (static_cast<u32>(c.b) << 16) |
           (static_cast<u32>(c.a) << 24);
}
inline usize vector_source_bytes(const vector_resource &resource)
{
    const auto &doc = resource.source;
    usize bytes = sizeof(resource) + doc.shapes.capacity() * sizeof(vector_detail::shape) +
                  doc.clips.capacity() * sizeof(vector_detail::clip) +
                  doc.operations.capacity() * sizeof(vector_detail::operation);
    const auto shape_bytes = [](const vector_detail::shape &shape)
    {
        return shape.commands.capacity() * sizeof(path_command) +
               (shape.fill.stops.capacity() + shape.outline.stops.capacity()) *
                   sizeof(gradient_stop) +
               shape.pen.dashes.capacity() * sizeof(f32) + shape.clips.capacity() * sizeof(u32);
    };
    for (const auto &shape : doc.shapes) bytes += shape_bytes(shape);
    for (const auto &clip : doc.clips)
    {
        bytes += clip.shapes.capacity() * sizeof(vector_detail::shape);
        for (const auto &shape : clip.shapes) bytes += shape_bytes(shape);
    }
    for (const auto &op : doc.operations) bytes += op.clips.capacity() * sizeof(u32);
    return bytes;
}
struct vector_cache
{
    struct entry
    {
        vector_handle source = nullptr;
        u64 identity = 0;
        u32 palette = 0;
        render_device *owner = nullptr;
        texture_handle texture = nullptr;
        vec2 max_pixels{};
        usize bytes = 0;
    };
    std::vector<entry> entries;
    usize bytes = 0;
    static bool less(const entry &a, std::pair<u64, u32> key)
    {
        return a.identity < key.first || (a.identity == key.first && a.palette < key.second);
    }
    auto find(vector_handle source, color palette)
    {
        return std::lower_bound(entries.begin(), entries.end(),
                                std::pair{source->identity, vector_palette(palette)}, less);
    }
    const entry *get(vector_handle source, color palette) const
    {
        if (!source) return nullptr;
        const u32 key = vector_palette(palette);
        const auto found = std::lower_bound(entries.begin(), entries.end(),
                                            std::pair{source->identity, key}, less);
        return found != entries.end() && found->identity == source->identity &&
                       found->palette == key
                   ? &*found
                   : nullptr;
    }
    static void release(entry &entry)
    {
        if (entry.texture && entry.owner) entry.owner->destroy_texture(entry.texture);
        vector_detail::release(entry.source);
        entry = {};
    }
    ~vector_cache()
    {
        for (auto &entry : entries) release(entry);
    }
};
struct vector_placement
{
    affine2 matrix{};
    rect visible{}, uv{}, bounds{};
    vec2 corners[4]{};
    vec2 pixel_size{};
    bool empty = false;
};
#if defined(PUFFERUI_VULKAN_VECTOR_CPU_TESTS)
inline thread_local u64 vector_placement_calls = 0;
#endif
inline vector_status place_vector(const context &ctx, const paint_vector &paint,
                                  vector_placement &out)
{
#if defined(PUFFERUI_VULKAN_VECTOR_CPU_TESTS)
    ++vector_placement_calls;
#endif
    if (!paint.image)
        return vector_failure(vector_error_code::INVALID_PATH, "null vector resource");
    const auto &options = paint.options;
    const auto &box = paint.image->source.view_box;
    if (!std::isfinite(paint.area.x) || !std::isfinite(paint.area.y) ||
        !std::isfinite(paint.area.w) || !std::isfinite(paint.area.h) || paint.area.w < 0 ||
        paint.area.h < 0 || !vector_detail::finite(options.transform) ||
        !std::isfinite(options.style.opacity) || options.style.opacity < 0 ||
        options.style.opacity > 1 || !std::isfinite(paint.scope_alpha) || paint.scope_alpha < 0 ||
        paint.scope_alpha > 1 || options.fit > vector_fit::STRETCH)
        return vector_failure(vector_error_code::INVALID_PATH, "invalid vector placement/style");
    if (paint.area.w == 0 || paint.area.h == 0 || box.w <= 0 || box.h <= 0 ||
        paint.image->source.operations.empty() || options.style.opacity == 0 ||
        options.style.tint.a == 0 || paint.scope_alpha == 0)
    {
        out.empty = true;
        return {};
    }
    const auto &document = paint.image->source;
    bool preserve = document.preserve_aspect, slice = document.slice;
    vec2 alignment = document.alignment;
    if (options.fit != vector_fit::SOURCE)
    {
        preserve = options.fit != vector_fit::STRETCH;
        slice = options.fit == vector_fit::COVER;
        alignment = {.5f, .5f};
    }
    const affine2 base = vector_detail::svg_viewport(box, paint.area, alignment, preserve, slice);
    out.matrix = vector_detail::multiply(options.transform, base);
    const rect mapped{base.a * box.x + base.e, base.d * box.y + base.f, base.a * box.w,
                      base.d * box.h};
    out.visible = rect::intersect(mapped, paint.area);
    if (!vector_detail::finite(out.matrix) || !std::isfinite(mapped.w) || !std::isfinite(mapped.h))
        return vector_failure(vector_error_code::LIMIT, "vector placement precision");
    if (mapped.w <= 0 || mapped.h <= 0 || out.visible.w <= 0 || out.visible.h <= 0)
    {
        out.empty = true;
        return {};
    }
    out.uv = {(out.visible.x - mapped.x) / mapped.w, (out.visible.y - mapped.y) / mapped.h,
              out.visible.w / mapped.w, out.visible.h / mapped.h};
    const auto &r = out.visible;
    out.corners[0] = options.transform.apply({r.x, r.y});
    out.corners[1] = options.transform.apply({r.right(), r.y});
    out.corners[2] = options.transform.apply({r.right(), r.bottom()});
    out.corners[3] = options.transform.apply({r.x, r.bottom()});
    vector_detail::bounds_builder bounds;
    for (auto p : out.corners)
    {
        if (!vector_detail::finite(p))
            return vector_failure(vector_error_code::LIMIT, "vector transformed bounds");
        bounds.add(p.x, p.y);
    }
    out.bounds = bounds.value();
    if (detail::outside_paint(&ctx, out.bounds))
    {
        out.empty = true;
        return {};
    }
    out.pixel_size = {static_cast<f32>(std::hypot(static_cast<f64>(out.matrix.a), out.matrix.b) *
                                       box.w * ctx.render_scale),
                      static_cast<f32>(std::hypot(static_cast<f64>(out.matrix.c), out.matrix.d) *
                                       box.h * ctx.render_scale)};
    if (!vector_detail::finite(out.pixel_size))
        return vector_failure(vector_error_code::LIMIT, "vector pixel extent precision");
    affine2 inverse;
    if (!vector_detail::inverse(out.matrix, inverse)) out.empty = true;
    return {};
}
inline color vector_tint(const paint_vector &paint)
{
    color tint = paint.options.style.tint;
    tint.a = static_cast<u8>(std::floor(tint.a * paint.options.style.opacity + .5f));
    tint.a = static_cast<u8>(tint.a * paint.scope_alpha);
    return tint;
}
inline vector_status vector_prepare_options(const vector_prepare_opts &options)
{
    if (!vector_detail::finite(options.max_pixel_size) || options.max_pixel_size.x <= 0 ||
        options.max_pixel_size.y <= 0 || options.max_pixel_size.x > 4096 ||
        options.max_pixel_size.y > 4096 || options.max_instances == 0 ||
        options.max_instances > 16384 || options.resource_budget_bytes > VECTOR_CACHE_BYTES ||
        options.resource_budget_bytes < 1024)
        return vector_failure(vector_error_code::LIMIT, "vector preparation dimensions/budget");
    return {};
}
mesh_painter::~mesh_painter()
{
    delete vector_images;
}
vector_status mesh_painter::prepare_vector(vector_handle source, const vector_prepare_opts &options)
try
{
    if (ctx->current_window)
        return vector_failure(vector_error_code::WRONG_CONTEXT,
                              "prepare_vector requires a closed frame");
    if (!source) return vector_failure(vector_error_code::INVALID_PATH, "null vector resource");
    if (!ctx->device)
        return vector_failure(vector_error_code::DEVICE_ERROR,
                              "vector preparation needs a render device");
    if (auto status = vector_prepare_options(options); !status) return status;
    if (source->source.operations.empty() || source->source.view_box.w <= 0 ||
        source->source.view_box.h <= 0)
        return {};
    vec2 size{std::ceil(options.max_pixel_size.x), std::ceil(options.max_pixel_size.y)};
    const auto *previous =
        vector_images ? vector_images->get(source, options.style.current_color) : nullptr;
    if (previous && previous->max_pixels.x >= size.x && previous->max_pixels.y >= size.y)
    {
        if (!prepare({options.max_instances * 4, options.max_instances * 6}))
            return vector_failure(vector_error_code::LIMIT, "vector batch preparation failed");
        vector_batch_hint = std::max(vector_batch_hint, options.max_instances);
        return {};
    }
    if (previous)
    {
        size.x = std::max(size.x, previous->max_pixels.x);
        size.y = std::max(size.y, previous->max_pixels.y);
    }
    const usize cost =
        vector_source_bytes(*source) + static_cast<usize>(size.x) * static_cast<usize>(size.y) * 4;
    const usize old = previous ? previous->bytes : 0,
                total = vector_images ? vector_images->bytes : 0;
    if (cost > options.resource_budget_bytes || cost > VECTOR_CACHE_BYTES - (total - old) ||
        (!previous && vector_images && vector_images->entries.size() >= VECTOR_CACHE_ENTRIES))
        return vector_failure(vector_error_code::LIMIT, "vector renderer cache budget");
    std::vector<u8> pixels;
    vector_error error;
    if (!vector_raster::raster_document(source->source, static_cast<i32>(size.x),
                                        static_cast<i32>(size.y), options.style.current_color,
                                        options.resource_budget_bytes, pixels, error))
        return {error};
    if (!prepare({options.max_instances * 4, options.max_instances * 6}))
        return vector_failure(vector_error_code::LIMIT, "vector batch preparation failed");
    if (!vector_images) vector_images = new vector_cache;
    // Allocate cache slots before creating device resources; failures keep old residency.
    vector_images->entries.reserve(vector_images->entries.size() + (previous ? 0 : 1));
    texture_handle texture = ctx->device->create_texture(static_cast<i32>(size.x),
                                                         static_cast<i32>(size.y), pixels.data());
    if (!texture)
        return vector_failure(vector_error_code::DEVICE_ERROR, "vector texture creation failed");
    try
    {
        ctx->device->set_texture_filter(texture, true);
    }
    catch (...)
    {
        ctx->device->destroy_texture(texture);
        throw;
    }
    auto found = vector_images->find(source, options.style.current_color);
    const bool replace = found != vector_images->entries.end() &&
                         found->identity == source->identity &&
                         found->palette == vector_palette(options.style.current_color);
    if (!vector_detail::retain(source))
    {
        ctx->device->destroy_texture(texture);
        return vector_failure(vector_error_code::LIMIT, "vector resource reference limit");
    }
    vector_cache::entry entry{
        source,      source->identity, vector_palette(options.style.current_color),
        ctx->device, texture,          size,
        cost};
    if (replace)
    {
        vector_cache::release(*found);
        *found = entry;
    }
    else
        vector_images->entries.insert(found, entry);
    vector_images->bytes = total - old + cost;
    vector_batch_hint = std::max(vector_batch_hint, options.max_instances);
    return {};
}
catch (const std::bad_alloc &)
{
    return vector_failure(vector_error_code::LIMIT, "vector preparation allocation failed");
}
vector_status mesh_painter::release_vector(vector_handle source)
{
    if (ctx->current_window)
        return vector_failure(vector_error_code::WRONG_CONTEXT,
                              "release_vector requires a closed frame");
    if (!source || !vector_images) return {};
    const u64 identity = source->identity;
    for (usize i = 0; i < vector_images->entries.size();)
    {
        auto &entry = vector_images->entries[i];
        if (entry.identity != identity)
        {
            ++i;
            continue;
        }
        vector_images->bytes -= entry.bytes;
        vector_cache::release(entry);
        vector_images->entries.erase(vector_images->entries.begin() +
                                     static_cast<std::ptrdiff_t>(i));
    }
    if (vector_images->entries.empty()) vector_batch_hint = 0;
    return {};
}
vector_status mesh_painter::vector_ready(const paint_vector &paint) const
{
    if (vector_proxy) return vector_proxy->vector_ready(paint);
    vector_placement placed;
    if (auto status = place_vector(*ctx, paint, placed); !status) return status;
    if (placed.empty) return {};
    const auto *entry = vector_images
                            ? vector_images->get(paint.image, paint.options.style.current_color)
                            : nullptr;
    if (!entry)
        return vector_failure(vector_error_code::NOT_PREPARED,
                              "vector/style is not prepared on this renderer");
    if (placed.pixel_size.x > entry->max_pixels.x + .001f ||
        placed.pixel_size.y > entry->max_pixels.y + .001f)
        return vector_failure(vector_error_code::NOT_PREPARED,
                              "vector draw exceeds its prepared pixel extent");
    return {};
}
vector_status mesh_painter::vector(const paint_vector &paint)
{
    if (!ctx->current_window)
        return vector_failure(vector_error_code::WRONG_CONTEXT,
                              "draw_vector requires an open frame");
    if (auto status = vector_ready(paint); !status) return status;
    vector_placement placed;
    if (auto status = place_vector(*ctx, paint, placed); !status) return status;
    if (placed.empty) return {};
    if (record_vector)
    {
        detail::mesh_flush(ctx);
        detail::dl_prepare(ctx, nullptr,
                           ctx->clip_depth > 0 ? &ctx->clip_stack[ctx->clip_depth - 1] : nullptr);
        paint_vector copy = paint;
        if (ctx->disabled_depth > 0)
            copy.scope_alpha *= std::clamp(ctx->disabled_alpha, 0.0f, 1.0f);
        return record_vector(vector_recorder, copy);
    }
    const auto *entry = vector_images->get(paint.image, paint.options.style.current_color);
    if (storage.verts.capacity() - storage.verts.size() < 4 ||
        storage.idx.capacity() - storage.idx.size() < 6)
        flush();
    const color tint = vector_tint(paint);
    const rect uv = placed.uv;
    const vertex v[4] = {{placed.corners[0].x, placed.corners[0].y, uv.x, uv.y, tint},
                         {placed.corners[1].x, placed.corners[1].y, uv.right(), uv.y, tint},
                         {placed.corners[2].x, placed.corners[2].y, uv.right(), uv.bottom(), tint},
                         {placed.corners[3].x, placed.corners[3].y, uv.x, uv.bottom(), tint}};
    constexpr i32 indices[6] = {0, 1, 2, 0, 2, 3};
    append_triangles(entry->texture, v, 4, indices, 6);
    return {};
}
} // namespace renderer_detail
