#include "vulkan_test_options.h"
// Independent memory-role regressions reuse the unchanged actual showcase driver.
#include "test_vulkan_preparation.inl"

namespace
{
PFN_vkGetDeviceProcAddr role_resolver = nullptr;
PFN_vkAllocateMemory role_allocator = nullptr;
u32 role_lut_allocations = 0;
VkResult VKAPI_CALL reject_local_lut(VkDevice device, const VkMemoryAllocateInfo *info,
                                     const VkAllocationCallbacks *callbacks, VkDeviceMemory *memory)
{
    if (info->allocationSize >= vk_detail::rgba8_blend_lut::BYTES &&
        info->allocationSize <= vk_detail::rgba8_blend_lut::BYTES + 65536 &&
        ++role_lut_allocations == 2)
    {
        *memory = VK_NULL_HANDLE;
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    return role_allocator(device, info, callbacks, memory);
}
PFN_vkVoidFunction VKAPI_CALL role_device_proc(VkDevice device, const char *name)
{
    if (!std::strcmp(name, "vkAllocateMemory"))
        return reinterpret_cast<PFN_vkVoidFunction>(reject_local_lut);
    return role_resolver(device, name);
}
} // namespace

PUI_TEST(memory_role_actual_spiral_has_bounded_prepared_work_input_and_readable_leases)
{
    auto options = preparation_options();
#if defined(PUFFERUI_ENABLE_HARFBUZZ)
    options.vector_text = true;
#endif
    auto *base = create_vulkan_device(options);
    CHECK(base);
    if (!base) return;
    example_app app;
    CHECK(initialize(app, base, 5000));
    if (!app.ctx)
    {
        destroy_vulkan_device(base);
        return;
    }
    auto &d = *static_cast<vk_detail::device *>(base);
    const auto allocations = d.stats.gpu_metadata_allocations;
    std::vector<u8> unused;
    for (i32 f = 0; f < FRAMES; ++f) CHECK(render(app, f, unused, false));
    CHECK(d.stats.gpu_metadata_allocations == allocations);
    for (const auto &slot : static_cast<vk_detail::surface *>(app.surface)->slots)
    {
        const auto &r = *slot.sdf_resources;
        std::fprintf(stderr, "role work=%llu/%llu input=%llu/%llu readable=%llu/%llu\n",
                     r.role_peaks[0], r.banks[0].capacity, r.role_peaks[1], r.inputs[0].capacity,
                     r.role_peaks[2], r.readable[0].capacity);
        CHECK(r.role_peaks[0] <= r.banks[0].capacity);
        CHECK(r.role_peaks[1] <= r.inputs[0].capacity);
        CHECK(r.role_peaks[2] <= r.readable[0].capacity);
        CHECK(!r.banks[0].mapped && r.inputs[0].mapped && r.readable[0].mapped);
        CHECK(!r.inputs[1].buffer); // staging never retains a history bank
    }
    CHECK(!d.stats.gpu_sdf_capacity_bypasses);
    CHECK(d.stats.peak_gpu_metadata_allocated_bytes <= options.gpu_command_budget_bytes);
    CHECK(!d.sdf_blend->storage.buffers[1].mapped);
    CHECK(vulkan_wait_idle(base));
    destroy_context(app.ctx);
    base->destroy_surface(app.surface);
    CHECK(!d.stats.validation_errors);
    destroy_vulkan_device(base);
}

PUI_TEST(memory_role_selection_prefers_local_work_and_readable_control_with_portable_fallback)
{
    auto *base = create_vulkan_device();
    CHECK(base);
    if (!base) return;
    auto &d = *static_cast<vk_detail::device *>(base);
    const auto actual = d.memory_properties;
    d.memory_properties = {};
    d.memory_properties.memoryTypeCount = 3;
    d.memory_properties.memoryTypes[0].propertyFlags =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    d.memory_properties.memoryTypes[1].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    d.memory_properties.memoryTypes[2].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    CHECK(d.memory_type(7, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == 1);
    CHECK(d.memory_type(7, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0);
    CHECK(d.memory_type(7, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == 2);
    CHECK(d.memory_type(1, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == 0);
    CHECK(d.memory_type(1, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == 0);
    CHECK(d.memory_type(2, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 0) == UINT32_MAX);
    d.memory_properties = actual;
    destroy_vulkan_device(base);
}

PUI_TEST(memory_role_lut_migration_budget_rejection_preserves_the_calibrated_host_table)
{
    vulkan_device_opts options = test_detail::reference_options();
    options.painter = vulkan_painter_mode::COMPUTE_SDF;
    options.gpu_command_budget_bytes = 20 * 1024 * 1024;
    auto *base = create_vulkan_device(options);
    CHECK(base);
    if (!base) return;
    auto &d = *static_cast<vk_detail::device *>(base);
    CHECK(d.sdf_blend->valid && d.sdf_blend->storage.buffers[1].mapped);
    CHECK(d.stats.gpu_metadata_allocated_bytes <= options.gpu_command_budget_bytes);
    CHECK(vulkan_last_result(base) == VK_SUCCESS);
    destroy_vulkan_device(base);
}

PUI_TEST(memory_role_lut_migration_allocation_failure_fails_creation_and_preserves_borrowed_device)
{
    auto *owner = create_vulkan_device();
    CHECK(owner);
    if (!owner) return;
    auto &d = *static_cast<vk_detail::device *>(owner);
    auto native = vulkan_handles(owner);
    role_resolver = d.vkGetDeviceProcAddr;
    role_allocator =
        reinterpret_cast<PFN_vkAllocateMemory>(role_resolver(native.device, "vkAllocateMemory"));
    role_lut_allocations = 0;
    native.get_device_proc_addr = role_device_proc;
    vulkan_device_opts options = test_detail::reference_options();
    options.native = &native;
    options.painter = vulkan_painter_mode::COMPUTE_SDF;
    u32 errors = 0;
    options.user = &errors;
    options.error_callback = [](i32 result, const char *, void *user)
    {
        CHECK(result == VK_ERROR_OUT_OF_DEVICE_MEMORY);
        ++*static_cast<u32 *>(user);
    };
    auto *child = create_vulkan_device(options);
    CHECK(!child && errors && role_lut_allocations == 2);
    if (child) destroy_vulkan_device(child);
    CHECK(vulkan_wait_idle(owner) && vulkan_last_result(owner) == VK_SUCCESS);
    auto target = owner->create_target(16, 16);
    CHECK(target);
    owner->destroy_target(target);
    destroy_vulkan_device(owner);
}
