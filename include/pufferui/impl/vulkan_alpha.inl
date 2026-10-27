// Prepared native reference/fallback for an ordered local alpha accumulator.
// One reusable surface image; nesting is rejected before changing the target.
// Children retain their actual draw/texture/effect order and RGBA8 quantization.
// The final composite converts associated color back to native straight alpha.
#if defined(PUFFERUI_ENABLE_VULKAN)
namespace vk_detail
{
bool device::prepare_alpha(surface &s)
{
    if (options.painter != vulkan_painter_mode::INTEGRATED || !s.scene) return true;
    if (s.alpha_scratch && s.alpha_scratch->width == s.scene->width &&
        s.alpha_scratch->height == s.scene->height)
        return true;
    if (s.alpha_scratch)
    {
        destroy_texture(s.alpha_scratch);
        s.alpha_scratch = nullptr;
    }
    const u64 bytes = static_cast<u64>(s.scene->width) * s.scene->height * 4;
    if (bytes > options.cache_budget_bytes) return true;
    s.alpha_scratch = make_texture({.width = s.scene->width,
                                    .height = s.scene->height,
                                    .render_target = true,
                                    .allocation_budget = options.cache_budget_bytes});
    return s.alpha_scratch || result == VK_SUCCESS;
}
bool device::begin_alpha(context *c, f32 opacity)
{
    if (alpha_context)
    {
        PUFFERUI_CHECK(VIOL_SCOPE_UNBALANCED, false, "nested Vulkan alpha barrier");
        return false;
    }
    if (options.painter != vulkan_painter_mode::INTEGRATED || !c || !c->current_window ||
        c->overlay_depth || !current_surface || !current_surface->alpha_scratch || !target ||
        target == current_surface->alpha_scratch || !std::isfinite(opacity) || opacity < 0 ||
        opacity > 1 || target->width > current_surface->alpha_scratch->width ||
        target->height > current_surface->alpha_scratch->height)
        return false;
    detail::dl_flush(c);
    if (!flush_program()) return false;
    alpha_destination = target;
    alpha_opacity = opacity;
    alpha_clip = clip;
    alpha_clipped = has_clip;
    alpha_context = c;
    set_target(current_surface->alpha_scratch);
    clear({0, 0, 0, 0});
    set_clip(alpha_clipped ? &alpha_clip : nullptr);
    return result == VK_SUCCESS;
}
void device::end_alpha(context *c)
{
    if (!c || alpha_context != c) return;
    detail::dl_flush(c);
    if (!flush_program()) return;
    auto *source = current_surface->alpha_scratch;
    auto *destination = alpha_destination;
    const f32 opacity = alpha_opacity;
    alpha_context = nullptr;
    alpha_destination = nullptr;
    set_target(destination);
    set_clip(alpha_clipped ? &alpha_clip : nullptr);
    if (result != VK_SUCCESS || !c->dl || !c->current_window || !std::isfinite(c->render_scale) ||
        c->render_scale <= 0)
        return;
    const rect area{0, 0, destination->width / c->render_scale,
                    destination->height / c->render_scale};
    if (detail::outside_paint(c, area)) return;
    const rect *active_clip = c->clip_depth ? &c->clip_stack[c->clip_depth - 1] : nullptr;
    detail::dl_prepare(c, source, active_clip);
    if (!primitive_commands.shape_room(c))
    {
        fail("alpha composite exceeded prepared shape capacity");
        return;
    }
    sdf_shape shape;
    shape.bounds = {0, 0, static_cast<f32>(destination->width),
                    static_cast<f32>(destination->height)};
    shape.reserved[0] = 4;
    shape.reserved[1] = opacity;
    sdf_shapes.push_back(shape);
    const u32 index = static_cast<u32>(sdf_shapes.size() - 1);
    sdf_spans.push_back({c->dl->verts.size(), index});
    primitive_commands.pending_shape = index;
    const f32 right = static_cast<f32>(destination->width) / source->width;
    const f32 bottom = static_cast<f32>(destination->height) / source->height;
    const color white{255, 255, 255, 255};
    const vertex vertices[] = {{0, 0, 0, 0, white},
                               {area.w, 0, right, 0, white},
                               {area.w, area.h, right, bottom, white},
                               {0, area.h, 0, bottom, white}};
    constexpr i32 indices[] = {0, 1, 2, 0, 2, 3};
    detail::dl_add(c, vertices, 4, indices, 6);
    // One sealed composite program has one sampled accumulator. Keep its source
    // live through descriptor selection, before another barrier reuses scratch.
    alpha_sample = source;
    detail::dl_flush(c);
    flush_program();
    alpha_sample = nullptr;
}
} // namespace vk_detail
vulkan_alpha_scope::vulkan_alpha_scope(ui &u, const vulkan_alpha_opts &opts) : ctx(u.ctx)
{
    auto *d = ctx ? dynamic_cast<vk_detail::device *>(ctx->device) : nullptr;
    opened = d && d->begin_alpha(ctx, opts.opacity);
}
vulkan_alpha_scope::~vulkan_alpha_scope()
{
    if (!opened || !ctx) return;
    auto *d = dynamic_cast<vk_detail::device *>(ctx->device);
    if (d) d->end_alpha(ctx);
}
#endif
