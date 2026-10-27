#include <pufferui/vulkan.h>
#include "vulkan_test_options.h"
#include <cstdio>
using namespace pui;
#define REQUIRE(x)                                                                                 \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x);                                   \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
static i32 errors = 0;
static void diagnostic(i32 code, const char *message, void *)
{
    std::fprintf(stderr, "Vulkan %d: %s\n", code, message);
    ++errors;
}
int run(bool cache, bool timeline)
{
    vulkan_device_opts options = test_detail::reference_options();
    options.validation = true;
    options.tile_cache = cache;
    options.error_callback = diagnostic;
    render_device *d = create_vulkan_device(options);
    REQUIRE(d);
    const auto native = vulkan_handles(d);
    // These objects belong to an independent producer, not to PufferUI.
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkSemaphore ready = VK_NULL_HANDLE;
    VkImageCreateInfo ic{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ic.imageType = VK_IMAGE_TYPE_2D;
    ic.format = VK_FORMAT_R8G8B8A8_UNORM;
    ic.extent = {16, 16, 1};
    ic.mipLevels = ic.arrayLayers = 1;
    ic.samples = VK_SAMPLE_COUNT_1_BIT;
    ic.tiling = VK_IMAGE_TILING_OPTIMAL;
    ic.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    REQUIRE(vkCreateImage(native.device, &ic, nullptr, &image) == VK_SUCCESS);
    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(native.device, image, &req);
    VkPhysicalDeviceMemoryProperties props{};
    vkGetPhysicalDeviceMemoryProperties(native.physical_device, &props);
    u32 type = 0;
    while (type < props.memoryTypeCount && !(req.memoryTypeBits & (1u << type))) ++type;
    REQUIRE(type < props.memoryTypeCount);
    VkMemoryAllocateInfo ac{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ac.allocationSize = req.size;
    ac.memoryTypeIndex = type;
    REQUIRE(vkAllocateMemory(native.device, &ac, nullptr, &memory) == VK_SUCCESS);
    REQUIRE(vkBindImageMemory(native.device, image, memory, 0) == VK_SUCCESS);
    VkImageViewCreateInfo vc{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vc.image = image;
    vc.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vc.format = ic.format;
    vc.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    REQUIRE(vkCreateImageView(native.device, &vc, nullptr, &view) == VK_SUCCESS);
    VkCommandPoolCreateInfo pc{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pc.queueFamilyIndex = native.queue_family;
    REQUIRE(vkCreateCommandPool(native.device, &pc, nullptr, &pool) == VK_SUCCESS);
    VkCommandBufferAllocateInfo bc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    bc.commandPool = pool;
    bc.commandBufferCount = 1;
    bc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    REQUIRE(vkAllocateCommandBuffers(native.device, &bc, &cmd) == VK_SUCCESS);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    REQUIRE(vkBeginCommandBuffer(cmd, &begin) == VK_SUCCESS);
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = vc.subresourceRange;
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dep);
    const VkClearColorValue blue{{0, 0, 1, 1}};
    vkCmdClearColorImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &blue, 1,
                         &vc.subresourceRange);
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier2(cmd, &dep);
    REQUIRE(vkEndCommandBuffer(cmd) == VK_SUCCESS);
    VkSemaphoreCreateInfo sc{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkSemaphoreTypeCreateInfo type_info{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    type_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    if (timeline) sc.pNext = &type_info;
    REQUIRE(vkCreateSemaphore(native.device, &sc, nullptr, &ready) == VK_SUCCESS);
    VkCommandBufferSubmitInfo cb{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cb.commandBuffer = cmd;
    VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    signal.semaphore = ready;
    signal.value = timeline ? 5 : 0;
    signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cb;
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos = &signal;
    REQUIRE(vkQueueSubmit2(native.queue, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS);
    vulkan_image_opts imported;
    imported.image = image;
    imported.view = view;
    imported.width = imported.height = 16;
    imported.ready = ready;
    imported.ready_value = timeline ? 5 : 0;
    texture_handle external = import_vulkan_image(d, imported);
    REQUIRE(external);
    render_surface *surface =
        cache ? create_vulkan_surface(d, {.width = 16, .height = 16}) : nullptr;
    texture_handle target = surface ? surface->scene_target() : d->create_target(16, 16);
    REQUIRE(target);
    if (surface) surface->make_current(*d);
    d->set_target(target);
    d->clear({0, 0, 0, 255});
    const vertex vertices[] = {{0, 0, 0, 0, {255, 255, 255, 255}},
                               {16, 0, 1, 0, {255, 255, 255, 255}},
                               {16, 16, 1, 1, {255, 255, 255, 255}},
                               {0, 16, 0, 1, {255, 255, 255, 255}}};
    const i32 indices[] = {0, 1, 2, 0, 2, 3};
    d->draw(external, vertices, 4, indices, 6);
    const u64 done = release_vulkan_image(d, external);
    REQUIRE(done);
    REQUIRE(vulkan_wait(d, done));
    VkSemaphore completion = vulkan_completion_semaphore(d);
    REQUIRE(completion);
    VkSemaphoreWaitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
    wait.semaphoreCount = 1;
    wait.pSemaphores = &completion;
    wait.pValues = &done;
    REQUIRE(vkWaitSemaphores(native.device, &wait, UINT64_MAX) == VK_SUCCESS);
    // Backend released only its wrapper/descriptors. Producer destroys its own objects.
    vkDestroySemaphore(native.device, ready, nullptr);
    vkDestroyCommandPool(native.device, pool, nullptr);
    vkDestroyImageView(native.device, view, nullptr);
    vkDestroyImage(native.device, image, nullptr);
    vkFreeMemory(native.device, memory, nullptr);
    // The release value alone must cover cached as well as immediate draws.
    // Reading the result after producer destruction catches a too-early token.
    u8 pixel[4]{};
    REQUIRE(d->read_pixels(target, 8, 8, 1, 1, pixel));
    REQUIRE(pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 255 && pixel[3] == 255);
    if (surface)
    {
        surface->present();
        d->destroy_surface(surface);
    }
    else
        d->destroy_target(target);
    REQUIRE(vulkan_wait_idle(d));
    REQUIRE(vulkan_statistics(d).validation_errors == 0);
    destroy_vulkan_device(d);
    REQUIRE(errors == 0);
    std::puts("all Vulkan external image tests passed");
    return 0;
}
int main()
{
    for (bool timeline : {false, true})
        for (bool cache : {false, true})
            if (run(cache, timeline)) return 1;
    return 0;
}
