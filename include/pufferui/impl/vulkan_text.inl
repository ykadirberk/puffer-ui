// Native HarfBuzz/Slug graphics, independent of shape tessellation.
// Instances, outline residency and descriptors are owned by the Vulkan device.
// Resident chunks are append-only; submitted prefixes are never overwritten.
// Included inside namespace pui by the single implementation TU.
#if defined(PUFFERUI_ENABLE_VULKAN)
namespace vk_detail
{
render_painter *device::create_painter(context &c)
{
    auto *p = new renderer_detail::mesh_painter(c);
#if defined(PUFFERUI_ENABLE_HARFBUZZ)
    p->vectors = options.vector_text;
    p->native_device = this;
    // A context may be rebound to a different device or back to a recycled
    // device address. Its CPU outlines outlive the previous residency domain.
    if (p->vectors && c.ts)
        for (auto &font : c.ts->fonts)
            if (font.harfbuzz)
                for (auto &entry : font.harfbuzz->outlines) entry.second.uploaded = false;
    if (p->vectors)
        p->native_glyph = [](render_device *d, context *ctx, font_handle font, u32 index,
                             vec2 baseline, f32 scale, color tint)
        { return static_cast<device *>(d)->glyph(ctx, font, index, baseline, scale, tint); };
#endif
    return p;
}
#if defined(PUFFERUI_ENABLE_HARFBUZZ)
bool device::initialize_text()
{
    text = new native_text();
    text->chunks.reserve(32);
    text->pending.reserve(2048);
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 1;
    binding.descriptorCount = 1;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dl.bindingCount = 1;
    dl.pBindings = &binding;
    if (!check(vkCreateDescriptorSetLayout(native.device, &dl, nullptr, &text->descriptor_layout),
               "vkCreateDescriptorSetLayout(Slug)"))
        return false;
    const VkDescriptorSetLayout sets[] = {descriptor_layout, text->descriptor_layout};
    VkPushConstantRange range{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 2};
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 2;
    pl.pSetLayouts = sets;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &range;
    if (!check(vkCreatePipelineLayout(native.device, &pl, nullptr, &text->pipeline_layout),
               "vkCreatePipelineLayout(Slug)"))
        return false;
    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 32};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = 32;
    pool.poolSizeCount = 1;
    pool.pPoolSizes = &size;
    if (!check(vkCreateDescriptorPool(native.device, &pool, nullptr, &text->pool),
               "vkCreateDescriptorPool(Slug)"))
        return false;
    VkShaderModuleCreateInfo sc{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    sc.codeSize = sizeof(vulkan_text_vert);
    sc.pCode = vulkan_text_vert;
    if (!check(vkCreateShaderModule(native.device, &sc, nullptr, &text->vertex),
               "vkCreateShaderModule(Slug vertex)"))
        return false;
    sc.codeSize = sizeof(vulkan_text_frag);
    sc.pCode = vulkan_text_frag;
    return check(vkCreateShaderModule(native.device, &sc, nullptr, &text->fragment),
                 "vkCreateShaderModule(Slug fragment)") &&
           text_chunk(1);
}
bool device::text_chunk(VkDeviceSize required)
{
    constexpr VkDeviceSize chunk_size = 512 * 1024, budget = 16 * 1024 * 1024;
    const VkDeviceSize alignment = std::max<VkDeviceSize>(8, properties.limits.nonCoherentAtomSize);
    const VkDeviceSize bytes =
        ((std::max(required, chunk_size) + alignment - 1) / alignment) * alignment;
    if (text->chunks.size() >= 32 || bytes > properties.limits.maxStorageBufferRange ||
        bytes > budget || text->bytes > budget - bytes)
        return false;
    glyph_chunk chunk;
    if (!allocate_buffer(chunk.storage, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) return false;
    VkDescriptorSetAllocateInfo ac{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ac.descriptorPool = text->pool;
    ac.descriptorSetCount = 1;
    ac.pSetLayouts = &text->descriptor_layout;
    if (!check(vkAllocateDescriptorSets(native.device, &ac, &chunk.descriptor),
               "vkAllocateDescriptorSets(Slug)"))
    {
        free_page(chunk.storage);
        return false;
    }
    VkDescriptorBufferInfo buffer{chunk.storage.buffer, 0, bytes};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = chunk.descriptor;
    write.dstBinding = 1;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &buffer;
    vkUpdateDescriptorSets(native.device, 1, &write, 0, nullptr);
    ++stats.descriptor_sets;
    text->bytes += bytes;
    text->chunks.push_back(chunk);
    return true;
}
bool device::glyph(context *c, font_handle font, u32 index, vec2 baseline, f32 scale, color tint)
{
    paint_guard drawing(*this);
    if (!text) return false;
    auto *outline = detail::harfbuzz_outline(c, font, index);
    if (!outline) return false;
    const auto &e = outline->extents;
    if (!e.width || !e.height || outline->data.empty()) return true;
    const rect box{baseline.x + e.x_bearing * scale - 1, baseline.y - e.y_bearing * scale - 1,
                   e.width * scale + 2, -e.height * scale + 2};
    if (detail::outside_paint(c, box)) return true;
    const rect *clip = c->clip_depth > 0 ? &c->clip_stack[c->clip_depth - 1] : nullptr;
    detail::dl_prepare(c, nullptr, clip);
    detail::mesh_flush(c);
    // The original retained-pixel cache records only mesh events. Replay its
    // ordered prefix before a glyph; it cannot certify a mixed native-text frame.
    if (cache_active) cache_flush(false);
    if (!outline->uploaded)
    {
        const VkDeviceSize alignment =
            std::max<VkDeviceSize>(8, properties.limits.nonCoherentAtomSize);
        auto *chunk = &text->chunks.back();
        VkDeviceSize offset = ((chunk->storage.used + alignment - 1) / alignment) * alignment;
        if (offset > chunk->storage.capacity ||
            outline->data.size() > chunk->storage.capacity - offset)
        {
            if (!text_chunk(outline->data.size())) return false;
            chunk = &text->chunks.back();
            offset = 0;
        }
        std::memcpy(chunk->storage.mapped + offset, outline->data.data(), outline->data.size());
        chunk->storage.used = offset + outline->data.size();
        if (!chunk->storage.coherent)
        {
            VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
            range.memory = chunk->storage.memory;
            range.offset = offset;
            range.size = ((outline->data.size() + alignment - 1) / alignment) * alignment;
            if (!check(vkFlushMappedMemoryRanges(native.device, 1, &range),
                       "vkFlushMappedMemoryRanges(Slug append)"))
                return false;
        }
        outline->location = static_cast<u32>(offset / 8);
        outline->chunk = static_cast<u32>(text->chunks.size() - 1);
        outline->uploaded = true;
        stats.glyph_upload_bytes += outline->data.size();
    }
    if (text->pending_chunk != outline->chunk || text->pending.size() >= text->batch_limit)
        flush_text();
    text->pending_chunk = outline->chunk;
    const f32 density = c->render_scale;
    if (c->disabled_depth > 0)
        tint.a = static_cast<u8>(tint.a * clampf(c->disabled_alpha, 0.0f, 1.0f));
    text->pending.push_back({box.x * density, box.y * density, box.right() * density,
                             box.bottom() * density, baseline.x * density, baseline.y * density,
                             scale * density, c->active_theme.text_gamma, tint.r / 255.0f,
                             tint.g / 255.0f, tint.b / 255.0f, tint.a / 255.0f, outline->location});
    ++stats.glyph_instances;
    return true;
}
void device::release_text()
{
    if (!text) return;
    for (auto &chunk : text->chunks) free_page(chunk.storage);
    if (text->pool) vkDestroyDescriptorPool(native.device, text->pool, nullptr);
    if (text->vertex) vkDestroyShaderModule(native.device, text->vertex, nullptr);
    if (text->fragment) vkDestroyShaderModule(native.device, text->fragment, nullptr);
    if (text->pipeline_layout)
        vkDestroyPipelineLayout(native.device, text->pipeline_layout, nullptr);
    if (text->descriptor_layout)
        vkDestroyDescriptorSetLayout(native.device, text->descriptor_layout, nullptr);
    delete text;
    text = nullptr;
}
#endif
void device::flush_text()
{
#if defined(PUFFERUI_ENABLE_HARFBUZZ)
    if (!text || text->pending.empty() || text_flushing) return;
    struct flush_guard
    {
        bool &active;
        explicit flush_guard(bool &a) : active(a) { active = true; }
        ~flush_guard() { active = false; }
    } flushing(text_flushing);
    paint_guard drawing(*this);
    if (!valid(target) ||
        (current_surface && current_surface->suspended && target == current_surface->scene))
    {
        text->pending.clear();
        return;
    }
    double left = 0, top = 0, right = target->width, bottom = target->height;
    if (has_clip)
    {
        if (!std::isfinite(clip.x) || !std::isfinite(clip.y) || !std::isfinite(clip.w) ||
            !std::isfinite(clip.h))
        {
            text->pending.clear();
            return;
        }
        const double x = std::trunc(static_cast<double>(clip.x)),
                     y = std::trunc(static_cast<double>(clip.y));
        left = std::max(0.0, x);
        top = std::max(0.0, y);
        right = std::min(right, x + std::trunc(static_cast<double>(clip.w)));
        bottom = std::min(bottom, y + std::trunc(static_cast<double>(clip.h)));
    }
    if (right <= left || bottom <= top)
    {
        text->pending.clear();
        return;
    }
    VkDeviceSize offset = 0;
    const VkDeviceSize bytes = text->pending.size() * sizeof(glyph_instance);
    page *p = upload(bytes, offset, true);
    if (!p)
    {
        text->pending.clear();
        return;
    }
    std::memcpy(p->mapped + offset, text->pending.data(), static_cast<usize>(bytes));
    const VkBuffer buffer = p->staged ? p->device_buffer : p->buffer;
    if (p->staged)
    {
        end_rendering();
        VkBufferMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = buffer;
        barrier.size = VK_WHOLE_SIZE;
        VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.bufferMemoryBarrierCount = 1;
        dep.pBufferMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(frame->command, &dep);
        VkBufferCopy copy{offset, offset, bytes};
        vkCmdCopyBuffer(frame->command, p->buffer, p->device_buffer, 1, &copy);
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
        barrier.offset = offset;
        barrier.size = bytes;
        vkCmdPipelineBarrier2(frame->command, &dep);
    }
    if (!start_rendering(target))
    {
        text->pending.clear();
        return;
    }
    const VkPipeline pipe = pipeline(target->format, true, true);
    if (!pipe)
    {
        text->pending.clear();
        return;
    }
    VkRect2D scissor{{static_cast<i32>(left), static_cast<i32>(top)},
                     {static_cast<u32>(right - left), static_cast<u32>(bottom - top)}};
    vkCmdBindVertexBuffers(frame->command, 0, 1, &buffer, &offset);
    const auto descriptor = text->chunks[text->pending_chunk].descriptor;
    bind_graphics(pipe, text->pipeline_layout,
                  {static_cast<u32>(target->width), static_cast<u32>(target->height)}, scissor,
                  descriptor, 1);
    vkCmdDraw(frame->command, 6, static_cast<u32>(text->pending.size()), 0, 0);
    ++stats.draws;
    ++stats.glyph_draws;
    ++target->revision;
    target->last_use = next_serial;
    text->pending.clear();
#endif
}
} // namespace vk_detail
#endif
