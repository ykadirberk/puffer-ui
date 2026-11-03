// Native preparation failures preserve prior bindings and release partial objects.
namespace vector_fault_test
{
static i32 errors = 0;
static void error(i32, const char *, void *)
{
    ++errors;
}
static VKAPI_ATTR VkResult VKAPI_CALL buffer(VkDevice, const VkBufferCreateInfo *,
                                             const VkAllocationCallbacks *, VkBuffer *out)
{
    *out = VK_NULL_HANDLE;
    return VK_ERROR_OUT_OF_DEVICE_MEMORY;
}
static VKAPI_ATTR VkResult VKAPI_CALL map(VkDevice, VkDeviceMemory, VkDeviceSize, VkDeviceSize,
                                          VkMemoryMapFlags, void **out)
{
    *out = nullptr;
    return VK_ERROR_MEMORY_MAP_FAILED;
}
static VKAPI_ATTR VkResult VKAPI_CALL pool(VkDevice, const VkDescriptorPoolCreateInfo *,
                                           const VkAllocationCallbacks *, VkDescriptorPool *out)
{
    *out = VK_NULL_HANDLE;
    return VK_ERROR_OUT_OF_POOL_MEMORY;
}
} // namespace vector_fault_test
static void vector_native_fault_regressions()
{
    using namespace vk_detail;
    for (i32 fault = 0; fault < 3; ++fault)
    {
        vector_fault_test::errors = 0;
        auto *base = create_vulkan_device(
            {.validation = true, .vector_text = false, .error_callback = vector_fault_test::error});
        CHECK(base);
        if (!base) continue;
        auto &dev = *static_cast<device *>(base);
        auto *surface = create_vulkan_surface(base, {.width = 64, .height = 64});
        CHECK(surface);
        if (!surface)
        {
            destroy_vulkan_device(base);
            continue;
        }
        auto *ctx = create_context(base, surface);
        (void)add_window(ctx, nullptr, surface, {0, 0, 64, 64});
        ui u(ctx);
        auto a =
            load_svg("<svg viewBox='0 0 64 64'><rect x='8' y='8' width='48' height='48'/></svg>");
        auto b = load_svg("<svg viewBox='0 0 64 64'><circle cx='32' cy='32' r='20'/></svg>");
        CHECK(a && b);
        CHECK(u.prepare_vector(a.image));
        CHECK(vulkan_wait_idle(base));
        auto *painter = static_cast<native_painter *>(ctx->painter);
        auto *prior = painter->path_bindings->get(a.image);
        const auto bytes = dev.stats.device_upload_allocated_bytes, images = dev.stats.image_bytes;
        const auto old_buffer = dev.vkCreateBuffer;
        const auto old_pool = dev.vkCreateDescriptorPool;
        const auto old_map = dev.vkMapMemory;
        if (fault == 0) dev.vkCreateBuffer = vector_fault_test::buffer;
        if (fault == 1) dev.vkMapMemory = vector_fault_test::map;
        if (fault == 2) dev.vkCreateDescriptorPool = vector_fault_test::pool;
        const auto failed = u.prepare_vector(b.image);
        dev.vkCreateBuffer = old_buffer;
        dev.vkMapMemory = old_map;
        dev.vkCreateDescriptorPool = old_pool;
        CHECK(!failed && failed.error.code == vector_error_code::DEVICE_ERROR);
        CHECK(vector_fault_test::errors == 1 && vulkan_last_result(base) != VK_SUCCESS);
        CHECK(painter->path_bindings->get(a.image) == prior &&
              painter->path_bindings->get(b.image) == nullptr);
        CHECK(dev.paths->assets.size() == 1 && b.image->references.load() == 1);
        CHECK(dev.stats.device_upload_allocated_bytes == bytes && dev.stats.image_bytes == images);
        // Native device errors are sticky. Do not reset them merely to draw again.
        CHECK(u.release_vector(a.image));
        destroy_vector(a.image);
        destroy_vector(b.image);
        destroy_context(ctx);
        base->destroy_surface(surface);
        CHECK(dev.stats.validation_errors == 0);
        destroy_vulkan_device(base);
    }
    if (!g_failures)
        std::puts("all native vector buffer/map/descriptor failure regressions passed");
}
