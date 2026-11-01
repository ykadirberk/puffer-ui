// Native Vulkan backdrop filtering; the shared mesh/SDL blur stays independent.
// Small kernels use separable Gaussian taps, wide kernels use Dual Kawase.
// Two bounded region targets are retained per window and reused between calls.
// Included inside namespace pui by the single implementation TU.
#if defined(PUFFERUI_ENABLE_VULKAN)
namespace vk_detail
{
struct blur_plan
{
    bool gaussian = true;
    u32 levels = 0;
    f32 offset = 0;
    blur_filter taps{};
};
inline blur_plan plan_blur(f32 radius)
{
    blur_plan plan;
    plan.gaussian = radius <= GAUSSIAN_RADIUS_LIMIT;
    if (plan.gaussian)
    {
        // Pair adjacent symmetric taps so the sampler performs their weighted
        // interpolation. At radius fifteen each axis needs 17 fetches, not 31.
        const f32 sigma = std::max(0.5f, radius / 3.0f);
        const i32 support = static_cast<i32>(std::ceil(radius));
        f32 weights[GAUSSIAN_PAIR_COUNT * 2 + 1]{1};
        f32 sum = 1;
        for (i32 i = 1; i <= support; ++i)
        {
            weights[i] = std::exp(-static_cast<f32>(i * i) / (2.0f * sigma * sigma));
            sum += 2 * weights[i];
        }
        plan.taps.weights[0] = 1 / sum;
        for (i32 pair = 0; pair < GAUSSIAN_PAIR_COUNT; ++pair)
        {
            const i32 first = pair * 2 + 1;
            const f32 weight = weights[first] + weights[first + 1];
            plan.taps.weights[pair + 1] = weight / sum;
            plan.taps.offsets[pair] = weight > 0 ? first + weights[first + 1] / weight : 0;
        }
    }
    else
    {
        // Keep the sample spacing near one texel as the pyramid gets deeper.
        // Six levels bound command count; larger radii increase spacing.
        plan.levels = 1;
        while (plan.levels < 6 && radius > static_cast<f32>(4u << plan.levels)) ++plan.levels;
        const f32 variance = (35.0f / 72.0f) * ((1u << (2 * plan.levels)) - 1);
        plan.offset = radius / (3 * std::sqrt(variance));
    }
    return plan;
}
void device::filter(texture *source, rect source_area, texture *destination, rect destination_area,
                    blur_kernel kernel, blur_filter parameters, rect valid_source)
{
    if (!valid(source) || !valid(destination) || source == destination) return;
    parameters.low[0] = (valid_source.x + 0.5f) / source->width;
    parameters.low[1] = (valid_source.y + 0.5f) / source->height;
    parameters.high[0] = (valid_source.right() - 0.5f) / source->width;
    parameters.high[1] = (valid_source.bottom() - 0.5f) / source->height;
    paint_filter(source, source_area, destination, destination_area, {kernel, parameters});
}
void device::paint_filter(texture *source, rect source_area, texture *destination,
                          rect destination_area, const blur_pass &pass)
{
    set_target(destination);
    set_clip(nullptr);
    if (cache_active && cache_add(cache_event::FILTER, source, nullptr, 0, nullptr, 0, {},
                                  &source_area, &destination_area, &pass))
        return;
    const f32 left = source_area.x / source->width, top = source_area.y / source->height;
    const f32 right = source_area.right() / source->width,
              bottom = source_area.bottom() / source->height;
    const color white_color{255, 255, 255, 255};
    const vertex vertices[] = {
        {destination_area.x, destination_area.y, left, top, white_color},
        {destination_area.right(), destination_area.y, right, top, white_color},
        {destination_area.right(), destination_area.bottom(), right, bottom, white_color},
        {destination_area.x, destination_area.bottom(), left, bottom, white_color}};
    const i32 indices[] = {0, 1, 2, 0, 2, 3};
    paint(source, vertices, 4, indices, 6, false, 0, &pass.parameters,
          static_cast<u32>(pass.kernel));
}
struct native_painter : renderer_detail::mesh_painter
{
    device *dev;
    f32 gaussian_radius = -1;
    blur_plan gaussian_plan{};
    native_painter(context &c, device &d) : mesh_painter(c), dev(&d) {}
    void begin() override
    {
        mesh_painter::begin();
        render_surface *surface = ctx->current_window ? ctx->current_window->surface : ctx->surface;
        if (!surface || !ctx->blur || ctx->overlay_depth) return;
        auto *scene = static_cast<texture *>(surface->scene_target());
        if (!scene) return;
        auto &scratch = *ctx->blur;
        if (scratch.w > scene->width || scratch.h > scene->height)
        {
            // Retire oversized images at the frame boundary after a shrink.
            if (scratch.half) dev->destroy_target(scratch.half);
            if (scratch.quarter) dev->destroy_target(scratch.quarter);
            scratch = {};
        }
    }
    bool prepare_blur(i32 width, i32 height, i32 output_width, i32 output_height, bool gaussian)
    {
        auto &scratch = *ctx->blur;
        // First use/growth is lazy. Later calls overwrite their entire valid
        // region and clamp every sample to it; no clears or stale-edge reads.
        width = std::max(scratch.w, std::min(output_width, (width + 31) / 32 * 32));
        height = std::max(scratch.h, std::min(output_height, (height + 31) / 32 * 32));
        auto *old_a = static_cast<texture *>(scratch.half);
        const i32 aw = gaussian ? width : (width + 1) / 2;
        const i32 ah = gaussian ? height : (height + 1) / 2;
        const bool grow_a = !old_a || old_a->width < aw || old_a->height < ah;
        const bool grow_b = !scratch.quarter || scratch.w < width || scratch.h < height;
        texture_handle a = grow_a ? dev->create_target(aw, ah) : scratch.half;
        texture_handle b = grow_b && a ? dev->create_target(width, height) : scratch.quarter;
        if (!a || !b)
        {
            if (grow_a && a) dev->destroy_target(a);
            return false;
        }
        if (grow_a && scratch.half) dev->destroy_target(scratch.half);
        if (grow_b && scratch.quarter) dev->destroy_target(scratch.quarter);
        scratch.half = a;
        scratch.quarter = b;
        scratch.w = width;
        scratch.h = height;
        return true;
    }
    void blur(const paint_blur &p) override
    {
        if (!std::isfinite(p.radius) || !std::isfinite(p.alpha) || p.radius < 0.5f ||
            p.alpha <= 0 || ctx->overlay_depth > 0)
            return;
        PUFFERUI_CHECK(VIOL_INVALID_RECT, p.area.is_valid(), "blur with invalid rect");
        if (!p.area.is_valid() || detail::outside_paint(ctx, p.area)) return;
        render_surface *surface = ctx->current_window ? ctx->current_window->surface : ctx->surface;
        if (!surface || !ctx->blur) return;
        auto *scene = static_cast<texture *>(surface->scene_target());
        if (!scene || (dev->current_surface && dev->current_surface->suspended)) return;
        const i32 ow = scene->width, oh = scene->height;
        const f32 density = ctx->render_scale;
        if (ow <= 2 || oh <= 2 || !std::isfinite(density) || density <= 0) return;
        const f32 radius = static_cast<f32>(
            std::min(static_cast<double>(p.radius) * density, 2.0 * std::max(ow, oh) + 8));
        blur_plan plan;
        if (radius <= GAUSSIAN_RADIUS_LIMIT)
        {
            if (gaussian_radius != radius)
            {
                gaussian_plan = plan_blur(radius);
                gaussian_radius = radius;
            }
            plan = gaussian_plan;
        }
        else
            plan = plan_blur(radius);
        const f32 margin = std::min(static_cast<f32>(std::max(ow, oh)),
                                    std::ceil(plan.gaussian ? radius : radius * 2 + 2));
        const corner_radii radii = detail::clamp_radii(p.area, p.radii);
        rect paint_bounds;
        const bool bounded = detail::paint_bounds(ctx, paint_bounds);
        const rect visible = bounded ? rect::intersect(p.area, paint_bounds) : p.area;
        f64 left_pixel = std::floor(static_cast<f64>(visible.x) * density);
        f64 top_pixel = std::floor(static_cast<f64>(visible.y) * density);
        f64 right_pixel = std::ceil(static_cast<f64>(visible.right()) * density);
        f64 bottom_pixel = std::ceil(static_cast<f64>(visible.bottom()) * density);
        if (plan.gaussian && (radii.tl > 0 || radii.tr > 0 || radii.br > 0 || radii.bl > 0))
        {
            // The rounded fan's outer AA edge is half a logical pixel wider.
            // Include every pixel center strictly inside it before adding the
            // kernel support. At 1x these are the original nominal bounds.
            left_pixel = std::min(
                left_pixel, std::floor((static_cast<f64>(p.area.x) - 0.5) * density - 0.5) + 1);
            top_pixel = std::min(
                top_pixel, std::floor((static_cast<f64>(p.area.y) - 0.5) * density - 0.5) + 1);
            right_pixel = std::max(
                right_pixel, std::ceil((static_cast<f64>(p.area.right()) + 0.5) * density - 0.5));
            bottom_pixel = std::max(
                bottom_pixel, std::ceil((static_cast<f64>(p.area.bottom()) + 0.5) * density - 0.5));
            if (bounded)
            {
                left_pixel =
                    std::max(left_pixel, std::floor(static_cast<f64>(paint_bounds.x) * density));
                top_pixel =
                    std::max(top_pixel, std::floor(static_cast<f64>(paint_bounds.y) * density));
                right_pixel = std::min(right_pixel,
                                       std::ceil(static_cast<f64>(paint_bounds.right()) * density));
                bottom_pixel = std::min(
                    bottom_pixel, std::ceil(static_cast<f64>(paint_bounds.bottom()) * density));
            }
        }
        const i32 rx = static_cast<i32>(std::max(0.0, left_pixel - margin));
        const i32 ry = static_cast<i32>(std::max(0.0, top_pixel - margin));
        const i32 right = static_cast<i32>(std::min(static_cast<double>(ow), right_pixel + margin));
        const i32 bottom =
            static_cast<i32>(std::min(static_cast<double>(oh), bottom_pixel + margin));
        const i32 width = right - rx, height = bottom - ry;
        if (width <= 0 || height <= 0) return;
        detail::dl_flush(ctx);
        dev->flush_text();
        // Filter events include their complete normalized operands. An
        // unchanged chain can reuse final pixels; changed chains replay their
        // clear/draw/filter prefix in full order rather than sampling history.
        if (!prepare_blur(width, height, ow, oh, plan.gaussian)) return;
        auto &scratch = *ctx->blur;
        texture *images[] = {static_cast<texture *>(scratch.half),
                             static_cast<texture *>(scratch.quarter)};
        const rect source{static_cast<f32>(rx), static_cast<f32>(ry), static_cast<f32>(width),
                          static_cast<f32>(height)};
        const rect destination{0, 0, source.w, source.h};
        if (plan.gaussian)
        {
            auto parameters = plan.taps;
            parameters.step[0] = 1.0f / scene->width;
            dev->filter(scene, source, images[0], destination, blur_kernel::GAUSSIAN, parameters,
                        {0, 0, static_cast<f32>(ow), static_cast<f32>(oh)});
            parameters.step[0] = 0;
            parameters.step[1] = 1.0f / images[0]->height;
            dev->filter(images[0], destination, images[1], destination, blur_kernel::GAUSSIAN,
                        parameters, destination);
        }
        else
        {
            rect levels[7]{destination};
            for (u32 level = 1; level <= plan.levels; ++level)
                levels[level] = {0, 0, std::ceil(levels[level - 1].w * 0.5f),
                                 std::ceil(levels[level - 1].h * 0.5f)};
            texture *sample = scene;
            u32 next = 0;
            for (u32 level = 1; level <= plan.levels; ++level)
            {
                blur_filter parameters{};
                parameters.step[0] = plan.offset * 0.5f / sample->width;
                parameters.step[1] = plan.offset * 0.5f / sample->height;
                dev->filter(sample, level == 1 ? source : levels[level - 1], images[next],
                            levels[level], blur_kernel::KAWASE_DOWN, parameters,
                            level == 1 ? rect{0, 0, static_cast<f32>(ow), static_cast<f32>(oh)}
                                       : levels[level - 1]);
                sample = images[next];
                next ^= 1;
            }
            for (u32 level = plan.levels; level > 0; --level)
            {
                blur_filter parameters{};
                parameters.step[0] = plan.offset * 0.5f / sample->width;
                parameters.step[1] = plan.offset * 0.5f / sample->height;
                dev->filter(sample, levels[level], images[next], levels[level - 1],
                            blur_kernel::KAWASE_UP, parameters, levels[level]);
                sample = images[next];
                next ^= 1;
            }
        }
        dev->set_target(scene);
        const rect *clip = ctx->clip_depth ? &ctx->clip_stack[ctx->clip_depth - 1] : nullptr;
        detail::dl_prepare(ctx, scratch.quarter, clip);
        const f32 du = density / scratch.w, dv = density / scratch.h;
        const f32 ox = static_cast<f32>(rx) / density, oy = static_cast<f32>(ry) / density;
        const color tint{255, 255, 255, static_cast<u8>(clampf(p.alpha * 255, 0, 255))};
        const rect r = p.area;
        if (radii.tl == 0 && radii.tr == 0 && radii.br == 0 && radii.bl == 0)
        {
            const vertex vertices[] = {
                {r.x, r.y, (r.x - ox) * du, (r.y - oy) * dv, tint},
                {r.right(), r.y, (r.right() - ox) * du, (r.y - oy) * dv, tint},
                {r.right(), r.bottom(), (r.right() - ox) * du, (r.bottom() - oy) * dv, tint},
                {r.x, r.bottom(), (r.x - ox) * du, (r.bottom() - oy) * dv, tint}};
            const i32 indices[] = {0, 1, 2, 0, 2, 3};
            detail::dl_add(ctx, vertices, 4, indices, 6);
        }
        else
        {
            auto &vertices = storage.scratch_v;
            auto &indices = storage.scratch_i;
            vertices.clear();
            indices.clear();
            detail::build_rounded_fan(
                r, radii, tint,
                [&](f32 x, f32 y, f32 &u, f32 &v)
                {
                    u = (x - ox) * du;
                    v = (y - oy) * dv;
                },
                vertices, indices);
            detail::dl_add(ctx, vertices.data(), static_cast<i32>(vertices.size()), indices.data(),
                           static_cast<i32>(indices.size()));
        }
        struct composite_scope
        {
            device &dev;
            bool previous;
            composite_scope(device &d, bool gaussian) : dev(d), previous(d.gaussian_compositing)
            {
                dev.gaussian_compositing = gaussian;
            }
            ~composite_scope() { dev.gaussian_compositing = previous; }
        } composite{*dev, plan.gaussian};
        detail::dl_flush(ctx);
    }
};
} // namespace vk_detail
#endif
