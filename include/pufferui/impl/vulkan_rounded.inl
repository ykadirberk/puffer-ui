// Instanced Vulkan coverage for filled and gradient rounded rectangles.
// The device owns the fixed CPU batch and uses ordinary frame upload pages.
// Analytic paints share the mesh painter's clip and submission order.
// Included inside namespace pui by the single implementation TU.
#if defined(PUFFERUI_ENABLE_VULKAN)
namespace vk_detail
{
bool device::initialize_rounded(const VkGraphicsPipelineCreateInfo &base)
{
    try
    {
        rounded_pending.reserve(ROUNDED_BATCH_LIMIT);
    }
    catch (const std::bad_alloc &)
    {
        return check(VK_ERROR_OUT_OF_HOST_MEMORY, "reserve rounded rectangle batch");
    }
    VkShaderModule modules[2]{};
    VkShaderModuleCreateInfo shader{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    shader.codeSize = sizeof(vulkan_rounded_vert);
    shader.pCode = vulkan_rounded_vert;
    if (!check(vkCreateShaderModule(native.device, &shader, nullptr, &modules[0]),
               "vkCreateShaderModule(rounded vertex)"))
        return false;
    shader.codeSize = sizeof(vulkan_rounded_frag);
    shader.pCode = vulkan_rounded_frag;
    const bool compiled = check(vkCreateShaderModule(native.device, &shader, nullptr, &modules[1]),
                                "vkCreateShaderModule(rounded fragment)");
    bool created = false;
    if (compiled)
    {
        VkPipelineShaderStageCreateInfo stages[2] = {base.pStages[0], base.pStages[1]};
        for (usize i = 0; i < 2; ++i)
        {
            stages[i].module = modules[i];
            stages[i].pSpecializationInfo = nullptr;
        }
        const VkVertexInputBindingDescription binding{0, sizeof(rounded_instance),
                                                      VK_VERTEX_INPUT_RATE_INSTANCE};
        const VkVertexInputAttributeDescription attributes[] = {
            {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(rounded_instance, area)},
            {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(rounded_instance, radii)},
            {2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(rounded_instance, shades)},
            {3, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(rounded_instance, shades) + sizeof(color)},
            {4, 0, VK_FORMAT_R8G8B8A8_UNORM,
             offsetof(rounded_instance, shades) + 2 * sizeof(color)},
            {5, 0, VK_FORMAT_R8G8B8A8_UNORM,
             offsetof(rounded_instance, shades) + 3 * sizeof(color)},
            {6, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(rounded_instance, underlay)},
            {7, 0, VK_FORMAT_R32_UINT, offsetof(rounded_instance, gradient)}};
        VkPipelineVertexInputStateCreateInfo input{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        input.vertexBindingDescriptionCount = 1;
        input.pVertexBindingDescriptions = &binding;
        input.vertexAttributeDescriptionCount = static_cast<u32>(std::size(attributes));
        input.pVertexAttributeDescriptions = attributes;
        auto assembly = *base.pInputAssemblyState;
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        auto attachment = *base.pColorBlendState->pAttachments;
        attachment.blendEnable = VK_TRUE;
        attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        auto blend = *base.pColorBlendState;
        blend.pAttachments = &attachment;
        auto pipeline_info = base;
        pipeline_info.pStages = stages;
        pipeline_info.pVertexInputState = &input;
        pipeline_info.pInputAssemblyState = &assembly;
        pipeline_info.pColorBlendState = &blend;
        created = check(vkCreateGraphicsPipelines(native.device, VK_NULL_HANDLE, 1, &pipeline_info,
                                                  nullptr, &rounded_pipeline),
                        "vkCreateGraphicsPipelines(rounded rectangles)");
        vkDestroyShaderModule(native.device, modules[1], nullptr);
    }
    vkDestroyShaderModule(native.device, modules[0], nullptr);
    return created;
}

void device::flush_rounded()
{
    if (rounded_pending.empty() || rounded_flushing) return;
    paint_guard drawing(*this);
    const auto count = static_cast<u32>(rounded_pending.size());
    // An upload continuation can submit the previous prefix. Its paint barrier
    // must not reenter this flush before the batch has reached a fresh page.
    struct clear_batch
    {
        std::vector<rounded_instance> &pending;
        bool &flushing;
        ~clear_batch()
        {
            pending.clear();
            flushing = false;
        }
    } clear{rounded_pending, rounded_flushing};
    rounded_flushing = true;
    if (!valid(target) || result != VK_SUCCESS ||
        (current_surface && current_surface->suspended && target == current_surface->scene))
        return;

    const VkExtent2D extent{static_cast<u32>(target->width), static_cast<u32>(target->height)};
    VkRect2D scissor{{0, 0}, extent};
    if (has_clip)
    {
        if (!clip.is_finite()) return;
        const f64 x = std::trunc(static_cast<f64>(clip.x));
        const f64 y = std::trunc(static_cast<f64>(clip.y));
        const f64 left = std::max(0.0, x), top = std::max(0.0, y);
        const f64 right = std::min(static_cast<f64>(extent.width), x + std::trunc(clip.w));
        const f64 bottom = std::min(static_cast<f64>(extent.height), y + std::trunc(clip.h));
        if (right <= left || bottom <= top) return;
        scissor = {{static_cast<i32>(left), static_cast<i32>(top)},
                   {static_cast<u32>(right - left), static_cast<u32>(bottom - top)}};
    }

    VkDeviceSize offset = 0;
    const VkDeviceSize bytes = count * sizeof(rounded_instance);
    page *storage = upload(bytes, offset, true);
    if (!storage) return;
    std::memcpy(storage->mapped + offset, rounded_pending.data(), static_cast<usize>(bytes));
    const VkBuffer buffer = storage->staged ? storage->device_buffer : storage->buffer;
    if (!start_rendering(target)) return;
    vkCmdBindVertexBuffers(frame->command, 0, 1, &buffer, &offset);
    bind_graphics(rounded_pipeline, pipeline_layout, extent, scissor, VK_NULL_HANDLE, 0);
    vkCmdDraw(frame->command, 4, count, 0, 0);
    target->last_use = next_serial;
    ++target->revision;
    ++stats.draws;
    stats.vertices += count * 4;
    ++stats.rounded_draws;
    stats.rounded_instances += count;
}

bool native_painter::can_draw_rounded() const
{
    return dev->rounded_pipeline && ctx->current_window && !ctx->overlay_depth && dev->target &&
           dev->target->format == VK_FORMAT_R8G8B8A8_UNORM;
}

void native_painter::queue_rounded(rounded_instance instance)
{
    const f32 scale = ctx->render_scale;
    if (detail::outside_paint(ctx, instance.area.pad(-1.0f / scale))) return;
    auto &r = instance.area;
    r = {r.x * scale, r.y * scale, r.w * scale, r.h * scale};
    if (!r.is_finite() || !std::isfinite(r.right()) || !std::isfinite(r.bottom())) return;
    auto &q = instance.radii;
    q = {q.tl * scale, q.tr * scale, q.br * scale, q.bl * scale};
    if (!std::isfinite(q.tl) || !std::isfinite(q.tr) || !std::isfinite(q.br) ||
        !std::isfinite(q.bl))
        return;
    if (instance.gradient)
    {
        const auto &s = instance.shades;
        instance.underlay = theme_lerp_color(theme_lerp_color(s[0], s[1], .5f),
                                             theme_lerp_color(s[3], s[2], .5f), .5f);
    }
    const rect *clip = ctx->clip_depth > 0 ? &ctx->clip_stack[ctx->clip_depth - 1] : nullptr;
    detail::dl_prepare(ctx, nullptr, clip);
    if (!storage.verts.empty()) detail::mesh_flush(ctx);
    if (dev->rounded_pending.empty())
    {
        dev->flush_text();
        if (dev->cache_active) dev->cache_flush(false);
    }
    if (dev->rounded_pending.size() == ROUNDED_BATCH_LIMIT) dev->flush_rounded();

    if (ctx->disabled_depth > 0)
    {
        const f32 alpha = clampf(ctx->disabled_alpha, 0.0f, 1.0f);
        for (auto &shade : instance.shades) shade.a = static_cast<u8>(shade.a * alpha);
        instance.underlay.a = static_cast<u8>(instance.underlay.a * alpha);
    }
    dev->rounded_pending.push_back(instance);
}

void native_painter::rounded_rectangle(const paint_rounded_rect &p)
{
    if (!can_draw_rounded() || !p.area.is_valid()) return mesh_painter::rounded_rectangle(p);
    if (p.tint.a == 0 || p.area.w <= 0 || p.area.h <= 0) return;
    const corner_radii radii = detail::clamp_radii(p.area, p.radii);
    if (radii.tl == 0 && radii.tr == 0 && radii.br == 0 && radii.bl == 0)
        return mesh_painter::rounded_rectangle(p);
    queue_rounded({p.area, radii, {p.tint}});
}

void native_painter::rounded_gradient(const paint_rounded_gradient &p)
{
    if (!can_draw_rounded() || !p.area.is_valid() || !(p.radius > .5f))
        return mesh_painter::rounded_gradient(p);
    if (p.area.w <= .5f || p.area.h <= .5f) return;
    const f32 radius = min2(p.radius, min2(p.area.w, p.area.h) * .5f);
    queue_rounded({p.area,
                   corner_radii::all(radius),
                   {p.top_left, p.top_right, p.bottom_right, p.bottom_left},
                   {},
                   1});
}

void native_painter::flush()
{
    mesh_painter::flush();
    dev->flush_rounded();
#if defined(PUFFERUI_ENABLE_HARFBUZZ)
    if (dev->paths) dev->flush_paths();
#endif
}
} // namespace vk_detail
#endif
