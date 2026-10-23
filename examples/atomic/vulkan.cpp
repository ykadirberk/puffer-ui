// Native Vulkan renderer + SDL windows + an application-owned VkImage.
// The producer below deliberately uses only public Vulkan/PufferUI APIs.
#include "../example_common.h"
#include <pufferui/vulkan.h>

struct native_picture
{
    vulkan_native_device native{};
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDeviceMemory image_memory = VK_NULL_HANDLE, buffer_memory = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkSemaphore ready = VK_NULL_HANDLE;
    VkFence producer_done = VK_NULL_HANDLE;
    image_handle picture{};
    bool submitted = false, attempted = false, okay = false;
    u32 memory_type(u32 bits, VkMemoryPropertyFlags required)
    {
        VkPhysicalDeviceMemoryProperties memory{};
        vkGetPhysicalDeviceMemoryProperties(native.physical_device, &memory);
        for (u32 i = 0; i < memory.memoryTypeCount; ++i)
            if ((bits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & required) == required)
                return i;
        return UINT32_MAX;
    }
    bool create(render_device *device)
    {
        native = vulkan_handles(device);
#define VK_TRY(call)                                                                               \
    do                                                                                             \
    {                                                                                              \
        if ((call) != VK_SUCCESS) return false;                                                    \
    } while (0)
        VkImageCreateInfo ic{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ic.imageType = VK_IMAGE_TYPE_2D;
        ic.format = VK_FORMAT_R8G8B8A8_UNORM;
        ic.extent = {64, 64, 1};
        ic.mipLevels = ic.arrayLayers = 1;
        ic.samples = VK_SAMPLE_COUNT_1_BIT;
        ic.tiling = VK_IMAGE_TILING_OPTIMAL;
        ic.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        VK_TRY(vkCreateImage(native.device, &ic, nullptr, &image));
        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(native.device, image, &req);
        VkMemoryAllocateInfo memory{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        memory.allocationSize = req.size;
        memory.memoryTypeIndex =
            memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (memory.memoryTypeIndex == UINT32_MAX)
            memory.memoryTypeIndex = memory_type(req.memoryTypeBits, 0);
        if (memory.memoryTypeIndex == UINT32_MAX) return false;
        VK_TRY(vkAllocateMemory(native.device, &memory, nullptr, &image_memory));
        VK_TRY(vkBindImageMemory(native.device, image, image_memory, 0));
        VkImageViewCreateInfo vc{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vc.image = image;
        vc.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vc.format = ic.format;
        vc.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VK_TRY(vkCreateImageView(native.device, &vc, nullptr, &view));
        VkBufferCreateInfo bc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bc.size = 64 * 64 * 4;
        bc.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        VK_TRY(vkCreateBuffer(native.device, &bc, nullptr, &buffer));
        vkGetBufferMemoryRequirements(native.device, buffer, &req);
        memory.allocationSize = req.size;
        memory.memoryTypeIndex =
            memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        if (memory.memoryTypeIndex == UINT32_MAX) return false;
        VK_TRY(vkAllocateMemory(native.device, &memory, nullptr, &buffer_memory));
        VK_TRY(vkBindBufferMemory(native.device, buffer, buffer_memory, 0));
        void *mapped = nullptr;
        VK_TRY(vkMapMemory(native.device, buffer_memory, 0, VK_WHOLE_SIZE, 0, &mapped));
        auto *pixels = static_cast<u8 *>(mapped);
        for (u32 y = 0; y < 64; ++y)
            for (u32 x = 0; x < 64; ++x)
            {
                const usize at = (y * 64 + x) * 4;
                const bool light = ((x / 8 + y / 8) & 1) == 0;
                pixels[at] = light ? 40 : 16;
                pixels[at + 1] = static_cast<u8>(80 + x * 2);
                pixels[at + 2] = light ? 230 : 140;
                pixels[at + 3] = 255;
            }
        VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        range.memory = buffer_memory;
        range.size = VK_WHOLE_SIZE;
        const VkResult flushed = vkFlushMappedMemoryRanges(native.device, 1, &range);
        vkUnmapMemory(native.device, buffer_memory);
        if (flushed != VK_SUCCESS) return false;
        VkCommandPoolCreateInfo pc{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pc.queueFamilyIndex = native.queue_family;
        VK_TRY(vkCreateCommandPool(native.device, &pc, nullptr, &pool));
        VkCommandBufferAllocateInfo commands{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        commands.commandPool = pool;
        commands.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commands.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VK_TRY(vkAllocateCommandBuffers(native.device, &commands, &cmd));
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        VK_TRY(vkBeginCommandBuffer(cmd, &begin));
        VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange = vc.subresourceRange;
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cmd, &dependency);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = ic.extent;
        vkCmdCopyBufferToImage(cmd, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier2(cmd, &dependency);
        VK_TRY(vkEndCommandBuffer(cmd));
        VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VK_TRY(vkCreateSemaphore(native.device, &semaphore, nullptr, &ready));
        VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VK_TRY(vkCreateFence(native.device, &fence, nullptr, &producer_done));
        VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        signal.semaphore = ready;
        signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        VkCommandBufferSubmitInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        command.commandBuffer = cmd;
        VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos = &command;
        submit.signalSemaphoreInfoCount = 1;
        submit.pSignalSemaphoreInfos = &signal;
        VK_TRY(vkQueueSubmit2(native.queue, 1, &submit, producer_done));
        submitted = true;
        vulkan_image_opts imported;
        imported.image = image;
        imported.view = view;
        imported.width = imported.height = 64;
        imported.ready = ready;
        picture = {import_vulkan_image(device, imported), 64, 64};
        return picture.valid();
#undef VK_TRY
    }
    void destroy(render_device *device)
    {
        if (!native.device) return;
        if (picture.valid())
        {
            const u64 released = release_vulkan_image(device, picture.tex);
            okay = okay && released && vulkan_wait(device, released);
            picture = {};
        }
        if (submitted &&
            vkWaitForFences(native.device, 1, &producer_done, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
            okay = false;
        if (producer_done) vkDestroyFence(native.device, producer_done, nullptr);
        if (ready) vkDestroySemaphore(native.device, ready, nullptr);
        if (pool) vkDestroyCommandPool(native.device, pool, nullptr);
        if (buffer) vkDestroyBuffer(native.device, buffer, nullptr);
        if (buffer_memory) vkFreeMemory(native.device, buffer_memory, nullptr);
        if (view) vkDestroyImageView(native.device, view, nullptr);
        if (image) vkDestroyImage(native.device, image, nullptr);
        if (image_memory) vkFreeMemory(native.device, image_memory, nullptr);
        native.device = VK_NULL_HANDLE;
    }
};
int main(int argc, char **argv)
{
    native_picture state;
    bool help = false;
    for (i32 i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--help") == 0) help = true;
    const i32 code = example_run(
        "Vulkan", 680, 480, argc, argv,
        [&](ui &u, example_app &app)
        {
            if (!state.attempted)
            {
                state.attempted = true;
                app.shutdown_user = &state;
                app.before_shutdown = [](example_app &a, void *user)
                { static_cast<native_picture *>(user)->destroy(a.device); };
                state.okay = state.create(app.device);
                if (!state.okay) app.running = false;
            }
            auto page = example_begin_page(u, app, "Vulkan + SDL",
                                           "a native image supplied by the application");
            column areas(page.content, u.spacing());
            rect picture =
                example_section(u, areas.next(page.content.h * 0.68f), "External VkImage",
                                "uploaded by the producer, sampled by PufferUI", app.font_bold);
            u.image(picture, state.picture);
            rect info = example_section(u, areas.remaining(), "Independent ownership",
                                        "SDL manages windows and input; Vulkan paints the UI",
                                        app.font_bold);
            u.text_wrapped(info,
                           "The application owns this image and its Vulkan allocation. PufferUI "
                           "borrows the image until its release timeline completes.",
                           u.th().text_dim);
        });
    return help ? code : (code ? code : (state.okay ? 0 : 1));
}
