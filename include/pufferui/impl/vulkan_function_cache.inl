// Bounded GPU-resident native float blend functions. Included by the one TU.
// Preparation creates every resource. Recording only dispatches prepared work.
// Cache misses preserve native replay; CPU never reads or edits resident keys.
// The same blend-state factory is used by ordinary native graphics pipelines.
#if defined(PUFFERUI_ENABLE_VULKAN)
namespace vk_detail
{
struct native_function_cache
{
    static constexpr u32 WIDTH = 256, WAYS = 8, HEADER_WORDS = 32;
    gpu_bin_job storage;
    texture *atlas = nullptr;
    VkDescriptorSetLayout descriptor_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet descriptor = VK_NULL_HANDLE;
    VkPipeline begin = VK_NULL_HANDLE, prepare = VK_NULL_HANDLE;
    VkPipeline reset = VK_NULL_HANDLE, ready = VK_NULL_HANDLE, calibrate = VK_NULL_HANDLE;
    u32 rows = 0, requests = 0;
    bool valid = false;
    native_function_cache() = default;
    native_function_cache(const native_function_cache &) = delete;
    native_function_cache &operator=(const native_function_cache &) = delete;
    ~native_function_cache() { release(); }

    void barrier(VkPipelineStageFlags2 source, VkAccessFlags2 access,
                 VkPipelineStageFlags2 destination, VkAccessFlags2 destination_access)
    {
        storage.barrier(source, access, destination, destination_access);
    }
    void release()
    {
        auto *d = storage.dev;
        if (!d) return;
        // This waits for both unfinished recording and already submitted users
        // before destroying the shared arena, atlas, descriptors or pipelines.
        storage.release();
        for (auto p : {begin, prepare, reset, ready, calibrate})
            if (p) d->vkDestroyPipeline(d->native.device, p, nullptr);
        if (descriptor) --d->stats.descriptor_sets;
        if (pool) d->vkDestroyDescriptorPool(d->native.device, pool, nullptr);
        if (layout) d->vkDestroyPipelineLayout(d->native.device, layout, nullptr);
        if (descriptor_layout)
            d->vkDestroyDescriptorSetLayout(d->native.device, descriptor_layout, nullptr);
        if (atlas) d->destroy_texture(atlas);
        atlas = nullptr;
        begin = prepare = reset = ready = calibrate = VK_NULL_HANDLE;
        descriptor_layout = VK_NULL_HANDLE;
        layout = VK_NULL_HANDLE;
        pool = VK_NULL_HANDLE;
        descriptor = VK_NULL_HANDLE;
        rows = requests = 0;
        valid = false;
    }
    bool initialize(device &d, u32 row_count = 4096, u32 request_count = 256,
                    u64 image_budget = ~0ull)
    {
        if (storage.dev || d.paint_depth || !d.can_compute || row_count < WAYS ||
            (row_count & (row_count - 1)) || row_count > d.properties.limits.maxImageDimension2D ||
            !request_count || request_count > 256 || (request_count & (request_count - 1)))
            return false;
        const u64 bytes = u64(HEADER_WORDS + row_count * 8 + request_count * 41) * 4;
        if (bytes > d.properties.limits.maxStorageBufferRange) return false;
        storage.dev = &d;
        if (!storage.storage(storage.buffers[0], bytes, storage_role::DEVICE)) return false;
        rows = row_count;
        requests = request_count;
        atlas = d.make_texture(
            {.width = WIDTH,
             .height = static_cast<i32>(rows),
             .render_target = true,
             .extra_usage = VK_IMAGE_USAGE_STORAGE_BIT,
             .allocation_budget =
                 std::min(image_budget,
                          d.options.cache_budget_bytes > d.stats.gpu_sdf_cache_image_bytes
                              ? d.options.cache_budget_bytes - d.stats.gpu_sdf_cache_image_bytes
                              : 0)});
        if (!atlas) return false;
        atlas->intrinsic = true;
        const VkDescriptorSetLayoutBinding bindings[] = {
            {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
             VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT,
             nullptr}};
        VkDescriptorSetLayoutCreateInfo dc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        dc.bindingCount = 3;
        dc.pBindings = bindings;
        if (!d.check(
                d.vkCreateDescriptorSetLayout(d.native.device, &dc, nullptr, &descriptor_layout),
                "native function descriptors"))
            return false;
        const VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT, 0, 8};
        VkPipelineLayoutCreateInfo lc{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        lc.setLayoutCount = 1;
        lc.pSetLayouts = &descriptor_layout;
        lc.pushConstantRangeCount = 1;
        lc.pPushConstantRanges = &push;
        if (!d.check(d.vkCreatePipelineLayout(d.native.device, &lc, nullptr, &layout),
                     "native function layout"))
            return false;
        const VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
                                              {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
                                              {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}};
        VkDescriptorPoolCreateInfo pc{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pc.maxSets = 1;
        pc.poolSizeCount = 3;
        pc.pPoolSizes = sizes;
        if (!d.check(d.vkCreateDescriptorPool(d.native.device, &pc, nullptr, &pool),
                     "native function pool"))
            return false;
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = pool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &descriptor_layout;
        if (!d.check(d.vkAllocateDescriptorSets(d.native.device, &ai, &descriptor),
                     "native function set"))
            return false;
        ++d.stats.descriptor_sets;
        const VkDescriptorBufferInfo buffer{storage.buffers[0].buffer, 0, bytes};
        const VkDescriptorImageInfo images[] = {
            {VK_NULL_HANDLE, atlas->view, VK_IMAGE_LAYOUT_GENERAL},
            {d.samplers[0], atlas->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
        VkWriteDescriptorSet writes[3]{};
        for (u32 i = 0; i < 3; ++i)
        {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = descriptor;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = bindings[i].descriptorType;
            if (i)
                writes[i].pImageInfo = &images[i - 1];
            else
                writes[i].pBufferInfo = &buffer;
        }
        d.vkUpdateDescriptorSets(d.native.device, 3, writes, 0, nullptr);
        begin = compute_pipeline(d, layout, vulkan_function_begin, sizeof(vulkan_function_begin));
        prepare =
            compute_pipeline(d, layout, vulkan_function_prepare, sizeof(vulkan_function_prepare));
        reset = compute_pipeline(d, layout, vulkan_function_reset, sizeof(vulkan_function_reset));
        ready = compute_pipeline(d, layout, vulkan_function_ready, sizeof(vulkan_function_ready));
        if (!begin || !prepare || !reset || !ready || !initialize_graphics(d)) return false;
        if (!d.commands()) return false;
        d.end_rendering();
        const auto command = d.frame->command;
        d.vkCmdFillBuffer(command, buffer.buffer, 0, VK_WHOLE_SIZE, 0);
        barrier(VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        d.vkCmdFillBuffer(command, buffer.buffer, 6 * 4, 4, rows);
        d.vkCmdFillBuffer(command, buffer.buffer, 7 * 4, 4, requests);
        d.vkCmdFillBuffer(command, buffer.buffer, 8 * 4, 4, requests * 4);
        barrier(VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
        d.transition(atlas, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        // Commit initial zeroing outside the draw path. A discarded future tape
        // cannot make a CPU 'initialized' flag disagree with the GPU arena.
        storage.recorded = true;
        storage.serial = d.submit();
        valid = storage.serial && d.wait(storage.serial);
        return valid;
    }
    bool initialize_graphics(device &d)
    {
        VkShaderModule modules[2]{};
        const u32 *sources[] = {vulkan_function_vert, vulkan_function_frag};
        const usize sizes[] = {sizeof(vulkan_function_vert), sizeof(vulkan_function_frag)};
        for (u32 i = 0; i < 2; ++i)
        {
            VkShaderModuleCreateInfo mc{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            mc.codeSize = sizes[i];
            mc.pCode = sources[i];
            if (!d.check(d.vkCreateShaderModule(d.native.device, &mc, nullptr, &modules[i]),
                         "native function graphics module"))
            {
                if (modules[0]) d.vkDestroyShaderModule(d.native.device, modules[0], nullptr);
                return false;
            }
        }
        VkPipelineShaderStageCreateInfo stages[2]{};
        for (u32 i = 0; i < 2; ++i)
        {
            stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[i].stage = i ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
            stages[i].module = modules[i];
            stages[i].pName = "main";
        }
        VkPipelineVertexInputStateCreateInfo vi{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo ia{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vp.viewportCount = vp.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rs{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = VK_CULL_MODE_NONE;
        rs.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo ms{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        const auto attachment = native_blend_attachment(true);
        VkPipelineColorBlendStateCreateInfo cb{
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        cb.attachmentCount = 1;
        cb.pAttachments = &attachment;
        const VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        ds.dynamicStateCount = 2;
        ds.pDynamicStates = states;
        const VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
        VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachmentFormats = &format;
        VkGraphicsPipelineCreateInfo pc{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pc.pNext = &rendering;
        pc.stageCount = 2;
        pc.pStages = stages;
        pc.pVertexInputState = &vi;
        pc.pInputAssemblyState = &ia;
        pc.pViewportState = &vp;
        pc.pRasterizationState = &rs;
        pc.pMultisampleState = &ms;
        pc.pColorBlendState = &cb;
        pc.pDynamicState = &ds;
        pc.layout = layout;
        const bool success = d.check(d.vkCreateGraphicsPipelines(d.native.device, VK_NULL_HANDLE, 1,
                                                                 &pc, nullptr, &calibrate),
                                     "native function calibration pipeline");
        for (auto module : modules) d.vkDestroyShaderModule(d.native.device, module, nullptr);
        return success;
    }
    void bind(VkPipeline pipeline)
    {
        auto &d = *storage.dev;
        d.vkCmdBindPipeline(d.frame->command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        d.vkCmdBindDescriptorSets(d.frame->command, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1,
                                  &descriptor, 0, nullptr);
    }
    bool record_begin()
    {
        if (!valid || !storage.dev->commands()) return false;
        auto &d = *storage.dev;
        d.end_rendering();
        storage.recorded = true;
        storage.serial = d.next_serial;
        barrier(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
        bind(begin);
        d.vkCmdDispatch(d.frame->command, 1, 1, 1);
        barrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
        return d.result == VK_SUCCESS;
    }
    bool record_prepare()
    {
        if (!valid || !storage.dev->commands()) return false;
        auto &d = *storage.dev;
        d.end_rendering();
        storage.recorded = true;
        storage.serial = d.next_serial;
        barrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
        bind(prepare);
        d.vkCmdDispatch(d.frame->command, 1, 1, 1);
        barrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT);
        d.transition(atlas, VK_IMAGE_LAYOUT_GENERAL);
        bind(reset);
        d.vkCmdDispatchIndirect(d.frame->command, storage.buffers[0].buffer, 20 * 4);
        if (!d.start_rendering(atlas)) return false;
        d.vkCmdBindPipeline(d.frame->command, VK_PIPELINE_BIND_POINT_GRAPHICS, calibrate);
        const VkViewport viewport{0, 0, WIDTH, static_cast<f32>(rows), 0, 1};
        const VkRect2D scissor{{0, 0}, {WIDTH, rows}};
        d.vkCmdSetViewport(d.frame->command, 0, 1, &viewport);
        d.vkCmdSetScissor(d.frame->command, 0, 1, &scissor);
        const f32 extent[] = {WIDTH, static_cast<f32>(rows)};
        d.vkCmdPushConstants(d.frame->command, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, 8, extent);
        d.vkCmdBindDescriptorSets(d.frame->command, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1,
                                  &descriptor, 0, nullptr);
        d.vkCmdDrawIndirect(d.frame->command, storage.buffers[0].buffer, 16 * 4, 1,
                            sizeof(VkDrawIndirectCommand));
        d.frame->graphics = {}; // private layout/viewport must not poison native state reuse
        d.end_rendering();
        d.transition(atlas, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        bind(ready);
        d.vkCmdDispatch(d.frame->command, (requests + 63) / 64, 1, 1);
        barrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
        return d.result == VK_SUCCESS;
    }
};
} // namespace vk_detail
#endif
