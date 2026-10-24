// Compute painter compatibility stage: exact calibrated RGBA8 rect programs.
// Each invocation owns its pixels; the indirect tile count is GPU-produced.
// Unsupported programs are rejected before recording any scene write.
// Included inside namespace pui by the one implementation translation unit.
#if defined(PUFFERUI_ENABLE_VULKAN)
namespace vk_detail
{
struct rgba8_painter_job
{
    device *dev = nullptr;
    gpu_bin_job *bin = nullptr;
    rgba8_blend_lut *blend = nullptr;
    texture *target = nullptr;
    VkDescriptorSetLayout descriptor_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet descriptor = VK_NULL_HANDLE;
    VkPipeline painter = VK_NULL_HANDLE, args = VK_NULL_HANDLE;
    u64 serial = 0;
    u32 parameters[12]{};
    rgba8_painter_job() = default;
    rgba8_painter_job(const rgba8_painter_job &) = delete;
    rgba8_painter_job &operator=(const rgba8_painter_job &) = delete;
    ~rgba8_painter_job() { release(); }
    void release()
    {
        if (!dev) return;
        if (serial && serial >= dev->next_serial && dev->frame && dev->frame->recording)
            serial = dev->submit();
        if (serial && serial < dev->next_serial) dev->wait(serial);
        if (descriptor) --dev->stats.descriptor_sets;
        if (painter) dev->vkDestroyPipeline(dev->native.device, painter, nullptr);
        if (args) dev->vkDestroyPipeline(dev->native.device, args, nullptr);
        if (pool) dev->vkDestroyDescriptorPool(dev->native.device, pool, nullptr);
        if (layout) dev->vkDestroyPipelineLayout(dev->native.device, layout, nullptr);
        if (descriptor_layout)
            dev->vkDestroyDescriptorSetLayout(dev->native.device, descriptor_layout, nullptr);
        dev = nullptr;
        descriptor_layout = VK_NULL_HANDLE;
        layout = VK_NULL_HANDLE;
        pool = VK_NULL_HANDLE;
        descriptor = VK_NULL_HANDLE;
        painter = args = VK_NULL_HANDLE;
        serial = 0;
    }
    bool initialize(device &owner, gpu_bin_job &bins, const paint_detail::command_tape &tape,
                    texture *image, rgba8_blend_lut &lut)
    {
        if (dev || !tape.valid || tape.commands.empty() || bins.dev != &owner || !lut.valid ||
            lut.storage.dev != &owner || !owner.valid(image) ||
            image->format != VK_FORMAT_R8G8B8A8_UNORM || !image->target ||
            !(image->usage & VK_IMAGE_USAGE_STORAGE_BIT) ||
            bins.parameters[0] != static_cast<u32>(image->width) ||
            bins.parameters[1] != static_cast<u32>(image->height))
            return false;
        const auto &clear = tape.commands.front();
        if (clear.operation != static_cast<u32>(paint_detail::opcode::CLEAR) || clear.x0 != 0 ||
            clear.y0 != 0 || clear.x1 != image->width || clear.y1 != image->height)
            return false;
        for (usize i = 0; i < tape.commands.size(); ++i)
        {
            const auto &c = tape.commands[i];
            if ((i && c.operation != static_cast<u32>(paint_detail::opcode::RECT_COMPAT)) ||
                c.payload_size || c.resource || c.clip || c.pass || c.compatibility ||
                !std::isfinite(c.x0) || !std::isfinite(c.y0) || !std::isfinite(c.x1) ||
                !std::isfinite(c.y1) || c.x0 != std::trunc(c.x0) || c.y0 != std::trunc(c.y0) ||
                c.x1 != std::trunc(c.x1) || c.y1 != std::trunc(c.y1))
                return false;
        }
        if (bins.parameters[8] != tape.commands.size() ||
            std::memcmp(bins.buffers[0].mapped, tape.commands.data(),
                        tape.commands.size() * sizeof(paint_detail::command)))
            return false;
        const u64 max_x = owner.properties.limits.maxComputeWorkGroupCount[0];
        const u64 max_y = owner.properties.limits.maxComputeWorkGroupCount[1];
        if (bins.parameters[11] > max_x * max_y) return false;
        dev = &owner;
        bin = &bins;
        blend = &lut;
        target = image;
        std::memcpy(parameters, bins.parameters, sizeof(parameters));
        parameters[7] = static_cast<u32>(max_x);
        VkDescriptorSetLayoutBinding bindings[6]{};
        for (u32 i = 0; i < 6; ++i)
            bindings[i] = {
                i, i == 5 ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo dc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        dc.bindingCount = 6;
        dc.pBindings = bindings;
        if (!owner.check(owner.vkCreateDescriptorSetLayout(owner.native.device, &dc, nullptr,
                                                           &descriptor_layout),
                         "painter descriptors"))
            return false;
        VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(parameters)};
        VkPipelineLayoutCreateInfo lc{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        lc.setLayoutCount = 1;
        lc.pSetLayouts = &descriptor_layout;
        lc.pushConstantRangeCount = 1;
        lc.pPushConstantRanges = &range;
        if (!owner.check(owner.vkCreatePipelineLayout(owner.native.device, &lc, nullptr, &layout),
                         "painter layout"))
            return false;
        painter = compute_pipeline(owner, layout, vulkan_paint_compat, sizeof(vulkan_paint_compat));
        args = compute_pipeline(owner, bin->layout, vulkan_paint_args, sizeof(vulkan_paint_args));
        if (!painter || !args) return false;
        const VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5},
                                              {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1}};
        VkDescriptorPoolCreateInfo pc{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pc.maxSets = 1;
        pc.poolSizeCount = 2;
        pc.pPoolSizes = sizes;
        if (!owner.check(owner.vkCreateDescriptorPool(owner.native.device, &pc, nullptr, &pool),
                         "painter pool"))
            return false;
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = pool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &descriptor_layout;
        if (!owner.check(owner.vkAllocateDescriptorSets(owner.native.device, &ai, &descriptor),
                         "painter set"))
            return false;
        ++owner.stats.descriptor_sets;
        VkDescriptorBufferInfo infos[5]{};
        VkWriteDescriptorSet writes[6]{};
        for (u32 i = 0; i < 5; ++i)
        {
            const page &buffer = i == 1 ? lut.storage.buffers[1] : bins.buffers[i];
            infos[i] = {buffer.buffer, 0, buffer.capacity};
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = descriptor;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[i].pBufferInfo = &infos[i];
        }
        VkDescriptorImageInfo image_info{};
        image_info.imageView = image->view;
        image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[5].dstSet = descriptor;
        writes[5].dstBinding = 5;
        writes[5].descriptorCount = 1;
        writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[5].pImageInfo = &image_info;
        owner.vkUpdateDescriptorSets(owner.native.device, 6, writes, 0, nullptr);
        return true;
    }
    bool record()
    {
        if (!dev || !painter || !bin->recorded || serial || !dev->commands()) return false;
        dev->end_rendering();
        dev->transition(target, VK_IMAGE_LAYOUT_GENERAL);
        bin->barrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
                     VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
        dev->vkCmdBindPipeline(dev->frame->command, VK_PIPELINE_BIND_POINT_COMPUTE, args);
        dev->vkCmdBindDescriptorSets(dev->frame->command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                     bin->layout, 0, 1, &bin->descriptor, 0, nullptr);
        dev->vkCmdPushConstants(dev->frame->command, bin->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                sizeof(parameters), parameters);
        dev->vkCmdDispatch(dev->frame->command, 1, 1, 1);
        bin->barrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
                     VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                     VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT);
        dev->vkCmdBindPipeline(dev->frame->command, VK_PIPELINE_BIND_POINT_COMPUTE, painter);
        dev->vkCmdBindDescriptorSets(dev->frame->command, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0,
                                     1, &descriptor, 0, nullptr);
        dev->vkCmdPushConstants(dev->frame->command, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                sizeof(parameters), parameters);
        dev->vkCmdDispatchIndirect(dev->frame->command, bin->buffers[4].buffer, 0);
        serial = dev->next_serial;
        bin->serial = blend->storage.serial = serial;
        target->last_use = serial;
        ++target->revision;
        return true;
    }
};
} // namespace vk_detail
#endif
