// Native vector source residency, ordered batches and isolated SVG groups.
// Allocation/encoding/pipeline creation belongs to explicit preparation.
// Immutable buffers retire at their last recorded submission serial.
// Included inside pui after vulkan_vector_pipeline.inl.
#if defined(PUFFERUI_ENABLE_VULKAN) && defined(PUFFERUI_ENABLE_HARFBUZZ)
namespace vk_detail
{
using vector_scene = renderer_detail::vector_slug::prepared_scene;
using vector_step = renderer_detail::vector_slug::scene_step;
static constexpr u32 VECTOR_NO_CLIP = renderer_detail::vector_slug::NO_CLIP;
static constexpr u32 VECTOR_EMPTY_CLIP = renderer_detail::vector_slug::EMPTY_CLIP;
struct vector_asset
{
    vector_buffer buffer;
    vector_scene scene;
    vector_handle source = nullptr;
    vec2 max_pixels{};
    usize references = 0, hint = 0, bytes = 0;
    u64 last_use = 0;
    explicit vector_asset(device &d) : buffer(d) {}
    ~vector_asset() { vector_detail::release(source); }
};
struct native_vector_bindings
{
    std::vector<vector_asset *> entries;
    vector_asset *get(vector_handle source) const
    {
        for (auto *asset : entries)
            if (asset->source == source) return asset;
        return nullptr;
    }
};
struct checked_vector_placement
{
    renderer_detail::vector_placement placed;
    vector_asset *asset = nullptr;
};
inline vector_status check_vector_placement(const context &ctx,
                                            const native_vector_bindings *bindings,
                                            const paint_vector &paint,
                                            checked_vector_placement &out)
{
    if (auto status = renderer_detail::place_vector(ctx, paint, out.placed); !status) return status;
    if (out.placed.empty) return {};
    out.asset = bindings ? bindings->get(paint.image) : nullptr;
    if (!out.asset || out.placed.pixel_size.x > out.asset->max_pixels.x + .001f ||
        out.placed.pixel_size.y > out.asset->max_pixels.y + .001f)
        return renderer_detail::vector_failure(vector_error_code::NOT_PREPARED,
                                               "native vector extent is not prepared");
    return {};
}
struct native_vectors
{
    static constexpr usize SCENE_BATCH = 16;
    struct scene_instance
    {
        affine2 matrix, inverse;
        rect viewport, bounds;
        color tint, current;
    };
    device &dev;
    vector_graphics graphics;
    std::vector<vector_asset *> assets;
    std::vector<vector_instance> pending;
    vector_asset *pending_asset = nullptr;
    VkRect2D pending_scissor{};
    bool flushing = false;
    usize scene_count = 0;
    std::vector<texture *> layers;
    struct retired_layer
    {
        u64 serial;
        usize bytes;
    };
    std::vector<retired_layer> retired_layers;
    usize bytes = 0, layer_bytes = 0, retired_bytes = 0;
    VkDeviceSize requested_upload = 0;
    i32 layer_side = 0;
    u32 layer_columns = 1;
    // Keep frequently read queue/clip/layer state together. Tile scratch is
    // cold for the simple-path route and belongs after those hot fields.
    scene_instance scenes[SCENE_BATCH]{};
    rect group_rois[66][SCENE_BATCH]{};
    explicit native_vectors(device &d) : dev(d), graphics(d)
    {
        assets.reserve(renderer_detail::VECTOR_CACHE_ENTRIES);
        layers.reserve(65);
        retired_layers.reserve(256);
    }
    ~native_vectors()
    {
        for (auto *asset : assets) delete asset;
        for (auto *layer : layers) dev.destroy_target(layer);
    }
    usize usage() const
    {
        return bytes + layer_bytes + retired_bytes + pending.capacity() * sizeof(vector_instance) +
               sizeof(scenes) + sizeof(group_rois);
    }
    void collect(u64 done)
    {
        for (usize i = 0; i < assets.size();)
            if (!assets[i]->references && assets[i]->last_use <= done)
            {
                bytes -= assets[i]->bytes;
                delete assets[i];
                assets.erase(assets.begin() + static_cast<std::ptrdiff_t>(i));
            }
            else
                ++i;
        for (usize i = 0; i < retired_layers.size();)
            if (retired_layers[i].serial <= done)
            {
                retired_bytes -= retired_layers[i].bytes;
                retired_layers.erase(retired_layers.begin() + static_cast<std::ptrdiff_t>(i));
            }
            else
                ++i;
    }
    void trim_layers()
    {
        for (auto *layer : layers)
        {
            retired_layers.push_back({layer->last_use, static_cast<usize>(layer->allocation)});
            retired_bytes += layer->allocation;
            dev.destroy_target(layer);
        }
        layers.clear();
        layer_side = 0;
        layer_columns = 1;
        layer_bytes = 0;
    }
    bool reserve_layers(u32 depth, i32 side, usize extra, usize instances, usize budget)
    {
        if (!depth) return true;
        const usize count = std::max<usize>(depth, layers.size());
        side = std::max(side, layer_side);
        u32 columns = 1;
        while ((columns + 1) * (columns + 1) <= std::min(instances, SCENE_BATCH)) ++columns;
        const usize tile_bytes = count * static_cast<usize>(side) * side * 4;
        // Tiling is optional: reduce batch width before declining an otherwise
        // valid single-instance preparation. Bound both transient residency and
        // this source's explicit resource budget, including actual alignment.
        const usize available = renderer_detail::VECTOR_CACHE_BYTES -
                                std::min(usage(), renderer_detail::VECTOR_CACHE_BYTES);
        while (columns > 1 &&
               (tile_bytes * columns * columns > budget - std::min(extra, budget) ||
                extra > available || tile_bytes * columns * columns > available - extra ||
                static_cast<u64>(side) * columns > dev.properties.limits.maxImageDimension2D))
            --columns;
        if (side == layer_side && depth <= layers.size() && columns <= layer_columns) return true;
        const usize estimated = tile_bytes * columns * columns;
        const bool replace = side != layer_side || columns != layer_columns;
        const usize growth = replace ? estimated
                                     : (count - layers.size()) * static_cast<usize>(side) * side *
                                           columns * columns * 4;
        if (extra > renderer_detail::VECTOR_CACHE_BYTES -
                        std::min(usage(), renderer_detail::VECTOR_CACHE_BYTES) ||
            growth > renderer_detail::VECTOR_CACHE_BYTES - usage() - extra)
            return false;
        std::vector<texture *> fresh;
        fresh.reserve(replace ? count : count - layers.size());
        retired_layers.reserve(retired_layers.size() + layers.size());
        dev.retired.reserve(dev.retired.size() + layers.size() + fresh.capacity());
        usize allocated = 0;
        try
        {
            for (usize i = replace ? 0 : layers.size(); i < count; ++i)
            {
                auto *target = static_cast<texture *>(dev.create_target(
                    side * static_cast<i32>(columns), side * static_cast<i32>(columns)));
                if (!target)
                {
                    for (auto *made : fresh) dev.destroy_target(made);
                    return false;
                }
                // A new empty target has not been referenced by GPU commands.
                target->last_use = 0;
                fresh.push_back(target);
                allocated += target->allocation;
            }
        }
        catch (...)
        {
            for (auto *made : fresh) dev.destroy_target(made);
            throw;
        }
        if (allocated > renderer_detail::VECTOR_CACHE_BYTES - usage() - extra ||
            (columns > 1 && allocated > budget - std::min(extra, budget)))
        {
            for (auto *made : fresh) dev.destroy_target(made);
            if (columns > 1)
            {
                // These fresh, unrecorded images have serial zero. Reclaim
                // their actual aligned allocations before trying fewer tiles.
                dev.collect();
                return reserve_layers(depth, side, extra, (columns - 1) * (columns - 1), budget);
            }
            return false;
        }
        if (replace) trim_layers();
        layers.insert(layers.end(), fresh.begin(), fresh.end());
        layer_side = side;
        layer_columns = columns;
        layer_bytes += allocated;
        return true;
    }
    void refresh_upload_hint()
    {
        requested_upload = 0;
        for (auto *asset : assets)
            if (asset->references && !asset->scene.steps.empty())
                requested_upload +=
                    asset->hint * (asset->scene.quads + 1ull) * sizeof(vector_instance);
    }
    bool flush()
    {
        if (pending.empty() && !scene_count) return true;
        if (flushing) return true;
        flushing = true;
        if (dev.cache_active) dev.cache_flush(false);
        const bool okay = scene_count
                              ? draw_scenes()
                              : graphics.draw(dev.white, pending_asset->buffer.descriptor,
                                              pending.data(), pending.size(), pending_scissor);
        pending_asset->last_use = std::max(pending_asset->last_use, dev.next_serial);
        pending.clear();
        scene_count = 0;
        pending_asset = nullptr;
        flushing = false;
        return okay;
    }
    bool queue(vector_asset &asset, const vector_instance &instance, VkRect2D scissor)
    {
        // Immutable assets cannot be both a single-paint source and an isolated
        // scene. A mode change therefore already changes pending_asset. Reuse
        // that discriminator instead of testing scene state for every simple
        // instance; the empty queue has a null asset and flushes as a no-op.
        if (pending_asset != &asset ||
            std::memcmp(&pending_scissor, &scissor, sizeof(scissor)) != 0 ||
            pending.size() == pending.capacity())
            if (!flush()) return false;
        if (pending.capacity() == 0) return false;
        pending_asset = &asset;
        pending_scissor = scissor;
        pending.push_back(instance);
        return true;
    }
    bool draw_scene(vector_asset &, const paint_vector &, const renderer_detail::vector_placement &,
                    context &);
    bool queue_scene(vector_asset &, const scene_instance &, VkRect2D);
    bool draw_scenes();
};
void device::flush_paths()
{
    if (paths && !paths->flushing) paths->flush();
}
void device::collect_paths(u64 done)
{
    paths->collect(done);
}
void device::release_paths()
{
    auto *owned = paths;
    paths = nullptr;
    delete owned;
}
bool device::prepare_path_slot(frame_slot &slot)
{
    if (!paths->requested_upload || paint_depth) return true;
    const VkDeviceSize pixels =
        target ? static_cast<VkDeviceSize>(target->width) * target->height : 1024ull * 1024;
    const VkDeviceSize bytes = geometry_reservation(paths->requested_upload + 512 * 1024, pixels);
    if (!bytes) return false;
    if (slot.geometry_chunk_bytes >= bytes) return true;
    return prepare_slot_geometry(slot, bytes);
}
inline VkRect2D vector_scissor(rect area, texture &target)
{
    const f64 x = std::trunc(static_cast<f64>(area.x)), y = std::trunc(static_cast<f64>(area.y));
    const f64 left = std::clamp(x, 0.0, static_cast<f64>(target.width)),
              top = std::clamp(y, 0.0, static_cast<f64>(target.height));
    const f64 right =
        std::clamp(x + std::trunc(static_cast<f64>(area.w)), left, static_cast<f64>(target.width));
    const f64 bottom =
        std::clamp(y + std::trunc(static_cast<f64>(area.h)), top, static_cast<f64>(target.height));
    return {{static_cast<i32>(left), static_cast<i32>(top)},
            {static_cast<u32>(right - left), static_cast<u32>(bottom - top)}};
}
inline rect vector_pixel_bounds(rect area, affine2 matrix)
{
    vector_detail::bounds_builder bounds;
    const vec2 corners[] = {{area.x, area.y},
                            {area.right(), area.y},
                            {area.right(), area.bottom()},
                            {area.x, area.bottom()}};
    for (auto p : corners)
    {
        p = matrix.apply(p);
        bounds.add(p.x, p.y);
    }
    auto result = bounds.value();
    return {result.x - 1.5f, result.y - 1.5f, result.w + 3, result.h + 3};
}
inline vector_instance vector_packet(rect bounds, const affine2 &inverse, rect viewport, color tint,
                                     color current, vec2 parent_origin, vec2 sample_origin,
                                     u32 path, u32 paint, u32 clip, u32 flags)
{
    return {{bounds.x - parent_origin.x, bounds.y - parent_origin.y,
             bounds.right() - parent_origin.x, bounds.bottom() - parent_origin.y},
            {inverse.a, inverse.c, inverse.e, 0},
            {inverse.b, inverse.d, inverse.f, 0},
            {viewport.x, viewport.y, viewport.right(), viewport.bottom()},
            {tint.r / 255.0f, tint.g / 255.0f, tint.b / 255.0f, tint.a / 255.0f},
            {current.r / 255.0f, current.g / 255.0f, current.b / 255.0f, current.a / 255.0f},
            {parent_origin.x, parent_origin.y, sample_origin.x, sample_origin.y},
            {path, paint, clip, flags}};
}
#include "vulkan_vector_batch.inl"
bool native_vectors::draw_scene(vector_asset &asset, const paint_vector &paint,
                                const renderer_detail::vector_placement &placed, context &ctx)
{
    const f32 scale = ctx.render_scale;
    const affine2 &m = placed.matrix;
    const affine2 matrix{m.a * scale, m.b * scale, m.c * scale,
                         m.d * scale, m.e * scale, m.f * scale};
    affine2 inverse;
    if (!vector_detail::inverse(matrix, inverse)) return true;
    const auto box = asset.source->source.view_box;
    const rect viewport{box.x + placed.uv.x * box.w, box.y + placed.uv.y * box.h,
                        placed.uv.w * box.w, placed.uv.h * box.h};
    const auto source_visible = rect::intersect(asset.scene.bounds, viewport);
    if (source_visible.w <= 0 || source_visible.h <= 0) return true;
    auto tint = renderer_detail::vector_tint(paint);
    if (ctx.disabled_depth > 0)
        tint.a = static_cast<u8>(tint.a * std::clamp(ctx.disabled_alpha, 0.0f, 1.0f));
    if (!tint.a || asset.scene.steps.empty()) return true;
    rect clip;
    if (!detail::paint_bounds(&ctx, clip)) return true;
    clip = {clip.x * scale, clip.y * scale, clip.w * scale, clip.h * scale};
    if (!dev.valid(dev.target) || !graphics.get(dev.target->format)) return false;
    if (!dev.has_clip || dev.clip.x != clip.x || dev.clip.y != clip.y || dev.clip.w != clip.w ||
        dev.clip.h != clip.h)
        dev.set_clip(&clip);
    const auto scissor = vector_scissor(clip, *dev.target);
    if (!scissor.extent.width || !scissor.extent.height) return true;
    if (asset.scene.steps.size() == 1 && asset.scene.steps[0].type == vector_step::PAINT)
    {
        const auto &step = asset.scene.steps[0];
        const rect bounds = vector_pixel_bounds(step.bounds, matrix);
        return queue(asset,
                     vector_packet(bounds, inverse, viewport, tint,
                                   paint.options.style.current_color, {}, {}, step.path, step.paint,
                                   step.clip, 0),
                     scissor);
    }
    // Isolating the complete source makes each tile independent. Only the final
    // composites touch the caller's target, in the original submission order.
    // Opaque/non-isolated scenes retain the original ordered immediate route.
    if (tint.a != 255 && layer_columns > 1)
    {
        const rect bounds = vector_group_roi(asset.scene.bounds, viewport, matrix, scissor);
        if (bounds.w <= 0 || bounds.h <= 0) return true;
        if (bounds.w <= layer_side && bounds.h <= layer_side)
            return queue_scene(
                asset, {matrix, inverse, viewport, bounds, tint, paint.options.style.current_color},
                scissor);
    }
    if (!flush()) return false;
    dev.flush_text(true);
    if (dev.cache_active) dev.cache_flush(false);
    const bool previous_flushing = flushing;
    flushing = true;
    struct restore_state
    {
        native_vectors &owner;
        texture *target;
        bool clipped;
        rect clip;
        bool was_flushing;
        ~restore_state()
        {
            owner.dev.set_target(target);
            owner.dev.set_clip(clipped ? &clip : nullptr);
            owner.flushing = was_flushing;
        }
    } restore{*this, dev.target, dev.has_clip, dev.clip, previous_flushing};
    struct group_frame
    {
        texture *parent = nullptr, *image = nullptr;
        vec2 parent_origin{}, origin{};
        rect roi{};
        VkRect2D parent_scissor{};
    };
    group_frame groups[66]{};
    usize depth = 0;
    vec2 origin{};
    VkRect2D active_scissor = scissor;
    rect root_clip{static_cast<f32>(scissor.offset.x), static_cast<f32>(scissor.offset.y),
                   static_cast<f32>(scissor.extent.width), static_cast<f32>(scissor.extent.height)};
    const auto push = [&](rect bounds) -> bool
    {
        if (depth >= layers.size()) return false;
        bounds = rect::intersect(vector_pixel_bounds(rect::intersect(bounds, viewport), matrix),
                                 root_clip);
        const f32 left = std::floor(bounds.x), top = std::floor(bounds.y),
                  right = std::ceil(bounds.right()), bottom = std::ceil(bounds.bottom());
        if (right <= left || bottom <= top) return false;
        const i32 width = static_cast<i32>(right - left), height = static_cast<i32>(bottom - top);
        if (width > layer_side || height > layer_side) return false;
        auto &frame = groups[depth];
        frame = {dev.target,
                 layers[depth],
                 origin,
                 {left, top},
                 {left, top, right - left, bottom - top},
                 active_scissor};
        ++depth;
        origin = frame.origin;
        active_scissor = {{0, 0}, {static_cast<u32>(width), static_cast<u32>(height)}};
        dev.set_target(frame.image);
        dev.set_clip(nullptr);
        if (!dev.start_rendering(frame.image)) return false;
        VkClearAttachment clear{};
        clear.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        const VkClearRect region{
            {{0, 0}, {static_cast<u32>(width), static_cast<u32>(height)}}, 0, 1};
        dev.vkCmdClearAttachments(dev.frame->command, 1, &clear, 1, &region);
        frame.image->last_use = dev.next_serial;
        ++frame.image->revision;
        return true;
    };
    const auto draw = [&](texture *sample, const vector_instance &instance) -> bool
    {
        const bool okay =
            graphics.draw(sample, asset.buffer.descriptor, &instance, 1, active_scissor);
        asset.last_use = std::max(asset.last_use, dev.next_serial);
        return okay;
    };
    const auto pop = [&](u32 clip_id, f32 opacity) -> bool
    {
        if (!depth) return false;
        const auto frame = groups[--depth];
        dev.set_target(frame.parent);
        origin = frame.parent_origin;
        active_scissor = frame.parent_scissor;
        auto instance = vector_packet(frame.roi, inverse, viewport, {255, 255, 255, 255},
                                      paint.options.style.current_color, origin, frame.origin,
                                      VECTOR_NO_CLIP, VECTOR_NO_CLIP, clip_id, 1);
        instance.tint[3] = opacity;
        return draw(frame.image, instance);
    };
    const bool root_isolated = tint.a != 255;
    const u8 root_alpha = tint.a;
    if (root_isolated)
    {
        if (!push(asset.scene.bounds)) return false;
        tint.a = 255;
    }
    for (usize i = 0; i < asset.scene.steps.size(); ++i)
    {
        const auto &step = asset.scene.steps[i];
        if (step.type == vector_step::PUSH)
        {
            const auto visible = rect::intersect(
                vector_pixel_bounds(rect::intersect(step.bounds, viewport), matrix), root_clip);
            if (step.opacity == 0 || step.clip == VECTOR_EMPTY_CLIP || visible.w <= 0 ||
                visible.h <= 0)
            {
                i = step.pair;
                continue;
            }
            if (!push(step.bounds)) return false;
        }
        else if (step.type == vector_step::POP)
        {
            if (!pop(step.clip, step.opacity)) return false;
        }
        else
        {
            const auto instance = vector_packet(vector_pixel_bounds(step.bounds, matrix), inverse,
                                                viewport, tint, paint.options.style.current_color,
                                                origin, {}, step.path, step.paint, step.clip, 0);
            if (!draw(dev.white, instance)) return false;
        }
    }
    if (root_isolated && !pop(VECTOR_NO_CLIP, root_alpha / 255.0f)) return false;
    asset.last_use = dev.next_serial;
    return depth == 0;
}
native_painter::~native_painter()
{
    if (path_bindings)
    {
        dev->flush_paths();
        for (auto *asset : path_bindings->entries) --asset->references;
        delete path_bindings;
        dev->paths->refresh_upload_hint();
        if (!dev->paths->requested_upload) dev->paths->trim_layers();
    }
}
void native_painter::flush()
{
    mesh_painter::flush();
    if (dev->paths) dev->flush_paths();
}
usize native_painter::vector_capacity() const
{
    return std::max(vector_batch_hint, mesh_painter::vector_capacity());
}
vector_status native_painter::prepare_vector(vector_handle source,
                                             const vector_prepare_opts &options)
try
{
    using namespace renderer_detail;
    if (!dev->options.vector_paths) return mesh_painter::prepare_vector(source, options);
    if (ctx->current_window)
        return vector_failure(vector_error_code::WRONG_CONTEXT,
                              "prepare_vector requires a closed frame");
    if (!source) return vector_failure(vector_error_code::INVALID_PATH, "null vector resource");
    if (auto status = vector_prepare_options(options); !status) return status;
    if (source->source.operations.empty() || source->source.view_box.w <= 0 ||
        source->source.view_box.h <= 0)
        return {};
    if (dev->result != VK_SUCCESS)
        return vector_failure(vector_error_code::DEVICE_ERROR, "Vulkan device is not ready");
    if (!dev->paths) dev->paths = new native_vectors(*dev);
    auto &state = *dev->paths;
    if (!state.flush())
        return vector_failure(vector_error_code::DEVICE_ERROR, "native vector preparation flush");
    state.collect(dev->completed());
    if (!path_bindings) path_bindings = new native_vector_bindings;
    auto *previous = path_bindings->get(source);
    vector_prepare_opts requested = options;
    if (previous)
    {
        requested.max_pixel_size.x = std::max(requested.max_pixel_size.x, previous->max_pixels.x);
        requested.max_pixel_size.y = std::max(requested.max_pixel_size.y, previous->max_pixels.y);
    }
    const usize batch_capacity = std::max<usize>(64, requested.max_instances);
    if (batch_capacity > state.pending.capacity())
    {
        std::vector<vector_instance> grown;
        grown.reserve(batch_capacity);
        const usize growth =
            (grown.capacity() - state.pending.capacity()) * sizeof(vector_instance);
        if (growth > VECTOR_CACHE_BYTES - std::min(state.usage(), VECTOR_CACHE_BYTES))
            return vector_failure(vector_error_code::LIMIT, "native vector batch storage budget");
        state.pending.swap(grown);
    }
    path_bindings->entries.reserve(path_bindings->entries.size() + (previous ? 0 : 1));
    vector_asset *asset = nullptr;
    for (auto *candidate : state.assets)
        if (candidate->source == source && candidate->max_pixels.x >= requested.max_pixel_size.x &&
            candidate->max_pixels.y >= requested.max_pixel_size.y)
        {
            asset = candidate;
            break;
        }
    if (!asset)
    {
        if (state.assets.size() == VECTOR_CACHE_ENTRIES)
            return vector_failure(vector_error_code::LIMIT, "native vector asset count");
        auto *fresh = new vector_asset(*dev);
        try
        {
            vector_error error;
            if (!vector_slug::prepare_scene(source->source, requested, fresh->scene, error))
            {
                delete fresh;
                return {error};
            }
            const usize source_bytes = vector_source_bytes(*source);
            const usize estimate = source_bytes + sizeof(vector_asset) +
                                   fresh->scene.steps.capacity() * sizeof(vector_step) +
                                   fresh->scene.words.size() * 4;
            const u32 depth = fresh->scene.max_depth + (fresh->scene.quads > 1 ? 1 : 0);
            const i32 side = static_cast<i32>(
                std::ceil(requested.max_pixel_size.x + requested.max_pixel_size.y + 4));
            const usize scratch = depth * static_cast<usize>(side) * side * 4;
            if (estimate > requested.resource_budget_bytes ||
                scratch > requested.resource_budget_bytes - estimate ||
                estimate > VECTOR_CACHE_BYTES - std::min(state.usage(), VECTOR_CACHE_BYTES))
            {
                delete fresh;
                return vector_failure(vector_error_code::LIMIT, "native vector residency budget");
            }
            if (!fresh->scene.words.empty() &&
                (!state.graphics.prepare(VK_FORMAT_R8G8B8A8_UNORM) ||
                 !fresh->buffer.prepare(fresh->scene, state.graphics)))
            {
                delete fresh;
                return vector_failure(vector_error_code::DEVICE_ERROR,
                                      "native vector buffer/pipeline preparation");
            }
            fresh->bytes = source_bytes + sizeof(vector_asset) +
                           fresh->scene.steps.capacity() * sizeof(vector_step) +
                           static_cast<usize>(fresh->buffer.storage.allocation);
            if (fresh->bytes > requested.resource_budget_bytes ||
                fresh->bytes > VECTOR_CACHE_BYTES - std::min(state.usage(), VECTOR_CACHE_BYTES) ||
                !state.reserve_layers(depth, side, fresh->bytes, requested.max_instances,
                                      requested.resource_budget_bytes))
            {
                delete fresh;
                return vector_failure(vector_error_code::LIMIT, "native vector group reservation");
            }
            std::vector<u32>().swap(fresh->scene.words);
            if (!vector_detail::retain(source))
            {
                delete fresh;
                return vector_failure(vector_error_code::LIMIT, "vector resource reference limit");
            }
            fresh->source = source;
            fresh->max_pixels = requested.max_pixel_size;
            state.assets.push_back(fresh);
            state.bytes += fresh->bytes;
            asset = fresh;
        }
        catch (...)
        {
            delete fresh;
            throw;
        }
    }
    const auto hint_bytes = [](const vector_asset &value, usize count) -> VkDeviceSize
    {
        return value.scene.steps.empty()
                   ? 0
                   : count * (value.scene.quads + 1ull) * sizeof(vector_instance);
    };
    const VkDeviceSize hint = hint_bytes(*asset, std::max(asset->hint, requested.max_instances));
    VkDeviceSize prospective = state.requested_upload;
    if (previous && previous != asset && previous->references == 1)
        prospective -= hint_bytes(*previous, previous->hint);
    if (asset->references) prospective -= hint_bytes(*asset, asset->hint);
    prospective += hint;
    const VkDeviceSize limit =
        dev->options.upload_budget_bytes /
        (dev->options.frames_in_flight * 4ull * std::max<usize>(1, dev->surfaces.size()) *
         (dev->options.upload_mode == vulkan_upload_mode::STAGED ? 2 : 1));
    if (prospective > limit)
        return vector_failure(vector_error_code::LIMIT, "native vector instance hint budget");
    struct upload_reservation
    {
        native_vectors &state;
        bool &preparing;
        VkDeviceSize previous;
        bool was_preparing, committed = false;
        upload_reservation(native_vectors &s, VkDeviceSize wanted)
            : state(s), preparing(s.dev.preparing_geometry), previous(s.requested_upload),
              was_preparing(preparing)
        {
            state.requested_upload = wanted;
            preparing = true;
        }
        ~upload_reservation()
        {
            preparing = was_preparing;
            if (!committed) state.requested_upload = previous;
        }
    } reservation(state, prospective);
    // Prepare idle slots now; busy slots reserve at their next safe boundary.
    for (auto *surface : dev->surfaces)
        for (auto &slot : surface->slots)
            if (!slot.recording && slot.serial <= dev->known_completed &&
                slot.group_serial <= dev->known_completed)
            {
                for (auto &page : slot.pages)
                    if (page.geometry) page.used = 0;
                if (!dev->prepare_path_slot(slot))
                {
                    return vector_failure(vector_error_code::DEVICE_ERROR,
                                          "native vector upload reservation");
                }
            }
    asset->hint = std::max(asset->hint, requested.max_instances);
    if (previous != asset)
    {
        ++asset->references;
        if (previous)
        {
            for (auto *&binding : path_bindings->entries)
                if (binding == previous)
                {
                    binding = asset;
                    break;
                }
            --previous->references;
        }
        else
            path_bindings->entries.push_back(asset);
    }
    vector_batch_hint = std::max(vector_batch_hint, requested.max_instances);
    state.refresh_upload_hint();
    reservation.committed = true;
    return {};
}
catch (const std::bad_alloc &)
{
    return renderer_detail::vector_failure(vector_error_code::LIMIT,
                                           "native vector preparation allocation");
}
vector_status native_painter::release_vector(vector_handle source)
{
    using namespace renderer_detail;
    if (!dev->options.vector_paths) return mesh_painter::release_vector(source);
    if (ctx->current_window)
        return vector_failure(vector_error_code::WRONG_CONTEXT,
                              "release_vector requires a closed frame");
    if (!path_bindings || !source) return {};
    dev->flush_paths();
    for (usize i = 0; i < path_bindings->entries.size(); ++i)
        if (path_bindings->entries[i]->source == source)
        {
            --path_bindings->entries[i]->references;
            path_bindings->entries.erase(path_bindings->entries.begin() +
                                         static_cast<std::ptrdiff_t>(i));
            break;
        }
    vector_batch_hint = 0;
    for (auto *asset : path_bindings->entries)
        vector_batch_hint = std::max(vector_batch_hint, asset->hint);
    dev->paths->refresh_upload_hint();
    if (!dev->paths->requested_upload) dev->paths->trim_layers();
    return {};
}
vector_status native_painter::vector_ready(const paint_vector &paint) const
{
    if (!dev->options.vector_paths) return mesh_painter::vector_ready(paint);
    checked_vector_placement checked;
    return check_vector_placement(*ctx, path_bindings, paint, checked);
}
vector_status native_painter::vector(const paint_vector &paint)
{
    using namespace renderer_detail;
    if (!dev->options.vector_paths) return mesh_painter::vector(paint);
    if (!ctx->current_window)
        return vector_failure(vector_error_code::WRONG_CONTEXT,
                              "draw_vector requires an open frame");
    // Readiness and submission consume one checked placement. Prepared sources
    // cannot be released during an open frame, including while batches flush.
    checked_vector_placement checked;
    if (auto status = check_vector_placement(*ctx, path_bindings, paint, checked); !status)
        return status;
    if (checked.placed.empty) return {};
    if (dev->current_surface && dev->current_surface->suspended &&
        (!dev->target || dev->target == dev->current_surface->scene))
        return {};
    detail::mesh_flush(ctx);
    dev->flush_text(true);
    const bool drawn = dev->paths->draw_scene(*checked.asset, paint, checked.placed, *ctx);
    // Native clipping/targets bypass the mesh recorder. Its next semantic
    // primitive must re-establish the current logical clip, even after a pop.
    invalidate();
    if (!drawn)
        return vector_failure(vector_error_code::DEVICE_ERROR, "native vector drawing failed");
    return {};
}
} // namespace vk_detail
#endif
