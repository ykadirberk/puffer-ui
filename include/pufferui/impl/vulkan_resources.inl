// Vulkan images, descriptors, revision and submission retirement.
// Each handle belongs to one device. Imported images remain caller-owned.
// Updates execute in call order, including between two draws in a frame.
// Included inside namespace pui by the single implementation TU.
#if defined(PUFFERUI_ENABLE_VULKAN)
namespace vk_detail
{
bool device::valid(texture *t) const
{
#if defined(PUFFERUI_ENABLE_HARFBUZZ)
    if (t && text && text->coverage && t == text->coverage->atlas) return !t->dead;
#endif
    for (auto *live : textures)
        if (live == t) return !live->dead;
    return false;
}
bool device::descriptors(texture *t)
{
    VkDescriptorSetLayout layouts[2] = {descriptor_layout, descriptor_layout};
    for (u32 attempt = 0; attempt < 2; ++attempt)
    {
        if (descriptor_pools.empty() || attempt)
        {
            VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 512};
            VkDescriptorPoolCreateInfo pc{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            pc.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
            pc.maxSets = 512;
            pc.poolSizeCount = 1;
            pc.pPoolSizes = &size;
            VkDescriptorPool pool = VK_NULL_HANDLE;
            if (!check(vkCreateDescriptorPool(native.device, &pc, nullptr, &pool),
                       "vkCreateDescriptorPool"))
                return false;
            descriptor_pools.push_back(pool);
        }
        VkDescriptorSetAllocateInfo ac{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ac.descriptorPool = descriptor_pools.back();
        ac.descriptorSetCount = 2;
        ac.pSetLayouts = layouts;
        const VkResult r = vkAllocateDescriptorSets(native.device, &ac, t->descriptors);
        if (r != VK_SUCCESS) t->descriptors[0] = t->descriptors[1] = VK_NULL_HANDLE;
        if (r == VK_ERROR_OUT_OF_POOL_MEMORY || r == VK_ERROR_FRAGMENTED_POOL) continue;
        if (!check(r, "vkAllocateDescriptorSets")) return false;
        t->descriptor_pool = ac.descriptorPool;
        stats.descriptor_sets += 2;
        for (u32 i = 0; i < 2; ++i)
        {
            VkDescriptorImageInfo info{samplers[i], t->view,
                                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = t->descriptors[i];
            write.dstBinding = 0;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = &info;
            vkUpdateDescriptorSets(native.device, 1, &write, 0, nullptr);
        }
        return true;
    }
    return check(VK_ERROR_OUT_OF_POOL_MEMORY, "descriptor pool exhausted");
}
texture *device::make_texture(i32 w, i32 h, bool render_target, const u8 *rgba)
{
    return allocate_texture(w, h, {.render_target = render_target, .rgba = rgba});
}
texture *device::allocate_texture(i32 w, i32 h, const texture_creation &creation)
{
    if (!creation.internal && cache_active) cache_flush(false);
    if (w <= 0 || h <= 0 || static_cast<u32>(w) > properties.limits.maxImageDimension2D ||
        static_cast<u32>(h) > properties.limits.maxImageDimension2D || result != VK_SUCCESS)
        return nullptr;
    auto *t = new texture();
    t->owner = this;
    t->width = w;
    t->height = h;
    t->generation = new_generation();
    t->target = creation.render_target;
    t->linear = creation.render_target;
    t->format = creation.format;
    VkImageCreateInfo ic{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ic.imageType = VK_IMAGE_TYPE_2D;
    ic.format = t->format;
    ic.extent = {static_cast<u32>(w), static_cast<u32>(h), 1};
    ic.mipLevels = ic.arrayLayers = 1;
    ic.samples = VK_SAMPLE_COUNT_1_BIT;
    ic.tiling = VK_IMAGE_TILING_OPTIMAL;
    ic.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
               VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (creation.render_target) ic.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (!check(vkCreateImage(native.device, &ic, nullptr, &t->image), "vkCreateImage"))
    {
        free_texture(t);
        return nullptr;
    }
    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(native.device, t->image, &req);
    const u32 type = memory_type(req.memoryTypeBits, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type == UINT32_MAX)
    {
        free_texture(t);
        fail("image memory type unavailable");
        return nullptr;
    }
    VkMemoryAllocateInfo ac{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ac.allocationSize = req.size;
    ac.memoryTypeIndex = type;
    if (!check(vkAllocateMemory(native.device, &ac, nullptr, &t->memory),
               "vkAllocateMemory(image)"))
    {
        free_texture(t);
        return nullptr;
    }
    t->allocation = req.size;
    stats.image_bytes += req.size;
    if (!check(vkBindImageMemory(native.device, t->image, t->memory, 0), "vkBindImageMemory"))
    {
        free_texture(t);
        return nullptr;
    }
    VkImageViewCreateInfo vc{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vc.image = t->image;
    vc.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vc.format = t->format;
    vc.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (!check(vkCreateImageView(native.device, &vc, nullptr, &t->view), "vkCreateImageView") ||
        !descriptors(t))
    {
        free_texture(t);
        return nullptr;
    }
    // Internal glyph coverage is renderer scratch, not a caller-visible texture
    // handle. Its allocation is still fully included in image_bytes.
    if (creation.internal) return t;
    textures.push_back(t);
    ++stats.live_textures;
    if (!commands())
    {
        destroy_texture(t);
        return nullptr;
    }
    end_rendering();
    transition(t, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    const VkClearColorValue zero{};
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(frame->command, t->image, t->layout, &zero, 1, &range);
    t->last_use = next_serial;
    ++t->revision;
    if (creation.rgba) update_texture(t, 0, 0, w, h, creation.rgba);
    if (result != VK_SUCCESS)
    {
        destroy_texture(t);
        return nullptr;
    }
    return t;
}
void device::free_texture(texture *t)
{
    if (!t) return;
    if (t->descriptor_pool && t->descriptors[0])
    {
        vkFreeDescriptorSets(native.device, t->descriptor_pool, 2, t->descriptors);
        stats.descriptor_sets -= 2;
    }
    if (t->owns_view && t->view) vkDestroyImageView(native.device, t->view, nullptr);
    if (!t->external && t->image) vkDestroyImage(native.device, t->image, nullptr);
    if (t->memory) vkFreeMemory(native.device, t->memory, nullptr);
    stats.image_bytes -= t->allocation;
    if (t->dead) stats.retired_bytes -= t->allocation;
    delete t;
}
void device::destroy_texture(texture_handle handle)
{
    flush_text();
    if (cache_active) cache_flush(false);
    auto *t = static_cast<texture *>(handle);
    if (!valid(t)) return;
    if (rendering_target == t) end_rendering();
    if (target == t) target = nullptr;
    for (usize i = 0; i < textures.size(); ++i)
        if (textures[i] == t)
        {
            textures.erase(textures.begin() + static_cast<std::ptrdiff_t>(i));
            break;
        }
    t->dead = true;
    stats.retired_bytes += t->allocation;
    retired.push_back(t);
    --stats.live_textures;
}
void device::set_texture_filter(texture_handle handle, bool linear)
{
    if (cache_active) cache_flush(false);
    auto *t = static_cast<texture *>(handle);
    if (valid(t) && t->linear != linear)
    {
        t->linear = linear;
        ++t->revision;
    }
}
static void layout_access(VkImageLayout layout, VkPipelineStageFlags2 &stage,
                          VkAccessFlags2 &access)
{
    switch (layout)
    {
    case VK_IMAGE_LAYOUT_UNDEFINED:
        stage = VK_PIPELINE_STAGE_2_NONE;
        access = 0;
        break;
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        stage = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        access = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        break;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        stage = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        access = VK_ACCESS_2_TRANSFER_READ_BIT;
        break;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        break;
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        access = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        break;
    default:
        stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        access = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
        break;
    }
}
void device::transition(texture *t, VkImageLayout layout)
{
    if (!commands()) return;
    if (t->ready)
    {
        VkSemaphoreSubmitInfo wi{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        wi.semaphore = t->ready;
        wi.value = t->ready_value;
        wi.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        frame->waits.push_back(wi);
        t->ready = VK_NULL_HANDLE;
    }
    t->last_use = next_serial;
    if (t->layout == layout && layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) return;
    end_rendering();
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    layout_access(t->layout, barrier.srcStageMask, barrier.srcAccessMask);
    layout_access(layout, barrier.dstStageMask, barrier.dstAccessMask);
    barrier.oldLayout = t->layout;
    barrier.newLayout = layout;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = t->image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(frame->command, &dependency);
    t->layout = layout;
}
void device::update_texture(texture_handle handle, i32 x, i32 y, i32 w, i32 h, const u8 *rgba)
{
    if (cache_active) cache_flush(false);
    auto *t = static_cast<texture *>(handle);
    if (!valid(t) || !rgba || t->external || w <= 0 || h <= 0 || x < 0 || y < 0 || w > t->width ||
        h > t->height || x > t->width - w || y > t->height - h)
        return;
    VkDeviceSize offset = 0;
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(w) * h * 4;
    page *p = upload(bytes, offset);
    if (!p) return;
    std::memcpy(p->mapped + offset, rgba, static_cast<usize>(bytes));
    end_rendering();
    transition(t, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy copy{};
    copy.bufferOffset = offset;
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageOffset = {x, y, 0};
    copy.imageExtent = {static_cast<u32>(w), static_cast<u32>(h), 1};
    vkCmdCopyBufferToImage(frame->command, p->buffer, t->image, t->layout, 1, &copy);
    ++t->revision;
}
bool device::read_pixels(texture_handle handle, i32 x, i32 y, i32 w, i32 h, u8 *rgba)
{
    flush_text();
    const f64 start = wall_ms();
    if (cache_active) cache_flush(false);
    auto *t = static_cast<texture *>(handle);
    if (!valid(t) || !rgba || w <= 0 || h <= 0 || x < 0 || y < 0 || w > t->width || h > t->height ||
        x > t->width - w || y > t->height - h)
        return false;
    if (t->format != VK_FORMAT_R8G8B8A8_UNORM && t->format != VK_FORMAT_B8G8R8A8_UNORM)
        return false;
    page p;
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(w) * h * 4;
    if (!allocate_buffer(p, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT)) return false;
    if (!commands())
    {
        free_page(p);
        return false;
    }
    end_rendering();
    transition(t, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageOffset = {x, y, 0};
    copy.imageExtent = {static_cast<u32>(w), static_cast<u32>(h), 1};
    vkCmdCopyImageToBuffer(frame->command, t->image, t->layout, p.buffer, 1, &copy);
    VkBufferMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = p.buffer;
    barrier.size = VK_WHOLE_SIZE;
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.bufferMemoryBarrierCount = 1;
    dep.pBufferMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(frame->command, &dep);
    const u64 serial = submit();
    const bool success = serial && wait(serial);
    if (success)
    {
        if (!p.coherent)
        {
            VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
            range.memory = p.memory;
            range.size = VK_WHOLE_SIZE;
            check(vkInvalidateMappedMemoryRanges(native.device, 1, &range),
                  "vkInvalidateMappedMemoryRanges");
        }
        std::memcpy(rgba, p.mapped, static_cast<usize>(bytes));
        if (t->format == VK_FORMAT_B8G8R8A8_UNORM)
            for (VkDeviceSize i = 0; i < bytes; i += 4)
            {
                const u8 r = rgba[i];
                rgba[i] = rgba[i + 2];
                rgba[i + 2] = r;
            }
    }
    free_page(p);
    ++stats.readbacks;
    stats.readback_bytes += bytes;
    stats.readback_ms += wall_ms() - start;
    return success && result == VK_SUCCESS;
}
} // namespace vk_detail
texture_handle import_vulkan_image(render_device *base, const vulkan_image_opts &opts)
{
    auto *d = dynamic_cast<vk_detail::device *>(base);
    if (!d || !opts.image || !opts.view || opts.width <= 0 || opts.height <= 0 ||
        (opts.queue_family != VK_QUEUE_FAMILY_IGNORED &&
         opts.queue_family != d->native.queue_family))
        return nullptr;
    auto *t = new vk_detail::texture();
    t->owner = d;
    t->external = true;
    t->owns_view = false;
    t->linear = opts.linear;
    t->image = opts.image;
    t->view = opts.view;
    t->format = opts.format;
    t->layout = opts.layout;
    t->width = opts.width;
    t->height = opts.height;
    t->generation = d->new_generation();
    t->ready = opts.ready;
    t->ready_value = opts.ready_value;
    if (!d->descriptors(t))
    {
        delete t;
        return nullptr;
    }
    d->textures.push_back(t);
    ++d->stats.live_textures;
    return t;
}
u64 release_vulkan_image(render_device *base, texture_handle handle, VkImageLayout final_layout)
{
    auto *d = dynamic_cast<vk_detail::device *>(base);
    auto *t = static_cast<vk_detail::texture *>(handle);
    if (!d || !d->valid(t) || !t->external) return 0;
    // Cached draws still own pending uses. Replay them before the release
    // transition/submission so the returned completion covers every use.
    if (d->cache_active) d->cache_flush(false);
    if (!d->commands()) return 0;
    d->end_rendering();
    d->transition(t, final_layout);
    const u64 serial = d->submit();
    d->destroy_texture(t);
    return serial;
}
#endif
