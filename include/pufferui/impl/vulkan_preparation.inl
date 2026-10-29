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
bool device::prepare_geometry(const geometry_capacity &capacity)
{
    constexpr usize limit = 0x7fffffff;
    if (paint_depth || !frame || !frame->recording || !target || capacity.vertices > limit ||
        capacity.indices > limit)
        return false;
    const VkDeviceSize requested = static_cast<VkDeviceSize>(capacity.vertices) * vertex_stride() +
                                   static_cast<VkDeviceSize>(capacity.indices) * 4;
    if (!requested) return true;
    const VkDeviceSize bytes =
        geometry_reservation(requested, static_cast<VkDeviceSize>(target->width) * target->height);
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
    if (graphics.descriptors[set] != descriptor)
    {
        vkCmdBindDescriptorSets(frame->command, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, set, 1,
                                &descriptor, 0, nullptr);
        graphics.descriptors[set] = descriptor;
        ++stats.descriptor_binds;
    }
    graphics.extent = extent;
}
} // namespace vk_detail
#endif
