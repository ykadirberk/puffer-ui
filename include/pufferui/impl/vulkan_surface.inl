// Vulkan per-window slots, WSI and offscreen surfaces.
// Acquire semaphores belong to slots; finished semaphores belong to images.
// Resize/shutdown may wait for WSI; ordinary frames never use device idle.
// Included inside namespace pui by the single implementation TU.
#if defined(PUFFERUI_ENABLE_VULKAN)
namespace vk_detail
{
void surface::output_size(i32 &w, i32 &h)
{
    w = static_cast<i32>(extent.width);
    h = static_cast<i32>(extent.height);
    if (window && dev->options.host) dev->options.host->output_size(window, w, h);
}
void surface::release_swapchain()
{
    if (swapchain)
        dev->check(vkQueueWaitIdle(dev->native.queue),
                   "vkQueueWaitIdle(resize/present retirement)");
    for (auto semaphore : finished)
        if (semaphore) vkDestroySemaphore(dev->native.device, semaphore, nullptr);
    finished.clear();
    images.clear();
    initialized.clear();
    if (swapchain) vkDestroySwapchainKHR(dev->native.device, swapchain, nullptr);
    swapchain = VK_NULL_HANDLE;
}
bool surface::rebuild()
{
    if (!native)
    {
        if (!extent.width || !extent.height) extent = {640, 480};
        if (!scene)
            scene = dev->make_texture(static_cast<i32>(extent.width),
                                      static_cast<i32>(extent.height), true, nullptr);
        dirty = false;
        return scene != nullptr;
    }
    i32 width = 0, height = 0;
    output_size(width, height);
    if (width <= 0 || height <= 0)
    {
        extent = {};
        return true;
    }
    if (dev->frame && dev->frame->recording) dev->submit();
    if (!dev->wait(dev->next_serial - 1)) return false;
    release_swapchain();
    VkSurfaceCapabilitiesKHR cap{};
    if (!dev->check(
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR(dev->native.physical_device, native, &cap),
            "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"))
        return false;
    extent = cap.currentExtent;
    if (extent.width == UINT32_MAX)
    {
        extent.width = static_cast<u32>(width);
        extent.height = static_cast<u32>(height);
        if (extent.width < cap.minImageExtent.width) extent.width = cap.minImageExtent.width;
        if (extent.width > cap.maxImageExtent.width) extent.width = cap.maxImageExtent.width;
        if (extent.height < cap.minImageExtent.height) extent.height = cap.minImageExtent.height;
        if (extent.height > cap.maxImageExtent.height) extent.height = cap.maxImageExtent.height;
    }
    u32 count = 0;
    if (!dev->check(vkGetPhysicalDeviceSurfaceFormatsKHR(dev->native.physical_device, native,
                                                         &count, nullptr),
                    "vkGetPhysicalDeviceSurfaceFormatsKHR"))
        return false;
    std::vector<VkSurfaceFormatKHR> formats(count);
    if (!dev->check(vkGetPhysicalDeviceSurfaceFormatsKHR(dev->native.physical_device, native,
                                                         &count, formats.data()),
                    "vkGetPhysicalDeviceSurfaceFormatsKHR"))
        return false;
    VkSurfaceFormatKHR selected{};
    if (formats.size() == 1 && formats[0].format == VK_FORMAT_UNDEFINED &&
        formats[0].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        selected = {VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    for (auto f : formats)
        if ((f.format == VK_FORMAT_R8G8B8A8_UNORM || f.format == VK_FORMAT_B8G8R8A8_UNORM) &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            selected = f;
            break;
        }
    if (selected.format == VK_FORMAT_UNDEFINED ||
        !(cap.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        return dev->fail("Vulkan surface requires SDR UNORM and transfer destination support");
    format = selected.format;
    VkFormatProperties output_format{};
    vkGetPhysicalDeviceFormatProperties(dev->native.physical_device, format, &output_format);
    if (!(output_format.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT))
        return dev->fail("Vulkan swapchain format cannot receive the scene copy");
    if (!dev->check(vkGetPhysicalDeviceSurfacePresentModesKHR(dev->native.physical_device, native,
                                                              &count, nullptr),
                    "vkGetPhysicalDeviceSurfacePresentModesKHR"))
        return false;
    std::vector<VkPresentModeKHR> modes(count);
    if (!dev->check(vkGetPhysicalDeviceSurfacePresentModesKHR(dev->native.physical_device, native,
                                                              &count, modes.data()),
                    "vkGetPhysicalDeviceSurfacePresentModesKHR"))
        return false;
    mode = VK_PRESENT_MODE_FIFO_KHR;
    bool pacer = true;
    for (const surface *candidate : dev->surfaces)
        if (candidate->native)
        {
            pacer = candidate == this;
            break;
        }
    if (!dev->vsync || !pacer)
        for (auto wanted : {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR})
            for (auto available : modes)
                if (available == wanted) mode = wanted;
    u32 image_count = cap.minImageCount + 1;
    if (cap.maxImageCount && image_count > cap.maxImageCount) image_count = cap.maxImageCount;
    VkSwapchainCreateInfoKHR sc{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    sc.surface = native;
    sc.minImageCount = image_count;
    sc.imageFormat = format;
    sc.imageColorSpace = selected.colorSpace;
    sc.imageExtent = extent;
    sc.imageArrayLayers = 1;
    sc.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    sc.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sc.preTransform = cap.currentTransform;
    sc.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(cap.supportedCompositeAlpha & sc.compositeAlpha))
        for (auto alpha :
             {VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
              VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR})
            if (cap.supportedCompositeAlpha & alpha)
            {
                sc.compositeAlpha = alpha;
                break;
            }
    sc.presentMode = mode;
    sc.clipped = VK_TRUE;
    if (!dev->check(vkCreateSwapchainKHR(dev->native.device, &sc, nullptr, &swapchain),
                    "vkCreateSwapchainKHR"))
        return false;
    if (!dev->check(vkGetSwapchainImagesKHR(dev->native.device, swapchain, &count, nullptr),
                    "vkGetSwapchainImagesKHR"))
        return false;
    images.resize(count);
    finished.resize(count);
    initialized.assign(count, false);
    if (!dev->check(vkGetSwapchainImagesKHR(dev->native.device, swapchain, &count, images.data()),
                    "vkGetSwapchainImagesKHR"))
        return false;
    VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (auto &semaphore : finished)
        if (!dev->check(vkCreateSemaphore(dev->native.device, &si, nullptr, &semaphore),
                        "vkCreateSemaphore(present image)"))
            return false;
    if (scene) dev->destroy_texture(scene);
    scene = dev->make_texture(static_cast<i32>(extent.width), static_cast<i32>(extent.height), true,
                              nullptr);
    dirty = false;
    return scene != nullptr;
}
void surface::make_current(render_device &)
{
    suspended = true;
    if (dev->frame && dev->frame->recording) dev->submit();
    dev->end_rendering();
    dev->current_surface = this;
    dev->target = nullptr;
    i32 w = 0, h = 0;
    output_size(w, h);
    if (window && (w != static_cast<i32>(extent.width) || h != static_cast<i32>(extent.height)))
        dirty = true;
    if (dirty)
    {
        if (!rebuild() || !dev->prepare_sdf_surface(*this)) return;
    }
    if (!extent.width || !extent.height || !scene) return;
    if (sdf_preparation_pending)
    {
        sdf_preparation_pending = false;
        if (!dev->wait(dev->next_serial - 1) || !dev->prepare_sdf_surface(*this)) return;
    }
    if (dev->frame && dev->frame->recording) dev->submit();
    slot = (slot + 1) % static_cast<u32>(slots.size());
    if (!dev->begin(slots[slot])) return;
    acquired = false;
    if (native)
    {
        const f64 acquire_start = wall_ms();
        VkResult r = vkAcquireNextImageKHR(dev->native.device, swapchain, UINT64_MAX,
                                           slots[slot].acquired, VK_NULL_HANDLE, &image_index);
        dev->stats.acquire_ms += wall_ms() - acquire_start;
        if (r == VK_ERROR_OUT_OF_DATE_KHR)
        {
            dirty = true;
            return;
        }
        if (r == VK_SUBOPTIMAL_KHR)
            dirty = true;
        else if (!dev->check(r, "vkAcquireNextImageKHR"))
            return;
        acquired = true;
    }
    suspended = false;
    dev->target = scene;
    dev->has_clip = scene->clipped;
    dev->clip = scene->clip;
    presented = false;
    dev->cache_begin(this);
    if (dev->options.painter != vulkan_painter_mode::REFERENCE)
    {
        dev->primitive_commands.owner = dev;
        dev->primitive_commands.reset();
    }
}
void surface::present()
{
    if (presented || !scene || dev->result != VK_SUCCESS) return;
    if (dev->cache_active) dev->cache_flush(true);
    presented = true;
    if (!native)
    {
        dev->cache_commit(dev->submit() != 0);
        return;
    }
    if (!acquired || !dev->commands()) return;
    dev->end_rendering();
    dev->transition(scene, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.oldLayout =
        initialized[image_index] ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = images[image_index];
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(dev->frame->command, &dep);
    VkImageBlit blit{};
    blit.srcSubresource = blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[1] =
        blit.dstOffsets[1] = {static_cast<i32>(extent.width), static_cast<i32>(extent.height), 1};
    vkCmdBlitImage(dev->frame->command, scene->image, scene->layout, images[image_index],
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
    barrier.dstAccessMask = 0;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    vkCmdPipelineBarrier2(dev->frame->command, &dep);
    if (!dev->submit(slots[slot].acquired, finished[image_index]))
    {
        dev->cache_commit(false);
        return;
    }
    dev->cache_commit(true);
    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &finished[image_index];
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain;
    pi.pImageIndices = &image_index;
    const f64 present_start = wall_ms();
    const VkResult r = vkQueuePresentKHR(dev->native.queue, &pi);
    dev->stats.present_ms += wall_ms() - present_start;
    initialized[image_index] = true;
    acquired = false;
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR)
        dirty = true;
    else
        dev->check(r, "vkQueuePresentKHR");
}
render_surface *device::create_surface(void *window)
{
    return make_surface(window, 640, 480);
}
render_surface *device::make_surface(void *window, i32 width, i32 height)
{
    if (width <= 0 || height <= 0) return nullptr;
    auto *s = new surface();
    static_cast<dispatch &>(*s) = static_cast<const dispatch &>(*this);
    s->dev = this;
    s->extent = {static_cast<u32>(width), static_cast<u32>(height)};
    s->window = window;
    s->slots.resize(options.frames_in_flight);
    if (window)
    {
        if (!options.host || !vkGetDeviceProcAddr(native.device, "vkCreateSwapchainKHR"))
        {
            delete s;
            fail("Vulkan WSI requires a host and enabled VK_KHR_swapchain");
            return nullptr;
        }
        s->native = options.host->create_surface(native.instance, window);
        VkBool32 supported = VK_FALSE;
        if (!s->native ||
            !check(vkGetPhysicalDeviceSurfaceSupportKHR(native.physical_device, native.queue_family,
                                                        s->native, &supported),
                   "vkGetPhysicalDeviceSurfaceSupportKHR") ||
            !supported)
        {
            if (s->native) vkDestroySurfaceKHR(native.instance, s->native, nullptr);
            delete s;
            fail("graphics queue cannot present this surface");
            return nullptr;
        }
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        for (auto &slot : s->slots)
            if (!check(vkCreateSemaphore(native.device, &si, nullptr, &slot.acquired),
                       "vkCreateSemaphore(acquire)"))
            {
                for (auto &f : s->slots) destroy_slot(f);
                vkDestroySurfaceKHR(native.instance, s->native, nullptr);
                delete s;
                return nullptr;
            }
    }
    if (!s->rebuild() || !prepare_sdf_surface(*s))
    {
        s->release_swapchain();
        for (auto &f : s->slots) destroy_slot(f);
        if (s->native) vkDestroySurfaceKHR(native.instance, s->native, nullptr);
        delete s;
        return nullptr;
    }
    surfaces.push_back(s);
    return s;
}
void device::destroy_surface(render_surface *base)
{
    auto *s = dynamic_cast<surface *>(base);
    if (!s || s->dev != this) return;
    bool was_pacer = false;
    for (const surface *candidate : surfaces)
        if (candidate->native)
        {
            was_pacer = candidate == s;
            break;
        }
    if (cache_active) cache_flush(false);
    if (frame && frame->recording) submit();
    wait(next_serial - 1);
    end_rendering();
    s->release_swapchain();
    if (target == s->scene) target = nullptr;
    if (s->scene) destroy_texture(s->scene);
    for (auto *image : s->kawase)
        if (image) destroy_texture(image);
    for (auto &slot : s->slots)
    {
        if (frame == &slot) frame = nullptr;
        destroy_slot(slot);
    }
    if (current_surface == s) current_surface = nullptr;
    if (s->native) vkDestroySurfaceKHR(native.instance, s->native, nullptr);
    for (usize i = 0; i < surfaces.size(); ++i)
        if (surfaces[i] == s)
        {
            surfaces.erase(surfaces.begin() + static_cast<std::ptrdiff_t>(i));
            break;
        }
    if (was_pacer && vsync)
        for (surface *candidate : surfaces)
            if (candidate->native)
            {
                candidate->dirty = true;
                break;
            }
    delete s;
    collect();
}
void device::set_vsync(bool enabled)
{
    if (vsync == enabled) return;
    vsync = enabled;
    for (auto *s : surfaces) s->dirty = true;
}
} // namespace vk_detail
render_surface *create_vulkan_surface(render_device *base, const vulkan_surface_opts &opts)
{
    auto *d = dynamic_cast<vk_detail::device *>(base);
    return d ? d->make_surface(opts.window, opts.width, opts.height) : nullptr;
}
VkPresentModeKHR vulkan_present_mode(render_surface *base)
{
    auto *s = dynamic_cast<vk_detail::surface *>(base);
    return s && s->native ? s->mode : VK_PRESENT_MODE_MAX_ENUM_KHR;
}
#endif
