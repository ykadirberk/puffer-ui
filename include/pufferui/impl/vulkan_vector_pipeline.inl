// Font-independent instanced vector pipeline; no text state or gamma access.
// Native resources are created during preparation, before UI drawing starts.
// Instance uploads use the renderer's direct/staged geometry arena.
// Included inside pui after the optional Vulkan implementation slices.
#if defined(PUFFERUI_ENABLE_VULKAN) && defined(PUFFERUI_ENABLE_HARFBUZZ)
namespace vk_detail
{
struct vector_instance
{
    f32 bounds[4]{}, inverse_x[4]{}, inverse_y[4]{}, viewport[4]{};
    f32 tint[4]{}, current[4]{}, origins[4]{};
    u32 metadata[4]{};
};
static_assert(sizeof(vector_instance) == 128 && offsetof(vector_instance, metadata) == 112);
struct vector_graphics
{
    device &dev;
    VkShaderModule vertex = VK_NULL_HANDLE, fragment = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptor = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    struct format_pipeline
    {
        VkFormat format;
        VkPipeline pipeline;
    };
    std::vector<format_pipeline> pipelines;
    explicit vector_graphics(device &d) : dev(d) {}
    vector_graphics(const vector_graphics &) = delete;
    vector_graphics &operator=(const vector_graphics &) = delete;
    ~vector_graphics()
    {
        for (const auto &p : pipelines)
            dev.vkDestroyPipeline(dev.native.device, p.pipeline, nullptr);
        if (layout) dev.vkDestroyPipelineLayout(dev.native.device, layout, nullptr);
        if (descriptor) dev.vkDestroyDescriptorSetLayout(dev.native.device, descriptor, nullptr);
        if (vertex) dev.vkDestroyShaderModule(dev.native.device, vertex, nullptr);
        if (fragment) dev.vkDestroyShaderModule(dev.native.device, fragment, nullptr);
    }
    VkPipeline get(VkFormat format) const
    {
        for (const auto &p : pipelines)
            if (p.format == format) return p.pipeline;
        return VK_NULL_HANDLE;
    }
    bool prepare(VkFormat format)
    {
        if (get(format)) return true;
        if (!descriptor)
        {
            const VkDescriptorSetLayoutBinding binding{1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                                                       VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
            VkDescriptorSetLayoutCreateInfo info{
                VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            info.bindingCount = 1;
            info.pBindings = &binding;
            if (!dev.check(
                    dev.vkCreateDescriptorSetLayout(dev.native.device, &info, nullptr, &descriptor),
                    "vkCreateDescriptorSetLayout(vector)"))
                return false;
        }
        if (!layout)
        {
            const VkDescriptorSetLayout sets[] = {dev.descriptor_layout, descriptor};
            const VkPushConstantRange projection{VK_SHADER_STAGE_VERTEX_BIT, 0, 8};
            VkPipelineLayoutCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            info.setLayoutCount = 2;
            info.pSetLayouts = sets;
            info.pushConstantRangeCount = 1;
            info.pPushConstantRanges = &projection;
            if (!dev.check(dev.vkCreatePipelineLayout(dev.native.device, &info, nullptr, &layout),
                           "vkCreatePipelineLayout(vector)"))
                return false;
        }
        const auto module = [&](const u32 *words, usize bytes, VkShaderModule &out)
        {
            if (out) return true;
            VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            info.pCode = words;
            info.codeSize = bytes;
            return dev.check(dev.vkCreateShaderModule(dev.native.device, &info, nullptr, &out),
                             "vkCreateShaderModule(vector)");
        };
        if (!module(vulkan_path_vert, sizeof(vulkan_path_vert), vertex) ||
            !module(vulkan_path_frag, sizeof(vulkan_path_frag), fragment))
            return false;
        pipelines.reserve(pipelines.size() + 1);
        VkPipelineShaderStageCreateInfo stages[2]{};
        for (auto &stage : stages)
        {
            stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stage.pName = "main";
        }
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertex;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragment;
        const VkVertexInputBindingDescription binding{0, sizeof(vector_instance),
                                                      VK_VERTEX_INPUT_RATE_INSTANCE};
        VkVertexInputAttributeDescription attributes[8]{};
        for (u32 i = 0; i < 8; ++i)
            attributes[i] = {
                i, 0, i == 7 ? VK_FORMAT_R32G32B32A32_UINT : VK_FORMAT_R32G32B32A32_SFLOAT, i * 16};
        VkPipelineVertexInputStateCreateInfo vi{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vi.vertexBindingDescriptionCount = 1;
        vi.pVertexBindingDescriptions = &binding;
        vi.vertexAttributeDescriptionCount = 8;
        vi.pVertexAttributeDescriptions = attributes;
        VkPipelineInputAssemblyStateCreateInfo assembly{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport.viewportCount = viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo samples{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState attachment{};
        attachment.blendEnable = VK_TRUE;
        attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        attachment.colorBlendOp = attachment.alphaBlendOp = VK_BLEND_OP_ADD;
        attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        VkPipelineColorBlendStateCreateInfo blend{
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount = 1;
        blend.pAttachments = &attachment;
        const VkDynamicState dynamics[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = dynamics;
        VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachmentFormats = &format;
        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        info.pNext = &rendering;
        info.stageCount = 2;
        info.pStages = stages;
        info.pVertexInputState = &vi;
        info.pInputAssemblyState = &assembly;
        info.pViewportState = &viewport;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &samples;
        info.pColorBlendState = &blend;
        info.pDynamicState = &dynamic;
        info.layout = layout;
        VkPipeline pipeline = VK_NULL_HANDLE;
        if (!dev.check(dev.vkCreateGraphicsPipelines(dev.native.device, VK_NULL_HANDLE, 1, &info,
                                                     nullptr, &pipeline),
                       "vkCreateGraphicsPipelines(vector Slug)"))
            return false;
        pipelines.push_back({format, pipeline});
        return true;
    }
    bool draw(texture *sample, VkDescriptorSet metadata, const vector_instance *instances,
              usize count, VkRect2D scissor)
    {
        if (!count) return true;
        if (!instances || count > UINT32_MAX || !metadata || !dev.valid(dev.target) ||
            !dev.valid(sample) || sample == dev.target || !get(dev.target->format))
            return false;
        paint_guard drawing(dev);
        const VkDeviceSize bytes = count * sizeof(vector_instance);
        VkDeviceSize offset = 0;
        page *upload = dev.upload(bytes, offset, true);
        if (!upload) return false;
        std::memcpy(upload->mapped + offset, instances, static_cast<usize>(bytes));
        VkBuffer buffer = upload->staged ? upload->device_buffer : upload->buffer;
        if (upload->staged)
        {
            dev.end_rendering();
            VkBufferMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
            barrier.srcAccessMask =
                VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer = buffer;
            barrier.size = VK_WHOLE_SIZE;
            VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            dependency.bufferMemoryBarrierCount = 1;
            dependency.pBufferMemoryBarriers = &barrier;
            dev.vkCmdPipelineBarrier2(dev.frame->command, &dependency);
            const VkBufferCopy copy{offset, offset, bytes};
            dev.vkCmdCopyBuffer(dev.frame->command, upload->buffer, upload->device_buffer, 1,
                                &copy);
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
            barrier.offset = offset;
            barrier.size = bytes;
            dev.vkCmdPipelineBarrier2(dev.frame->command, &dependency);
        }
        dev.transition(sample, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (!dev.start_rendering(dev.target)) return false;
        const VkExtent2D extent{static_cast<u32>(dev.target->width),
                                static_cast<u32>(dev.target->height)};
        const auto pipeline = get(dev.target->format);
        dev.bind_graphics(pipeline, layout, extent, scissor, sample->descriptors[0], 0);
        dev.bind_graphics(pipeline, layout, extent, scissor, metadata, 1);
        dev.vkCmdBindVertexBuffers(dev.frame->command, 0, 1, &buffer, &offset);
        dev.vkCmdDraw(dev.frame->command, 6, static_cast<u32>(count), 0, 0);
        ++dev.stats.draws;
        ++dev.target->revision;
        dev.target->last_use = sample->last_use = dev.next_serial;
        return true;
    }
};
struct vector_buffer
{
    device &dev;
    page storage;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet descriptor = VK_NULL_HANDLE;
    // The owning cache retires this object after the last recorded GPU serial.
    explicit vector_buffer(device &d) : dev(d) {}
    vector_buffer(const vector_buffer &) = delete;
    vector_buffer &operator=(const vector_buffer &) = delete;
    ~vector_buffer()
    {
        if (pool) dev.vkDestroyDescriptorPool(dev.native.device, pool, nullptr);
        dev.free_page(storage);
    }
    bool prepare(const renderer_detail::vector_slug::prepared_scene &source,
                 vector_graphics &graphics)
    {
        if (storage.buffer || pool || descriptor) return false;
        const usize bytes = source.words.size() * sizeof(u32);
        if (!bytes || bytes > dev.properties.limits.maxStorageBufferRange) return false;
        if (!dev.allocate_buffer(storage, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) return false;
        std::memcpy(storage.mapped, source.words.data(), bytes);
        storage.used = bytes;
        if (!dev.flush_page(storage)) return false;
        const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
        VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool_info.maxSets = 1;
        pool_info.poolSizeCount = 1;
        pool_info.pPoolSizes = &size;
        if (!dev.check(dev.vkCreateDescriptorPool(dev.native.device, &pool_info, nullptr, &pool),
                       "vkCreateDescriptorPool(vector)"))
            return false;
        VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocation.descriptorPool = pool;
        allocation.descriptorSetCount = 1;
        allocation.pSetLayouts = &graphics.descriptor;
        if (!dev.check(dev.vkAllocateDescriptorSets(dev.native.device, &allocation, &descriptor),
                       "vkAllocateDescriptorSets(vector)"))
            return false;
        const VkDescriptorBufferInfo buffer{storage.buffer, 0, bytes};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = descriptor;
        write.dstBinding = 1;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.pBufferInfo = &buffer;
        dev.vkUpdateDescriptorSets(dev.native.device, 1, &write, 0, nullptr);
        return true;
    }
};
} // namespace vk_detail
#endif
