// Optional native dual-filter Kawase blur, independent of the painter mode.
// Surfaces own a bounded pyramid, completed slots own reusable descriptor sets.
// The legacy blur remains the default and retains its existing pixel oracles.
// Included by the single implementation translation unit.
#if defined(PUFFERUI_ENABLE_VULKAN)
namespace vk_detail
{
bool device::initialize_kawase()
{
    if (options.blur != vulkan_blur_mode::KAWASE) return true;
    const VkDescriptorSetLayoutBinding bindings[] = {
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
    VkDescriptorSetLayoutCreateInfo dc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dc.bindingCount = 2;
    dc.pBindings = bindings;
    if (!check(vkCreateDescriptorSetLayout(native.device, &dc, nullptr, &kawase_descriptor_layout),
               "Kawase descriptor layout"))
        return false;
    const VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, 32};
    VkPipelineLayoutCreateInfo lc{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    lc.setLayoutCount = 1;
    lc.pSetLayouts = &kawase_descriptor_layout;
    lc.pushConstantRangeCount = 1;
    lc.pPushConstantRanges = &range;
    if (!check(vkCreatePipelineLayout(native.device, &lc, nullptr, &kawase_layout),
               "Kawase pipeline layout"))
        return false;
    kawase_pipeline = compute_pipeline(*this, kawase_layout, vulkan_kawase, sizeof(vulkan_kawase));
    return kawase_pipeline != VK_NULL_HANDLE;
}
bool device::prepare_kawase(surface &s)
{
    if (!kawase_pipeline || !s.scene) return true;
    i32 width = s.scene->width, height = s.scene->height;
    for (auto &image : s.kawase)
    {
        width = (width + 1) / 2;
        height = (height + 1) / 2;
        if (image && (image->width != width || image->height != height))
        {
            destroy_texture(image);
            image = nullptr;
        }
        if (!image)
            image = make_texture({.width = width,
                                  .height = height,
                                  .render_target = true,
                                  .extra_usage = VK_IMAGE_USAGE_STORAGE_BIT});
        if (!image) return false;
    }
    for (auto &slot : s.slots)
    {
        if (slot.kawase_pool) continue;
        constexpr u32 count = 64;
        const VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, count},
                                              {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, count}};
        VkDescriptorPoolCreateInfo pc{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pc.maxSets = count;
        pc.poolSizeCount = 2;
        pc.pPoolSizes = sizes;
        if (!check(vkCreateDescriptorPool(native.device, &pc, nullptr, &slot.kawase_pool),
                   "Kawase descriptor pool"))
            return false;
        VkDescriptorSetLayout layouts[count];
        std::fill(std::begin(layouts), std::end(layouts), kawase_descriptor_layout);
        slot.kawase_sets.resize(count);
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = slot.kawase_pool;
        ai.descriptorSetCount = count;
        ai.pSetLayouts = layouts;
        if (!check(vkAllocateDescriptorSets(native.device, &ai, slot.kawase_sets.data()),
                   "Kawase prepared descriptors"))
            return false;
        stats.descriptor_sets += count;
    }
    return true;
}
bool device::kawase_blur(texture *scene, rect source, f32 radius, paint_detail::blur_result &out)
{
    if (!kawase_pipeline || !current_surface || scene != current_surface->scene) return false;
    if (cache_active) cache_flush(false);
    flush_program();
    if (!commands() || frame->kawase_sets.empty()) return false;
    const u32 levels = radius > 20 ? 4 : radius > 4 ? 3 : 2;
    const u32 passes = levels * 2 - 1;
    if (frame->kawase_cursor + passes > frame->kawase_sets.size())
    {
        const auto serial = submit();
        if (!serial || !wait(serial) || !begin(*frame)) return false;
        ++stats.budget_waits;
    }
    end_rendering();
    struct parameters
    {
        float x, y, w, h;
        u32 width, height;
        float offset;
        u32 up;
    } p{};
    static_assert(sizeof(parameters) == 32);
    u32 widths[4], heights[4];
    u32 width = static_cast<u32>(source.w), height = static_cast<u32>(source.h);
    for (u32 i = 0; i < levels; ++i)
    {
        widths[i] = width = (width + 1) / 2;
        heights[i] = height = (height + 1) / 2;
    }
    const float offset = std::clamp(radius / static_cast<float>(1u << levels), 0.5f, 2.0f);
    const auto pass = [&](texture *input, texture *output, rect valid, u32 w, u32 h, bool up)
    {
        retained_draw event;
        event.key[0] = 3;
        const parameters key{valid.x, valid.y, valid.w, valid.h, w, h, offset, up ? 1u : 0u};
        std::memcpy(&event.key[6], &key, sizeof(key));
        retained_add(event, output, input, true);
        transition(input, VK_IMAGE_LAYOUT_GENERAL);
        output->producer = nullptr; // no native-output certificate for this compute producer
        transition(output, VK_IMAGE_LAYOUT_GENERAL);
        const VkDescriptorImageInfo images[] = {
            {samplers[1], input->view, VK_IMAGE_LAYOUT_GENERAL},
            {VK_NULL_HANDLE, output->view, VK_IMAGE_LAYOUT_GENERAL}};
        const auto set = frame->kawase_sets[frame->kawase_cursor++];
        VkWriteDescriptorSet writes[2]{};
        for (u32 i = 0; i < 2; ++i)
        {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = set;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType =
                i ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].pImageInfo = &images[i];
        }
        vkUpdateDescriptorSets(native.device, 2, writes, 0, nullptr);
        p = {valid.x, valid.y, valid.w, valid.h, w, h, offset, up ? 1u : 0u};
        vkCmdBindPipeline(frame->command, VK_PIPELINE_BIND_POINT_COMPUTE, kawase_pipeline);
        vkCmdBindDescriptorSets(frame->command, VK_PIPELINE_BIND_POINT_COMPUTE, kawase_layout, 0, 1,
                                &set, 0, nullptr);
        vkCmdPushConstants(frame->command, kawase_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(p),
                           &p);
        vkCmdDispatch(frame->command, (w + 7) / 8, (h + 7) / 8, 1);
        ++output->revision;
        ++stats.kawase_passes;
        stats.kawase_pixels += static_cast<u64>(w) * h;
    };
    auto &images = current_surface->kawase;
    pass(scene, images[0], source, widths[0], heights[0], false);
    for (u32 i = 1; i < levels; ++i)
        pass(images[i - 1], images[i],
             {0, 0, static_cast<float>(widths[i - 1]), static_cast<float>(heights[i - 1])},
             widths[i], heights[i], false);
    for (u32 i = levels - 1; i > 0; --i)
        pass(images[i], images[i - 1],
             {0, 0, static_cast<float>(widths[i]), static_cast<float>(heights[i])}, widths[i - 1],
             heights[i - 1], true);
    out.image = images[0];
    out.scale_u = static_cast<float>(widths[0]) / (source.w * images[0]->width);
    out.scale_v = static_cast<float>(heights[0]) / (source.h * images[0]->height);
    return true;
}
} // namespace vk_detail
namespace paint_detail
{
bool vulkan_blur(render_device *base, texture_handle image, rect source, f32 radius,
                 blur_result &out)
{
    auto *d = dynamic_cast<vk_detail::device *>(base);
    return d && d->kawase_blur(static_cast<vk_detail::texture *>(image), source, radius, out);
}
} // namespace paint_detail
#endif
