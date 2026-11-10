// Bounded native upload preparation and command-buffer graphics state.
// No command tape, shape interpreter, compute pipeline or GPU tile lists.
// Preparation occurs at surface/frame boundaries or an explicit caller hint.
// Included inside namespace pui by the single implementation TU.
#if defined(PUFFERUI_ENABLE_VULKAN)
namespace vk_detail
{
bool device::prepare_slot_geometry(frame_slot &slot, VkDeviceSize bytes)
{
    if (paint_depth || slot.serial > known_completed) return false;
    const bool staged = options.upload_mode == vulkan_upload_mode::STAGED;
    for (const auto &p : slot.pages)
    {
        if (p.geometry && p.used) return false; // this recording already references its storage
        if (p.geometry && p.staged == staged && p.capacity >= bytes)
        {
            slot.geometry_chunk_bytes = p.capacity;
            return true;
        }
    }
    // All previous uses of this slot are complete and the new recording has no
    // geometry prefix. Replace old small pages instead of retaining every peak.
    for (usize i = 0; i < slot.pages.size();)
        if (slot.pages[i].geometry)
        {
            free_page(slot.pages[i]);
            slot.pages.erase(slot.pages.begin() + static_cast<std::ptrdiff_t>(i));
        }
        else
            ++i;
    page fresh;
    if (!allocate_buffer(fresh, bytes,
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                             VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT))
        return false;
    fresh.geometry = true;
    fresh.staged = staged;
    if (staged && !allocate_device_buffer(fresh))
    {
        free_page(fresh);
        return false;
    }
    slot.pages.push_back(fresh);
    slot.geometry_chunk_bytes = bytes;
    return true;
}
VkDeviceSize device::geometry_reservation(VkDeviceSize requested, VkDeviceSize pixels) const
{
    // Hints may describe millions of clipped vertices. Bound speculative
    // residency by visible pixels and by a share of the configured budget.
    const VkDeviceSize budget =
        options.upload_budget_bytes / (2 * options.frames_in_flight *
                                       (options.upload_mode == vulkan_upload_mode::STAGED ? 2 : 1));
    const VkDeviceSize desired = std::min(requested, pixels * 24);
    VkDeviceSize bytes = 1024 * 1024;
    while (bytes < desired && bytes <= budget / 2) bytes *= 2;
    return bytes <= budget ? bytes : 0;
}
VkDeviceSize device::geometry_hint_reservation(VkDeviceSize requested, VkDeviceSize pixels,
                                               VkDeviceSize peak) const
{
    const VkDeviceSize bytes = geometry_reservation(requested, pixels);
    if (!options.analytic_rounded_rects || !bytes || requested <= bytes) return bytes;

    // A rejected mesh hint can describe the tessellation that analytic paints
    // avoid. Reserve less speculatively and retain headroom for observed usage.
    const VkDeviceSize observed = peak <= UINT64_MAX / 2 ? peak * 2 : peak;
    const VkDeviceSize desired = std::max(pixels * 2, observed);
    return desired < bytes ? geometry_reservation(desired, pixels) : bytes;
}
bool device::prepare_geometry(const geometry_capacity &capacity)
{
    constexpr usize limit = 0x7fffffff;
    if (paint_depth || !frame || !frame->recording || !target || capacity.vertices > limit ||
        capacity.indices > limit)
        return false;
    const VkDeviceSize requested = static_cast<VkDeviceSize>(capacity.vertices) * vertex_stride() +
                                   static_cast<VkDeviceSize>(capacity.indices) * 4;
    if (!requested) return true;
    const VkDeviceSize pixels = static_cast<VkDeviceSize>(target->width) * target->height;
    const VkDeviceSize bytes =
        current_surface && target == current_surface->scene
            ? geometry_hint_reservation(requested, pixels, current_surface->geometry_peak)
            : geometry_reservation(requested, pixels);
    if (!bytes) return false;
    const bool prepared = prepare_slot_geometry(*frame, bytes);
    // True certifies the complete requested byte count. Oversized hints still
    // receive the bounded reservation; ordinary geometric fallback remains.
    return prepared && requested <= bytes;
}
void device::bind_graphics(VkPipeline pipeline, VkPipelineLayout layout, VkExtent2D extent,
                           VkRect2D scissor, VkDescriptorSet descriptor, u32 set)
{
    auto &graphics = frame->graphics;
    if (graphics.pipeline != pipeline)
    {
        vkCmdBindPipeline(frame->command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        graphics.pipeline = pipeline;
        ++stats.pipeline_binds;
    }
    const bool resized =
        graphics.extent.width != extent.width || graphics.extent.height != extent.height;
    if (resized)
    {
        const VkViewport viewport{
            0, 0, static_cast<f32>(extent.width), static_cast<f32>(extent.height), 0, 1};
        vkCmdSetViewport(frame->command, 0, 1, &viewport);
        ++stats.viewport_sets;
    }
    if (resized || graphics.layout != layout)
    {
        const f32 projection[] = {static_cast<f32>(extent.width), static_cast<f32>(extent.height)};
        vkCmdPushConstants(frame->command, layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(projection), projection);
    }
    if (!graphics.scissor_known || graphics.scissor.offset.x != scissor.offset.x ||
        graphics.scissor.offset.y != scissor.offset.y ||
        graphics.scissor.extent.width != scissor.extent.width ||
        graphics.scissor.extent.height != scissor.extent.height)
    {
        vkCmdSetScissor(frame->command, 0, 1, &scissor);
        graphics.scissor = scissor;
        graphics.scissor_known = true;
        ++stats.scissor_sets;
    }
    if (graphics.layout != layout)
    {
        graphics.descriptors[0] = graphics.descriptors[1] = VK_NULL_HANDLE;
        graphics.layout = layout;
    }
    if (descriptor && graphics.descriptors[set] != descriptor)
    {
        vkCmdBindDescriptorSets(frame->command, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, set, 1,
                                &descriptor, 0, nullptr);
        graphics.descriptors[set] = descriptor;
        ++stats.descriptor_binds;
    }
    graphics.extent = extent;
}
} // namespace vk_detail
bool vulkan_prepare_surface_geometry(render_surface *base, const geometry_capacity &capacity)
{
    auto *surface = dynamic_cast<vk_detail::surface *>(base);
    constexpr usize limit = 0x7fffffff;
    if (!surface || capacity.vertices > limit || capacity.indices > limit) return false;
    auto &d = *surface->dev;
    if (d.result != VK_SUCCESS || d.preparing_geometry || d.paint_depth || d.cache_active ||
        (d.frame && d.frame->recording) || surface->dirty || !surface->scene ||
        !surface->extent.width || !surface->extent.height)
        return false;
    // Check every slot before replacing anything. group_serial covers uses by
    // another logical frame's continuation, not just this command pool's serial.
    for (const auto &slot : surface->slots)
        if (slot.recording || slot.serial > d.known_completed ||
            slot.group_serial > d.known_completed)
            return false;
    const VkDeviceSize requested =
        static_cast<VkDeviceSize>(capacity.vertices) * d.vertex_stride() +
        static_cast<VkDeviceSize>(capacity.indices) * sizeof(i32);
    if (!requested) return true;
    const VkDeviceSize bytes = d.geometry_hint_reservation(
        requested, static_cast<VkDeviceSize>(surface->extent.width) * surface->extent.height,
        surface->geometry_peak);
    if (!bytes) return false;
    struct preparation_scope
    {
        vk_detail::device &d;
        explicit preparation_scope(vk_detail::device &value) : d(value)
        {
            d.preparing_geometry = true;
        }
        ~preparation_scope() { d.preparing_geometry = false; }
    } scope(d);
    for (auto &slot : surface->slots)
    {
        // Completed recordings can no longer read these pages. No UI frame,
        // model update, command buffer reset or GPU submission is required.
        for (auto &page : slot.pages)
            if (page.geometry) page.used = 0;
        if (!d.prepare_slot_geometry(slot, bytes)) return false;
    }
    return requested <= bytes;
}
#endif
