// GPU fixed-point certificates for repeated whole native programs.
// Full image bytes, including LOAD-dependent scratch, must remain unchanged.
// All resources are prepared outside drawing; no scene readback is performed.
// The single reusable snapshot buffer is ordered across queue submissions.
#if defined(PUFFERUI_ENABLE_VULKAN)
namespace vk_detail
{
program_cache_resources::~program_cache_resources()
{
    if (!dev) return;
    gpu_bin_job allocator;
    allocator.dev = dev;
    allocator.free_storage(before);
    for (auto &probe : probes) allocator.free_storage(probe.control);
    if (pool) dev->vkDestroyDescriptorPool(dev->native.device, pool, nullptr);
    dev->stats.descriptor_sets -= allocated_sets;
}
bool device::prepare_program_cache(surface &s)
{
    if (!options.whole_program_cache || !paint_detail::sdf_painter(options.painter) ||
        !can_compute || !s.scene || s.slots.size() < 2 ||
        options.cache_budget_bytes < 2 * sizeof(retained_program))
        return true;
    if (s.program_cache) return true;
    const u64 used = stats.gpu_sdf_cache_image_bytes + cache_usage();
    const u64 available = options.cache_budget_bytes > used ? options.cache_budget_bytes - used : 0;
    const u64 maximum = static_cast<u64>(s.scene->width) * s.scene->height * 4;
    const u64 cpu_capacity = std::min({maximum, options.command_budget_bytes / 2,
                                       available > 2 * sizeof(retained_program)
                                           ? (available - 2 * sizeof(retained_program)) / 2
                                           : 0});
    if (cpu_capacity < 4096) return true;
    auto *resources = new program_cache_resources();
    resources->dev = this;
    resources->width = s.scene->width;
    resources->height = s.scene->height;
    resources->slots = static_cast<u32>(s.slots.size());
    gpu_bin_job allocator;
    allocator.dev = this;
    const u64 capacity = static_cast<u64>(s.scene->width) * s.scene->height * 4;
    bool ready = allocator.storage(resources->before, capacity, storage_role::DEVICE);
    for (u32 i = 0; ready && i < resources->slots; ++i)
        ready = allocator.storage(resources->probes[i].control, 64);
    if (!ready)
    {
        delete resources;
        return result == VK_SUCCESS;
    }
    s.program_cache = resources;
    if (!program_compare_set)
    {
        const VkDescriptorSetLayoutBinding bindings[] = {
            {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
        VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        info.bindingCount = 3;
        info.pBindings = bindings;
        if (!check(vkCreateDescriptorSetLayout(native.device, &info, nullptr, &program_compare_set),
                   "native program comparison layout"))
            return false;
        const VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, 12};
        VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout_info.setLayoutCount = 1;
        layout_info.pSetLayouts = &program_compare_set;
        layout_info.pushConstantRangeCount = 1;
        layout_info.pPushConstantRanges = &range;
        if (!check(vkCreatePipelineLayout(native.device, &layout_info, nullptr,
                                          &program_compare_layout),
                   "native program comparison pipeline layout"))
            return false;
        program_compare_pipeline = compute_pipeline(
            *this, program_compare_layout, vulkan_program_compare, sizeof(vulkan_program_compare));
        if (!program_compare_pipeline) return false;
    }
    const u32 count = resources->slots * retained_program::MAX_IMAGES;
    const VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, count * 2},
                                          {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, count}};
    VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_info.maxSets = count;
    pool_info.poolSizeCount = 2;
    pool_info.pPoolSizes = sizes;
    if (!check(vkCreateDescriptorPool(native.device, &pool_info, nullptr, &resources->pool),
               "native program comparison descriptors"))
        return false;
    VkDescriptorSetLayout layouts[8 * retained_program::MAX_IMAGES];
    std::fill_n(layouts, count, program_compare_set);
    VkDescriptorSetAllocateInfo sets_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    sets_info.descriptorPool = resources->pool;
    sets_info.descriptorSetCount = count;
    sets_info.pSetLayouts = layouts;
    if (!check(vkAllocateDescriptorSets(native.device, &sets_info, resources->sets[0]),
               "native program comparison sets"))
        return false;
    resources->allocated_sets = count;
    stats.descriptor_sets += count;
    s.retained[0] = new retained_program;
    s.retained[1] = new retained_program;
    for (auto *program : s.retained)
    {
        program->payload = new u8[static_cast<usize>(cpu_capacity)];
        program->payload_capacity = static_cast<usize>(cpu_capacity);
    }
    return true;
}
void device::release_program_cache(surface &s)
{
    if (s.program_cache) wait(next_serial - 1);
    if (retaining == s.retained[0] || retaining == s.retained[1]) retaining = nullptr;
    if (retained_previous == s.retained[0] || retained_previous == s.retained[1])
        retained_previous = nullptr;
    delete s.program_cache;
    s.program_cache = nullptr;
    for (auto *&program : s.retained)
    {
        delete program;
        program = nullptr;
    }
}
inline void program_memory_barrier(device &d, VkPipelineStageFlags2 from, VkAccessFlags2 reads,
                                   VkPipelineStageFlags2 to, VkAccessFlags2 writes)
{
    VkMemoryBarrier2 memory{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    memory.srcStageMask = from;
    memory.srcAccessMask = reads;
    memory.dstStageMask = to;
    memory.dstAccessMask = writes;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers = &memory;
    d.vkCmdPipelineBarrier2(d.frame->command, &dependency);
}
bool device::retained_confirmed(surface &s)
{
    auto &resources = *s.program_cache;
    if (resources.confirmed == resources.epoch) return true;
    const u64 done = completed();
    for (u32 i = 0; i < resources.slots; ++i)
    {
        auto &probe = resources.probes[i];
        if (!probe.serial || probe.serial > done) continue;
        if (!probe.control.coherent)
        {
            VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
            range.memory = probe.control.memory;
            range.size = VK_WHOLE_SIZE;
            if (!check(vkInvalidateMappedMemoryRanges(native.device, 1, &range),
                       "native program certificate invalidate"))
                return false;
        }
        if (probe.epoch == resources.epoch)
        {
            if (!*reinterpret_cast<const u32 *>(probe.control.mapped))
                resources.confirmed = resources.epoch;
            else
            {
                resources.partial_reuse = true;
                if (options.painter == vulkan_painter_mode::INTEGRATED &&
                    options.whole_program_cache && !s.sdf_compute_requested)
                {
                    s.sdf_compute_requested = true;
                    s.program_compute_preparation_pending = true;
                }
            }
        }
        probe.serial = 0;
    }
    return resources.confirmed == resources.epoch;
}
void device::retained_probe_begin(surface &s)
{
    auto &resources = *s.program_cache;
    resources.recording = false;
    resources.count = 0;
    if (!resources.warm || !retained_equal || retained_confirmed(s)) return;
    const u64 alignment = std::max<u64>(4, properties.limits.minStorageBufferOffsetAlignment);
    u64 offset = 0;
    for (usize i = 0; i < retained_previous->image_count; ++i)
    {
        auto &previous = retained_previous->images[i];
        // A full overwrite before any read removes that image's initial state.
        // The remaining images need an exact GPU fixed-point certificate.
        if (!previous.writer || previous.reset) continue;
        auto *image = previous.image;
        const u64 bytes = static_cast<u64>(image->width) * image->height * 4;
        offset = (offset + alignment - 1) & ~(alignment - 1);
        if (image->format != VK_FORMAT_R8G8B8A8_UNORM ||
            !(image->usage & VK_IMAGE_USAGE_SAMPLED_BIT) ||
            bytes > properties.limits.maxStorageBufferRange || offset > resources.before.capacity ||
            bytes > resources.before.capacity - offset)
            return;
        resources.images[resources.count++] = {image, offset};
        offset += bytes;
    }
    if (!resources.count)
    {
        resources.confirmed = resources.epoch;
        return;
    }
    if (!commands()) return;
    end_rendering();
    program_memory_barrier(*this, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                           VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                           VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    auto &control = resources.probes[s.slot].control;
    vkCmdFillBuffer(frame->command, control.buffer, 0, 4, 0);
    for (usize i = 0; i < resources.count; ++i)
    {
        auto &copy = resources.images[i];
        transition(copy.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VkBufferImageCopy region{};
        region.bufferOffset = copy.offset;
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {static_cast<u32>(copy.image->width),
                              static_cast<u32>(copy.image->height), 1};
        vkCmdCopyImageToBuffer(frame->command, copy.image->image, copy.image->layout,
                               resources.before.buffer, 1, &region);
    }
    resources.recording = true;
}
bool device::retained_probe_end(surface &s)
{
    auto &resources = *s.program_cache;
    if (!resources.recording) return true;
    end_rendering();
    program_memory_barrier(*this, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                           VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                           VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
    auto &probe = resources.probes[s.slot];
    u64 pixels = 0;
    for (usize i = 0; i < resources.count; ++i)
    {
        auto &copy = resources.images[i];
        auto *image = copy.image;
        transition(image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        const u64 bytes = static_cast<u64>(image->width) * image->height * 4;
        const VkDescriptorBufferInfo before{resources.before.buffer, copy.offset, bytes};
        const VkDescriptorBufferInfo control{probe.control.buffer, 0, 4};
        const VkDescriptorImageInfo current{samplers[0], image->view,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        const VkDescriptorSet set = resources.sets[s.slot][i];
        VkWriteDescriptorSet writes[3]{};
        for (u32 binding = 0; binding < 3; ++binding)
        {
            writes[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[binding].dstSet = set;
            writes[binding].dstBinding = binding;
            writes[binding].descriptorCount = 1;
            writes[binding].descriptorType = binding == 1
                                                 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
                                                 : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        }
        writes[0].pBufferInfo = &before;
        writes[1].pImageInfo = &current;
        writes[2].pBufferInfo = &control;
        vkUpdateDescriptorSets(native.device, 3, writes, 0, nullptr);
        vkCmdBindPipeline(frame->command, VK_PIPELINE_BIND_POINT_COMPUTE, program_compare_pipeline);
        vkCmdBindDescriptorSets(frame->command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                program_compare_layout, 0, 1, &set, 0, nullptr);
        const u32 groups = static_cast<u32>((bytes / 4 + 63) / 64);
        const u32 columns = std::min(groups, properties.limits.maxComputeWorkGroupCount[0]);
        const u32 push[] = {static_cast<u32>(image->width), static_cast<u32>(image->height),
                            columns};
        vkCmdPushConstants(frame->command, program_compare_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                           sizeof(push), push);
        vkCmdDispatch(frame->command, columns, (groups + columns - 1) / columns, 1);
        pixels += bytes / 4;
    }
    program_memory_barrier(*this, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                           VK_ACCESS_2_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_2_HOST_BIT,
                           VK_ACCESS_2_HOST_READ_BIT);
    probe.serial = next_serial;
    probe.epoch = resources.epoch;
    ++stats.whole_program_probes;
    stats.whole_program_probe_pixels += pixels;
    return true;
}
} // namespace vk_detail
#endif
