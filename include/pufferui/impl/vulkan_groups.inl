// Native distance groups share one ordered primitive and one source color.
// Child storage is bounded by the prepared shape arena; rejection is atomic.
// Existing per-primitive alpha and disabled scopes retain their semantics.
// Included in the optional Vulkan implementation only.
#if defined(PUFFERUI_ENABLE_VULKAN)
bool vulkan_shape_group(ui &u, const vulkan_shape_group_opts &opts)
{
    auto *c = u.ctx;
    auto *d = c ? dynamic_cast<vk_detail::device *>(c->device) : nullptr;
    if (!d || d->options.painter != vulkan_painter_mode::INTEGRATED || !c->dl ||
        !c->current_window || c->overlay_depth || opts.shapes.empty() || opts.shapes.size() > 256 ||
        !std::isfinite(opts.outline_width) || !std::isfinite(opts.glow_radius) ||
        opts.outline_width < 0 || opts.glow_radius < 0 || !std::isfinite(c->render_scale) ||
        c->render_scale <= 0)
        return false;
    rect bounds = opts.shapes.front().area;
    for (const auto &item : opts.shapes)
    {
        const auto &a = item.area;
        const auto &r = item.radii;
        if (!a.is_valid() || a.w <= 0 || a.h <= 0 || !std::isfinite(a.x) || !std::isfinite(a.y) ||
            !std::isfinite(a.w) || !std::isfinite(a.h) || !std::isfinite(r.tl) ||
            !std::isfinite(r.tr) || !std::isfinite(r.br) || !std::isfinite(r.bl))
            return false;
        const f32 left = min2(bounds.x, a.x), top = min2(bounds.y, a.y);
        bounds = {left, top, max2(bounds.right(), a.right()) - left,
                  max2(bounds.bottom(), a.bottom()) - top};
    }
    const f32 margin = max2(opts.outline_width, opts.glow_radius) + 1;
    bounds = bounds.pad(-margin);
    if (!bounds.is_valid() || !std::isfinite(bounds.x) || !std::isfinite(bounds.y) ||
        !std::isfinite(bounds.w) || !std::isfinite(bounds.h))
        return false;
    if (detail::outside_paint(c, bounds)) return true;
    const usize count = opts.shapes.size() + 1;
    const usize limit =
        std::min<usize>(d->sdf_shapes.capacity(),
                        d->properties.limits.maxStorageBufferRange / sizeof(vk_detail::sdf_shape));
    if (count > limit) return false;
    if (d->sdf_shapes.size() > limit - count || d->sdf_spans.size() == d->sdf_spans.capacity())
    {
        detail::dl_flush(c);
        if (!d->flush_program()) return false;
        if (d->sdf_shapes.size() > limit - count || d->sdf_spans.size() == d->sdf_spans.capacity())
            return false;
    }
    const rect *clip = c->clip_depth ? &c->clip_stack[c->clip_depth - 1] : nullptr;
    detail::dl_prepare(c, nullptr, clip);
    const f32 scale = c->render_scale;
    for (const auto &item : opts.shapes)
    {
        const auto r = detail::clamp_radii(item.area, item.radii);
        d->sdf_shapes.push_back(
            {{item.area.x * scale, item.area.y * scale, item.area.w * scale, item.area.h * scale},
             {r.tl * scale, r.tr * scale, r.br * scale, r.bl * scale},
             scale});
    }
    vk_detail::sdf_shape header;
    header.bounds = {bounds.x * scale, bounds.y * scale, bounds.w * scale, bounds.h * scale};
    const u32 children = static_cast<u32>(opts.shapes.size());
    std::memcpy(&header.radii.tl, &children, 4);
    header.feather = scale;
    header.reserved[0] = 3;
    header.reserved[1] = opts.outline_width * scale;
    header.reserved[2] = opts.glow_radius * scale;
    const color colors[] = {opts.fill, opts.outline, opts.glow};
    for (u32 i = 0; i < 3; ++i)
    {
        auto value = colors[i];
        if (c->disabled_depth > 0)
            value.a = static_cast<u8>(value.a * clampf(c->disabled_alpha, 0.0f, 1.0f));
        header.corners[i] = static_cast<u32>(value.r) | (static_cast<u32>(value.g) << 8) |
                            (static_cast<u32>(value.b) << 16) | (static_cast<u32>(value.a) << 24);
    }
    d->sdf_shapes.push_back(header);
    const u32 index = static_cast<u32>(d->sdf_shapes.size() - 1);
    d->sdf_spans.push_back({c->dl->verts.size(), index});
    d->primitive_commands.pending_shape = index;
    const color white{255, 255, 255, 255};
    const vertex vertices[] = {{bounds.x, bounds.y, 0, 0, white},
                               {bounds.right(), bounds.y, 0, 0, white},
                               {bounds.right(), bounds.bottom(), 0, 0, white},
                               {bounds.x, bounds.bottom(), 0, 0, white}};
    constexpr i32 indices[] = {0, 1, 2, 0, 2, 3};
    detail::dl_add(c, vertices, 4, indices, 6);
    ++d->stats.sdf_shapes;
    return true;
}
#endif
