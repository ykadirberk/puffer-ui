#include <pufferui/vulkan.h>
#if defined(PUFFERUI_ENABLE_SDL3)
#include <SDL3/SDL.h>
#endif
#include <cstdio>
#include <cstring>
#include <vector>
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
namespace
{
enum fault
{
    NONE,
    BUFFER,
    MEMORY,
    MAP,
    DESCRIPTORS,
    DESCRIPTOR_RETRY,
    SUBMIT,
    ACQUIRE,
    DEVICE_BUFFER
};
thread_local fault armed = NONE;
thread_local u32 injected = 0, flushes = 0, invalidates = 0;
thread_local bool noncoherent = false, padded = false;
struct capture
{
    u32 calls = 0;
    i32 code = 0;
};
void quiet(i32 code, const char *, void *user)
{
    auto *c = static_cast<capture *>(user);
    ++c->calls;
    c->code = code;
}
VkResult VKAPI_CALL create_buffer(VkDevice d, const VkBufferCreateInfo *c,
                                  const VkAllocationCallbacks *a, VkBuffer *out)
{
    if (armed == BUFFER ||
        (armed == DEVICE_BUFFER && (c->usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) &&
         !(c->usage & VK_BUFFER_USAGE_TRANSFER_SRC_BIT)))
    {
        ++injected;
        *out = VK_NULL_HANDLE;
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    return ::vkCreateBuffer(d, c, a, out);
}
VkResult VKAPI_CALL allocate(VkDevice d, const VkMemoryAllocateInfo *c,
                             const VkAllocationCallbacks *a, VkDeviceMemory *out)
{
    if (armed == MEMORY)
    {
        ++injected;
        *out = VK_NULL_HANDLE;
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    return ::vkAllocateMemory(d, c, a, out);
}
VkResult VKAPI_CALL map(VkDevice d, VkDeviceMemory m, VkDeviceSize o, VkDeviceSize s,
                        VkMemoryMapFlags flags, void **out)
{
    if (armed == MAP)
    {
        ++injected;
        *out = nullptr;
        return VK_ERROR_MEMORY_MAP_FAILED;
    }
    return ::vkMapMemory(d, m, o, s, flags, out);
}
VkResult VKAPI_CALL descriptors(VkDevice d, const VkDescriptorSetAllocateInfo *c,
                                VkDescriptorSet *out)
{
    if (armed == DESCRIPTORS || armed == DESCRIPTOR_RETRY)
    {
        ++injected;
        for (u32 i = 0; i < c->descriptorSetCount; ++i) out[i] = VK_NULL_HANDLE;
        if (armed == DESCRIPTOR_RETRY) armed = NONE;
        return VK_ERROR_OUT_OF_POOL_MEMORY;
    }
    return ::vkAllocateDescriptorSets(d, c, out);
}
VkResult VKAPI_CALL submit(VkQueue q, u32 n, const VkSubmitInfo2 *s, VkFence f)
{
    if (armed == SUBMIT)
    {
        ++injected;
        return VK_ERROR_DEVICE_LOST;
    }
    return ::vkQueueSubmit2(q, n, s, f);
}
VkResult VKAPI_CALL acquire(VkDevice d, VkSwapchainKHR s, uint64_t timeout, VkSemaphore sem,
                            VkFence fence, u32 *image)
{
    if (armed == ACQUIRE)
    {
        ++injected;
        armed = NONE;
        return VK_ERROR_OUT_OF_DATE_KHR;
    }
    return ::vkAcquireNextImageKHR(d, s, timeout, sem, fence, image);
}
void VKAPI_CALL requirements(VkDevice d, VkBuffer b, VkMemoryRequirements *r)
{
    ::vkGetBufferMemoryRequirements(d, b, r);
    if (padded) r->size += 4096;
}
VkResult VKAPI_CALL flush(VkDevice d, u32 n, const VkMappedMemoryRange *ranges)
{
    ++flushes;
    return ::vkFlushMappedMemoryRanges(d, n, ranges);
}
VkResult VKAPI_CALL invalidate(VkDevice d, u32 n, const VkMappedMemoryRange *ranges)
{
    ++invalidates;
    return ::vkInvalidateMappedMemoryRanges(d, n, ranges);
}
void VKAPI_CALL memory_properties(VkPhysicalDevice d, VkPhysicalDeviceMemoryProperties *p)
{
    ::vkGetPhysicalDeviceMemoryProperties(d, p);
    if (noncoherent)
        for (u32 i = 0; i < p->memoryTypeCount; ++i)
            p->memoryTypes[i].propertyFlags &= ~VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
}
PFN_vkVoidFunction VKAPI_CALL device_proc(VkDevice d, const char *name)
{
#define OVERRIDE(name_, fn)                                                                        \
    if (std::strcmp(name, "vk" name_) == 0) return reinterpret_cast<PFN_vkVoidFunction>(fn)
    OVERRIDE("CreateBuffer", create_buffer);
    OVERRIDE("AllocateMemory", allocate);
    OVERRIDE("MapMemory", map);
    OVERRIDE("AllocateDescriptorSets", descriptors);
    OVERRIDE("QueueSubmit2", submit);
    OVERRIDE("AcquireNextImageKHR", acquire);
    OVERRIDE("GetBufferMemoryRequirements", requirements);
    OVERRIDE("FlushMappedMemoryRanges", flush);
    OVERRIDE("InvalidateMappedMemoryRanges", invalidate);
#undef OVERRIDE
    return ::vkGetDeviceProcAddr(d, name);
}
PFN_vkVoidFunction VKAPI_CALL instance_proc(VkInstance i, const char *name)
{
    if (std::strcmp(name, "vkGetPhysicalDeviceMemoryProperties") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(memory_properties);
    return ::vkGetInstanceProcAddr(i, name);
}
vulkan_native_device borrowed(render_device *owner)
{
    auto n = vulkan_handles(owner);
    n.get_device_proc_addr = device_proc;
    n.get_instance_proc_addr = instance_proc;
    return n;
}
} // namespace
int main()
{
    capture validation;
    vulkan_device_opts opts;
    opts.validation = true;
    opts.error_callback = quiet;
    opts.user = &validation;
    render_device *owner = create_vulkan_device(opts);
    REQUIRE(owner);
    REQUIRE(vulkan_wait_idle(owner));
    auto native = borrowed(owner);
    for (fault mode : {BUFFER, MEMORY, MAP, DESCRIPTORS, SUBMIT, DESCRIPTOR_RETRY, DEVICE_BUFFER})
    {
        capture error;
        vulkan_device_opts e;
        e.native = &native;
        e.user = &error;
        e.error_callback = quiet;
        e.tile_cache = true;
        if (mode == DEVICE_BUFFER) e.upload_mode = vulkan_upload_mode::STAGED;
        render_device *d = create_vulkan_device(e);
        REQUIRE(d);
        REQUIRE(vulkan_wait_idle(d));
        armed = mode;
        injected = 0;
        if (mode == SUBMIT)
        {
            armed = NONE;
            render_surface *s = create_vulkan_surface(d, {.width = 16, .height = 16});
            REQUIRE(s);
            s->make_current(*d);
            d->clear({4, 5, 6, 255});
            armed = SUBMIT;
            s->present();
            REQUIRE(!vulkan_wait_idle(d));
            REQUIRE(vulkan_last_result(d) == VK_ERROR_DEVICE_LOST);
        }
        else if (mode == DEVICE_BUFFER)
        {
            armed = NONE;
            texture_handle t = d->create_target(16, 16);
            REQUIRE(t);
            d->set_target(t);
            d->clear({4, 5, 6, 255});
            REQUIRE(vulkan_wait_idle(d));
            const u64 before = vulkan_statistics(d).upload_allocated_bytes;
            armed = DEVICE_BUFFER;
            const vertex v[] = {{0, 0, 0, 0, {255, 0, 0, 255}},
                                {16, 0, 0, 0, {255, 0, 0, 255}},
                                {0, 16, 0, 0, {255, 0, 0, 255}}};
            const i32 idx[] = {0, 1, 2};
            d->draw(nullptr, v, 3, idx, 3);
            REQUIRE(error.calls == 1);
            REQUIRE(vulkan_last_result(d) == VK_ERROR_OUT_OF_DEVICE_MEMORY);
            REQUIRE(vulkan_statistics(d).upload_allocated_bytes == before);
            REQUIRE(vulkan_statistics(d).device_upload_allocated_bytes == 0);
        }
        else
        {
            std::vector<u8> pixels(1024 * 512 * 4, 0x7f);
            texture_handle t = d->create_texture(1024, 512, pixels.data());
            if (mode == DESCRIPTOR_RETRY)
            {
                REQUIRE(t);
                d->destroy_texture(t);
                REQUIRE(vulkan_wait_idle(d));
                REQUIRE(error.calls == 0);
            }
            else
            {
                REQUIRE(!t);
                REQUIRE(error.calls == 1);
                REQUIRE(vulkan_last_result(d) != VK_SUCCESS);
            }
        }
        REQUIRE(injected > 0);
        if (mode == SUBMIT) REQUIRE(error.calls == 1);
        armed = NONE;
        destroy_vulkan_device(d);
        REQUIRE(vulkan_wait_idle(owner));
        REQUIRE(validation.calls == 0);
        REQUIRE(vulkan_statistics(owner).validation_errors == 0);
    }
    // Force explicit cache flush/invalidate paths on coherent hardware. The
    // driver still supplies real valid mappings; reported capabilities are wrapped.
    noncoherent = true;
    padded = true;
    flushes = invalidates = 0;
    capture error;
    vulkan_device_opts e;
    e.native = &native;
    e.user = &error;
    e.error_callback = quiet;
    e.upload_budget_bytes = 1024 * 1024 + 8192;
    render_device *d = create_vulkan_device(e);
    REQUIRE(d);
    texture_handle t = d->create_target(16, 16);
    REQUIRE(t);
    d->set_target(t);
    d->clear({7, 8, 9, 255});
    u8 pixel[4]{}, row[64]{};
    REQUIRE(d->read_pixels(t, 0, 0, 16, 1, row));
    REQUIRE(d->read_pixels(t, 0, 0, 1, 1, pixel));
    REQUIRE(pixel[0] == 7 && pixel[1] == 8 && pixel[2] == 9);
    REQUIRE(flushes > 0 && invalidates > 0);
    REQUIRE(vulkan_statistics(d).budget_waits > 0);
    REQUIRE(vulkan_statistics(d).peak_upload_allocated_bytes <= e.upload_budget_bytes);
    d->destroy_target(t);
    REQUIRE(vulkan_wait_idle(d));
    destroy_vulkan_device(d);
    REQUIRE(error.calls == 0);
    noncoherent = false;
    padded = false;
    destroy_vulkan_device(owner);
    REQUIRE(validation.calls == 0);
#if defined(PUFFERUI_ENABLE_SDL3)
    sdl3_app app;
    app.hidden = true;
    sdl3_vulkan_app_opts init;
    init.width = 128;
    init.height = 96;
    init.device.validation = true;
    init.device.user = &validation;
    init.device.error_callback = quiet;
    REQUIRE(sdl3_vulkan_app_init(app, init));
    REQUIRE(vulkan_wait_idle(app.device));
    native = borrowed(app.device);
    e = {};
    e.native = &native;
    e.user = &validation;
    e.error_callback = quiet;
    d = create_sdl3_vulkan_device(e);
    REQUIRE(d);
    SDL_Window *window =
        SDL_CreateWindow("acquire fault", 64, 64, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
    REQUIRE(window);
    render_surface *s = d->create_surface(window);
    REQUIRE(s);
    armed = ACQUIRE;
    injected = 0;
    s->make_current(*d);
    s->present();
    REQUIRE(injected == 1);
    REQUIRE(vulkan_last_result(d) == VK_SUCCESS);
    s->make_current(*d);
    d->clear({1, 2, 3, 255});
    s->present();
    REQUIRE(vulkan_wait_idle(d));
    d->destroy_surface(s);
    destroy_vulkan_device(d);
    SDL_DestroyWindow(window);
    sdl3_app_shutdown(app);
    REQUIRE(validation.calls == 0);
#endif
    std::puts("all Vulkan fault injection tests passed");
    return 0;
}
