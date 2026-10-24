// GPU coarse/fine preprocessing and ordered index-list construction.
// A job owns buffers/descriptors and outlives the submission that consumes them.
// Independent from the painter; no scene pixels are modified by preprocessing.
// Included inside namespace pui by the single implementation translation unit.
#if defined(PUFFERUI_ENABLE_VULKAN)
namespace vk_detail
{
inline VkPipeline compute_pipeline(device &dev, VkPipelineLayout layout, const u32 *code,
                                   usize bytes)
{
    VkShaderModule module = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo sc{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    sc.codeSize = bytes;
    sc.pCode = code;
    if (!dev.check(dev.vkCreateShaderModule(dev.native.device, &sc, nullptr, &module),
                   "compute module"))
        return VK_NULL_HANDLE;
    VkComputePipelineCreateInfo pc{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pc.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pc.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pc.stage.module = module;
    pc.stage.pName = "main";
    pc.layout = layout;
    VkPipeline pipeline = VK_NULL_HANDLE;
    const VkResult result =
        dev.vkCreateComputePipelines(dev.native.device, VK_NULL_HANDLE, 1, &pc, nullptr, &pipeline);
    dev.vkDestroyShaderModule(dev.native.device, module, nullptr);
    if (!dev.check(result, "compute pipeline"))
    {
        if (pipeline) dev.vkDestroyPipeline(dev.native.device, pipeline, nullptr);
        return VK_NULL_HANDLE;
    }
    return pipeline;
}
struct gpu_bin_job
{
    device *dev = nullptr;
    page buffers[5];
    VkDescriptorSetLayout descriptor_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet descriptor = VK_NULL_HANDLE;
    VkPipeline pipelines[4]{};
    u32 parameters[12]{};
    u64 serial = 0;
    bool recorded = false;
    gpu_bin_job() = default;
    gpu_bin_job(const gpu_bin_job &) = delete;
    gpu_bin_job &operator=(const gpu_bin_job &) = delete;
    ~gpu_bin_job() { release(); }

    bool storage(page &p, u64 bytes)
    {
        bytes = bytes ? bytes : 4;
        VkBufferCreateInfo bc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bc.size = bytes;
        bc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
        if (!dev->check(dev->vkCreateBuffer(dev->native.device, &bc, nullptr, &p.buffer),
                        "bin buffer"))
            return false;
        VkMemoryRequirements req{};
        dev->vkGetBufferMemoryRequirements(dev->native.device, p.buffer, &req);
        const u64 budget = dev->options.gpu_command_budget_bytes;
        if (req.size > budget || dev->stats.gpu_metadata_allocated_bytes > budget - req.size)
            return false; // eligibility failure, not a sticky device failure
        const u32 type = dev->memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (type == UINT32_MAX) return false;
        VkMemoryAllocateInfo ac{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ac.allocationSize = req.size;
        ac.memoryTypeIndex = type;
        if (!dev->check(dev->vkAllocateMemory(dev->native.device, &ac, nullptr, &p.memory),
                        "bin allocation"))
            return false;
        p.allocation = req.size;
        dev->stats.gpu_metadata_allocated_bytes += req.size;
        if (dev->stats.gpu_metadata_allocated_bytes > dev->stats.peak_gpu_metadata_allocated_bytes)
            dev->stats.peak_gpu_metadata_allocated_bytes = dev->stats.gpu_metadata_allocated_bytes;
        if (!dev->check(dev->vkBindBufferMemory(dev->native.device, p.buffer, p.memory, 0),
                        "bin bind") ||
            !dev->check(dev->vkMapMemory(dev->native.device, p.memory, 0, VK_WHOLE_SIZE, 0,
                                         reinterpret_cast<void **>(&p.mapped)),
                        "bin map"))
            return false;
        p.capacity = bytes;
        p.coherent = (dev->memory_properties.memoryTypes[type].propertyFlags &
                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
        return true;
    }
    void free_storage(page &p)
    {
        if (p.mapped) dev->vkUnmapMemory(dev->native.device, p.memory);
        if (p.buffer) dev->vkDestroyBuffer(dev->native.device, p.buffer, nullptr);
        if (p.memory) dev->vkFreeMemory(dev->native.device, p.memory, nullptr);
        dev->stats.gpu_metadata_allocated_bytes -= p.allocation;
        p = {};
    }
    bool resize_storage(u32 index, u64 bytes)
    {
        if (!dev || recorded || index >= 5 || !descriptor) return false;
        if (bytes > dev->properties.limits.maxStorageBufferRange) return false;
        if (bytes > buffers[index].capacity)
        {
            free_storage(buffers[index]);
            if (!storage(buffers[index], bytes)) return false;
            VkDescriptorBufferInfo info{buffers[index].buffer, 0, buffers[index].capacity};
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = descriptor;
            write.dstBinding = index;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            write.pBufferInfo = &info;
            dev->vkUpdateDescriptorSets(dev->native.device, 1, &write, 0, nullptr);
        }
        return true;
    }
    bool upload_storage(u32 index, std::span<const u8> bytes)
    {
        if (!resize_storage(index, bytes.size())) return false;
        if (!bytes.empty()) std::memcpy(buffers[index].mapped, bytes.data(), bytes.size());
        buffers[index].used = bytes.size();
        return dev->flush_page(buffers[index]);
    }
    void release()
    {
        if (!dev) return;
        if (recorded && serial >= dev->next_serial && dev->frame && dev->frame->recording)
            serial = dev->submit();
        if (serial && serial < dev->next_serial) dev->wait(serial);
        if (descriptor) --dev->stats.descriptor_sets;
        for (auto pipeline : pipelines)
            if (pipeline) dev->vkDestroyPipeline(dev->native.device, pipeline, nullptr);
        if (pool) dev->vkDestroyDescriptorPool(dev->native.device, pool, nullptr);
        if (layout) dev->vkDestroyPipelineLayout(dev->native.device, layout, nullptr);
        if (descriptor_layout)
            dev->vkDestroyDescriptorSetLayout(dev->native.device, descriptor_layout, nullptr);
        for (auto &p : buffers) free_storage(p);
        for (auto &pipeline : pipelines) pipeline = VK_NULL_HANDLE;
        descriptor_layout = VK_NULL_HANDLE;
        layout = VK_NULL_HANDLE;
        pool = VK_NULL_HANDLE;
        descriptor = VK_NULL_HANDLE;
        dev = nullptr;
        serial = 0;
        recorded = false;
    }
    bool initialize(device &owner, const paint_detail::command_tape &tape, u32 width, u32 height,
                    u32 tile, u32 coarse, u32 capacity)
    {
        if (dev || !tape.valid || !width || !height || !tile || coarse < tile || coarse % tile ||
            width > owner.properties.limits.maxImageDimension2D ||
            height > owner.properties.limits.maxImageDimension2D ||
            tape.commands.size() > 0xffffffc0u ||
            owner.properties.limits.maxPerStageDescriptorStorageBuffers < 5)
            return false;
        dev = &owner;
        u32 family_count = 0;
        owner.vkGetPhysicalDeviceQueueFamilyProperties(owner.native.physical_device, &family_count,
                                                       nullptr);
        std::vector<VkQueueFamilyProperties> families(family_count);
        owner.vkGetPhysicalDeviceQueueFamilyProperties(owner.native.physical_device, &family_count,
                                                       families.data());
        if (owner.native.queue_family >= family_count ||
            !(families[owner.native.queue_family].queueFlags & VK_QUEUE_COMPUTE_BIT))
            return false;
        const u64 tx = (static_cast<u64>(width) + tile - 1) / tile;
        const u64 ty = (static_cast<u64>(height) + tile - 1) / tile;
        const u64 cx = (static_cast<u64>(width) + coarse - 1) / coarse;
        const u64 cy = (static_cast<u64>(height) + coarse - 1) / coarse;
        const u64 words = (tape.commands.size() + 31) / 32;
        const u64 tiles = tx * ty;
        if (tiles > 0x7fffffc0u ||
            (tiles + 63) / 64 > owner.properties.limits.maxComputeWorkGroupCount[0] ||
            (tape.commands.size() + 63) / 64 > owner.properties.limits.maxComputeWorkGroupCount[0])
            return false;
        parameters[0] = width;
        parameters[1] = height;
        parameters[2] = tile;
        parameters[3] = coarse;
        parameters[4] = static_cast<u32>(tx);
        parameters[5] = static_cast<u32>(ty);
        parameters[6] = static_cast<u32>(cx);
        parameters[7] = static_cast<u32>(cy);
        parameters[8] = static_cast<u32>(tape.commands.size());
        parameters[9] = static_cast<u32>(words);
        parameters[10] = capacity;
        parameters[11] = static_cast<u32>(tiles);
        const u64 sizes[] = {tape.commands.size() * sizeof(paint_detail::command),
                             cx * cy * words * 4, (tiles * 2 + 1) * 4,
                             static_cast<u64>(capacity) * 4, 32};
        u64 total = 0;
        for (const auto bytes : sizes)
        {
            if (bytes > owner.properties.limits.maxStorageBufferRange ||
                bytes > owner.options.gpu_command_budget_bytes - total)
                return false;
            total += bytes;
        }
        for (u32 i = 0; i < 5; ++i)
            if (!storage(buffers[i], sizes[i])) return false;
        if (sizes[0])
            std::memcpy(buffers[0].mapped, tape.commands.data(), static_cast<usize>(sizes[0]));
        buffers[0].used = sizes[0];
        if (!owner.flush_page(buffers[0])) return false;
        VkDescriptorSetLayoutBinding bindings[5]{};
        for (u32 i = 0; i < 5; ++i)
            bindings[i] = {i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT,
                           nullptr};
        VkDescriptorSetLayoutCreateInfo dc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        dc.bindingCount = 5;
        dc.pBindings = bindings;
        if (!owner.check(owner.vkCreateDescriptorSetLayout(owner.native.device, &dc, nullptr,
                                                           &descriptor_layout),
                         "bin descriptors"))
            return false;
        VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(parameters)};
        VkPipelineLayoutCreateInfo lc{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        lc.setLayoutCount = 1;
        lc.pSetLayouts = &descriptor_layout;
        lc.pushConstantRangeCount = 1;
        lc.pPushConstantRanges = &range;
        if (!owner.check(owner.vkCreatePipelineLayout(owner.native.device, &lc, nullptr, &layout),
                         "bin layout"))
            return false;
        const u32 *sources[] = {vulkan_bin_coarse, vulkan_bin_count, vulkan_bin_scan,
                                vulkan_bin_emit};
        const usize sizes_spv[] = {sizeof(vulkan_bin_coarse), sizeof(vulkan_bin_count),
                                   sizeof(vulkan_bin_scan), sizeof(vulkan_bin_emit)};
        for (u32 i = 0; i < 4; ++i)
        {
            pipelines[i] = compute_pipeline(owner, layout, sources[i], sizes_spv[i]);
            if (!pipelines[i]) return false;
        }
        VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5};
        VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pi.maxSets = 1;
        pi.poolSizeCount = 1;
        pi.pPoolSizes = &ps;
        if (!owner.check(owner.vkCreateDescriptorPool(owner.native.device, &pi, nullptr, &pool),
                         "bin pool"))
            return false;
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = pool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &descriptor_layout;
        if (!owner.check(owner.vkAllocateDescriptorSets(owner.native.device, &ai, &descriptor),
                         "bin set"))
            return false;
        ++owner.stats.descriptor_sets;
        VkDescriptorBufferInfo infos[5]{};
        VkWriteDescriptorSet writes[5]{};
        for (u32 i = 0; i < 5; ++i)
        {
            infos[i] = {buffers[i].buffer, 0, buffers[i].capacity};
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = descriptor;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[i].pBufferInfo = &infos[i];
        }
        owner.vkUpdateDescriptorSets(owner.native.device, 5, writes, 0, nullptr);
        return true;
    }
    void barrier(VkPipelineStageFlags2 source, VkAccessFlags2 source_access,
                 VkPipelineStageFlags2 destination, VkAccessFlags2 destination_access)
    {
        VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        barrier.srcStageMask = source;
        barrier.srcAccessMask = source_access;
        barrier.dstStageMask = destination;
        barrier.dstAccessMask = destination_access;
        VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.memoryBarrierCount = 1;
        dep.pMemoryBarriers = &barrier;
        dev->vkCmdPipelineBarrier2(dev->frame->command, &dep);
    }
    // The caller owns a completed frame slot; no descriptor or mapped range
    // may still be in use when preparing this job for another submission.
    bool prepare_again(const paint_detail::command_tape &tape, u32 capacity)
    {
        if (!dev || !tape.valid || tape.commands.size() > 0xffffffc0u) return false;
        recorded = false;
        serial = 0;
        const u64 words = (tape.commands.size() + 31) / 32;
        if (!resize_storage(0, tape.commands.size() * sizeof(paint_detail::command)) ||
            !resize_storage(1, static_cast<u64>(parameters[6]) * parameters[7] * words * 4) ||
            !resize_storage(3, static_cast<u64>(capacity) * 4))
            return false;
        parameters[8] = static_cast<u32>(tape.commands.size());
        parameters[9] = static_cast<u32>(words);
        parameters[10] = capacity;
        return upload_storage(
            0, std::span<const u8>(reinterpret_cast<const u8 *>(tape.commands.data()),
                                   tape.commands.size() * sizeof(paint_detail::command)));
    }
    bool record()
    {
        if (!dev || !pipelines[3] || recorded || !dev->commands()) return false;
        dev->end_rendering();
        recorded = true;
        serial = dev->next_serial;
        for (u32 i = 1; i < 5; ++i)
            dev->vkCmdFillBuffer(dev->frame->command, buffers[i].buffer, 0, VK_WHOLE_SIZE, 0);
        barrier(VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
        dev->vkCmdBindDescriptorSets(dev->frame->command, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0,
                                     1, &descriptor, 0, nullptr);
        dev->vkCmdPushConstants(dev->frame->command, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                sizeof(parameters), parameters);
        for (u32 i = 0; i < 4; ++i)
        {
            dev->vkCmdBindPipeline(dev->frame->command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                   pipelines[i]);
            if (i == 3)
                dev->vkCmdDispatchIndirect(dev->frame->command, buffers[4].buffer, 0);
            else
                dev->vkCmdDispatch(
                    dev->frame->command,
                    i == 2 ? 1 : ((i == 0 ? parameters[8] : parameters[11]) + 63) / 64, 1, 1);
            barrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                    VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT |
                        VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
        }
        return true;
    }
    bool readback(paint_detail::tile_lists &result, bool &overflow)
    {
        if (!dev || !recorded) return false;
        barrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
        serial = dev->submit();
        if (!serial || !dev->wait(serial)) return false;
        ++dev->stats.gpu_metadata_readbacks;
        for (const auto &p : buffers)
            if (!p.coherent)
            {
                VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
                range.memory = p.memory;
                range.size = VK_WHOLE_SIZE;
                if (!dev->check(dev->vkInvalidateMappedMemoryRanges(dev->native.device, 1, &range),
                                "bin readback invalidate"))
                    return false;
            }
        const auto *control = reinterpret_cast<const u32 *>(buffers[4].mapped);
        overflow = control[3] != 0;
        result.offsets.clear();
        result.indices.clear();
        if (overflow) return true;
        const auto *work = reinterpret_cast<const u32 *>(buffers[2].mapped);
        const auto *indices = reinterpret_cast<const u32 *>(buffers[3].mapped);
        if (control[4] > parameters[10]) return false;
        result.offsets.assign(work + parameters[11], work + parameters[11] * 2 + 1);
        result.indices.assign(indices, indices + control[4]);
        return true;
    }
};
} // namespace vk_detail
#endif
