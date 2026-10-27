// Reuse the independent SDL/native host; its existing tests remain unchanged.
#define main original_wsi_test_main
#include "test_vulkan_wsi.cpp"
#undef main
#include <cstring>
#include <vector>

namespace
{
PFN_vkGetDeviceProcAddr original_get = nullptr;
PFN_vkQueueSubmit2 original_submit = nullptr;
PFN_vkCmdPipelineBarrier2 original_barrier = nullptr;
PFN_vkGetSwapchainImagesKHR original_images = nullptr;
VkImage swapchain_images[64]{};
u32 swapchain_image_count = 0;
bool observe = false, bad_wait = false, bad_barrier = false;
u32 acquire_waits = 0, acquire_barriers = 0;
VKAPI_ATTR VkResult VKAPI_CALL images_probe(VkDevice device, VkSwapchainKHR swapchain, u32 *count,
                                            VkImage *images)
{
    const auto result = original_images(device, swapchain, count, images);
    if (result == VK_SUCCESS && images)
    {
        if (*count > 64) return VK_ERROR_INITIALIZATION_FAILED;
        swapchain_image_count = *count;
        std::memcpy(swapchain_images, images, *count * sizeof(VkImage));
    }
    return result;
}
VKAPI_ATTR VkResult VKAPI_CALL submit_probe(VkQueue queue, u32 count, const VkSubmitInfo2 *submits,
                                            VkFence fence)
{
    if (observe)
        for (u32 i = 0; i < count; ++i)
            for (u32 w = 0; w < submits[i].waitSemaphoreInfoCount; ++w)
            {
                const auto &wait = submits[i].pWaitSemaphoreInfos[w];
                if (wait.value == 0)
                {
                    ++acquire_waits;
                    bad_wait |= wait.stageMask != VK_PIPELINE_STAGE_2_BLIT_BIT;
                }
            }
    return original_submit(queue, count, submits, fence);
}
VKAPI_ATTR void VKAPI_CALL barrier_probe(VkCommandBuffer command, const VkDependencyInfo *info)
{
    if (observe)
        for (u32 i = 0; i < info->imageMemoryBarrierCount; ++i)
        {
            const auto &barrier = info->pImageMemoryBarriers[i];
            bool swapchain_image = false;
            for (u32 n = 0; n < swapchain_image_count; ++n)
                swapchain_image |= swapchain_images[n] == barrier.image;
            if (swapchain_image &&
                (barrier.oldLayout == VK_IMAGE_LAYOUT_UNDEFINED ||
                 barrier.oldLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) &&
                barrier.newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
            {
                ++acquire_barriers;
                bad_barrier |= barrier.srcStageMask != VK_PIPELINE_STAGE_2_BLIT_BIT ||
                               barrier.dstStageMask != VK_PIPELINE_STAGE_2_BLIT_BIT ||
                               barrier.srcAccessMask != 0 ||
                               barrier.dstAccessMask != VK_ACCESS_2_TRANSFER_WRITE_BIT;
            }
        }
    original_barrier(command, info);
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL device_proc(VkDevice device, const char *name)
{
    const auto function = original_get(device, name);
    if (!function) return nullptr;
    if (std::strcmp(name, "vkQueueSubmit2") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(submit_probe);
    if (std::strcmp(name, "vkCmdPipelineBarrier2") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(barrier_probe);
    if (std::strcmp(name, "vkGetSwapchainImagesKHR") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(images_probe);
    return function;
}
} // namespace

int main()
{
    REQUIRE(SDL_Init(SDL_INIT_VIDEO));
    SDL_Window *native_window =
        SDL_CreateWindow("Vulkan present stages", 96, 64, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
    REQUIRE(native_window);
    zero_size_host host;
    vulkan_device_opts options;
    options.host = &host;
    options.validation = true; // also enables synchronization validation
    options.error_callback = diagnostic;
    auto *owner = create_vulkan_device(options);
    REQUIRE(owner);
    auto borrowed = vulkan_handles(owner);
    original_get = borrowed.get_device_proc_addr;
    REQUIRE(original_get);
    original_submit =
        reinterpret_cast<PFN_vkQueueSubmit2>(original_get(borrowed.device, "vkQueueSubmit2"));
    original_barrier = reinterpret_cast<PFN_vkCmdPipelineBarrier2>(
        original_get(borrowed.device, "vkCmdPipelineBarrier2"));
    original_images = reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(
        original_get(borrowed.device, "vkGetSwapchainImagesKHR"));
    REQUIRE(original_submit && original_barrier && original_images);
    borrowed.get_device_proc_addr = device_proc;
    for (bool staged : {false, true})
    {
        swapchain_image_count = 0;
        auto child_options = options;
        child_options.native = &borrowed;
        child_options.painter = vulkan_painter_mode::SDF_RASTER;
        child_options.upload_mode =
            staged ? vulkan_upload_mode::STAGED : vulkan_upload_mode::DIRECT;
        auto *device = create_vulkan_device(child_options);
        REQUIRE(device);
        auto *native_surface = create_vulkan_surface(device, {.window = native_window});
        auto *reference_surface = create_vulkan_surface(device, {.width = 96, .height = 64});
        REQUIRE(native_surface && reference_surface);
        auto *ctx = create_context(device, native_surface);
        auto *native_win = add_window(ctx, native_window, native_surface, {0, 0, 96, 64});
        auto *reference_win = add_window(ctx, nullptr, reference_surface, {0, 0, 96, 64});
        REQUIRE(ctx && native_win && reference_win);
        std::vector<u8> native_pixels(96 * 64 * 4), reference_pixels(native_pixels.size());
        for (i32 frame = 0; frame < 8; ++frame)
        {
            device->set_vsync(frame >= 4);
            for (window *win : {native_win, reference_win})
            {
                observe = win == native_win;
                begin_frame(ctx, *win, frame / 60.0, 1.0 / 60.0);
                {
                    ui u(ctx);
                    u.draw_rect({0, 0, 96, 64}, {17, 37, 59, 255});
                    u.draw_rounded_rect({7.25f, 9.75f, 73.5f, 41.25f},
                                        {static_cast<u8>(120 + frame), 43, 211, 173}, 7);
                }
                end_frame(ctx);
                observe = false;
            }
            REQUIRE(device->read_pixels(native_surface->scene_target(), 0, 0, 96, 64,
                                        native_pixels.data()));
            REQUIRE(device->read_pixels(reference_surface->scene_target(), 0, 0, 96, 64,
                                        reference_pixels.data()));
            REQUIRE(native_pixels == reference_pixels);
            REQUIRE(!violation_count(ctx));
        }
        REQUIRE(vulkan_wait_idle(device));
        REQUIRE(!vulkan_statistics(device).validation_errors);
        destroy_context(ctx);
        device->destroy_surface(reference_surface);
        device->destroy_surface(native_surface);
        destroy_vulkan_device(device);
    }
    REQUIRE(vulkan_wait_idle(owner));
    REQUIRE(!vulkan_statistics(owner).validation_errors);
    destroy_vulkan_device(owner);
    SDL_DestroyWindow(native_window);
    SDL_Quit();
    std::printf("acquire waits=%u barriers=%u broad_wait=%d broad_barrier=%d\n", acquire_waits,
                acquire_barriers, bad_wait, bad_barrier);
    REQUIRE(errors == 0 && acquire_waits == 16 && acquire_barriers == 16);
    REQUIRE(!bad_wait && !bad_barrier);
    std::puts("all native presentation stage and exact pixel regressions passed");
    return 0;
}
