// New preflight/ownership regressions reuse the unchanged native fixture.
#define main preflight_blur_fixture_main
#include "test_vulkan_blur.cpp"
#undef main

namespace
{
std::vector<VkBuffer> geometry_buffers(vk_detail::surface &surface)
{
    std::vector<VkBuffer> result;
    for (const auto &slot : surface.slots)
        for (const auto &page : slot.pages)
            if (page.geometry) result.push_back(page.buffer);
    return result;
}
bool preflight_guards(vk_detail::device &d, vk_detail::surface &surface)
{
    const geometry_capacity capacity{4096, 8192};
    CHECK(!vulkan_prepare_surface_geometry(nullptr, capacity));
    null_device unsupported;
    CHECK(!vulkan_prepare_surface_geometry(unsupported.create_surface(), capacity));
    CHECK(!vulkan_prepare_surface_geometry(&surface, {usize(0x80000000u), 1}));
    const auto initial = d.stats;
    const auto buffers = geometry_buffers(surface);
    surface.dirty = true;
    CHECK(!vulkan_prepare_surface_geometry(&surface, capacity));
    surface.dirty = false;
    const auto extent = surface.extent;
    surface.extent.width = 0;
    CHECK(!vulkan_prepare_surface_geometry(&surface, capacity));
    surface.extent = extent;
    auto &slot = surface.slots.back();
    const auto serial = slot.serial, group = slot.group_serial;
    slot.serial = d.known_completed + 1;
    CHECK(!vulkan_prepare_surface_geometry(&surface, capacity));
    slot.serial = serial;
    slot.group_serial = d.known_completed + 1;
    CHECK(!vulkan_prepare_surface_geometry(&surface, capacity));
    slot.group_serial = group;
    slot.recording = true;
    CHECK(!vulkan_prepare_surface_geometry(&surface, capacity));
    slot.recording = false;
    CHECK(geometry_buffers(surface) == buffers);
    CHECK(d.stats.buffer_allocations == initial.buffer_allocations);
    CHECK(d.stats.submissions == initial.submissions);
    CHECK(d.stats.slot_wait_ms == initial.slot_wait_ms);
    CHECK(vulkan_last_result(&d) == VK_SUCCESS);
    return true;
}
bool preflight_frames(vulkan_device_opts options)
{
    auto *d = static_cast<vk_detail::device *>(create_vulkan_device(options));
    CHECK(d);
    auto *surface =
        static_cast<vk_detail::surface *>(create_vulkan_surface(d, {.width = 192, .height = 128}));
    CHECK(surface);
    // Initial white/texture uploads may still own the utility recording.
    if (d->frame && d->frame->recording)
        CHECK(!vulkan_prepare_surface_geometry(surface, {4096, 8192}));
    CHECK(vulkan_wait_idle(d) && preflight_guards(*d, *surface));
    d->set_vsync(!d->vsync);
    CHECK(!surface->dirty); // offscreen pacing changes have no swapchain effect
    const geometry_capacity capacity{4096, 8192};
    const auto initial = d->stats;
    CHECK(vulkan_prepare_surface_geometry(surface, {}));
    CHECK(vulkan_prepare_surface_geometry(surface, capacity));
    const auto ready = d->stats;
    CHECK(ready.submissions == initial.submissions && ready.slot_wait_ms == initial.slot_wait_ms);
    CHECK(ready.buffer_allocations > initial.buffer_allocations);
    CHECK(ready.upload_allocated_bytes <= options.upload_budget_bytes);
    auto buffers = geometry_buffers(*surface);
    CHECK(buffers.size() == options.frames_in_flight);
    CHECK(vulkan_prepare_surface_geometry(surface, capacity));
    CHECK(d->stats.buffer_allocations == ready.buffer_allocations);
    context *ctx = create_context(d, surface);
    CHECK(ctx && ui(ctx).prepare_geometry(capacity));
    std::vector<u8> first(192 * 128 * 4), next(first.size());
    u64 draw_allocations = d->stats.buffer_allocations;
    for (i32 f = 0; f < 4; ++f)
    {
        begin_frame(ctx, f / 60.0, 1.0 / 60.0, {0, 0, 192, 128});
        CHECK(!vulkan_prepare_surface_geometry(surface, capacity));
        ui(ctx).draw_rect({0, 0, 192, 128}, {4, 8, 12, 255});
        ui(ctx).draw_rounded_rect({9.25f, 12.5f, 131, 87}, {40, 180, 230, 139}, 13.5f);
        end_frame(ctx);
        CHECK(d->stats.buffer_allocations == draw_allocations);
        CHECK(d->read_pixels(surface->scene_target(), 0, 0, 192, 128, next.data()));
        if (!f) first = next;
        CHECK(first == next);
        CHECK(geometry_buffers(*surface) == buffers);
        draw_allocations = d->stats.buffer_allocations; // readback storage is independent
    }
    CHECK(vulkan_wait_idle(d));
    CHECK(vulkan_prepare_surface_geometry(surface, capacity)); // completed used pages
    CHECK(geometry_buffers(*surface) == buffers);
    CHECK(!vulkan_prepare_surface_geometry(surface, {2000000, 8000000})); // bounded hint
    CHECK(d->stats.upload_allocated_bytes <= options.upload_budget_bytes);
    surface->resize();
    CHECK(!vulkan_prepare_surface_geometry(surface, capacity));
    begin_frame(ctx, 1, 1.0 / 60.0, {0, 0, 192, 128});
    ui(ctx).draw_rect({1, 2, 50, 40}, color::white());
    end_frame(ctx);
    CHECK(vulkan_wait_idle(d) && vulkan_prepare_surface_geometry(surface, capacity));
    CHECK(!violation_count(ctx) && !d->stats.validation_errors);
    destroy_context(ctx);
    d->destroy_surface(surface);
    destroy_vulkan_device(d);
    return true;
}
bool preflight_budget(vulkan_device_opts options)
{
    options.frames_in_flight = 2;
    options.upload_budget_bytes = 8 * 1024 * 1024;
    auto *d = static_cast<vk_detail::device *>(create_vulkan_device(options));
    CHECK(d);
    auto *surface =
        static_cast<vk_detail::surface *>(create_vulkan_surface(d, {.width = 192, .height = 128}));
    CHECK(surface);
    CHECK(vulkan_wait_idle(d));
    vk_detail::page occupied;
    // Leave insufficient room for both prepared pages. Setup must decline
    // without invoking the ordinary allocator's trimming/wait fallback.
    const VkDeviceSize free = options.upload_budget_bytes - d->stats.upload_allocated_bytes;
    CHECK(free > 512 * 1024);
    CHECK(d->allocate_buffer(occupied, free - 256 * 1024, VK_BUFFER_USAGE_TRANSFER_SRC_BIT));
    std::vector<VkBuffer> utility_buffers;
    for (const auto &page : d->utility.pages) utility_buffers.push_back(page.buffer);
    d->utility.pages.push_back(occupied);
    const auto initial = d->stats;
    CHECK(!vulkan_prepare_surface_geometry(surface, {4096, 8192}));
    CHECK(d->utility.pages.size() == utility_buffers.size() + 1 &&
          d->utility.pages.back().buffer == occupied.buffer);
    for (usize i = 0; i < utility_buffers.size(); ++i)
        CHECK(d->utility.pages[i].buffer == utility_buffers[i]);
    CHECK(d->stats.submissions == initial.submissions &&
          d->stats.slot_wait_ms == initial.slot_wait_ms);
    CHECK(d->stats.budget_waits == initial.budget_waits);
    CHECK(vulkan_last_result(d) == VK_SUCCESS && !d->preparing_geometry);
    d->free_page(d->utility.pages.back());
    d->utility.pages.pop_back();
    CHECK(vulkan_prepare_surface_geometry(surface, {4096, 8192}));
    CHECK(d->stats.peak_upload_allocated_bytes <= options.upload_budget_bytes);
    CHECK(vulkan_wait_idle(d) && !d->stats.validation_errors);
    d->destroy_surface(surface);
    destroy_vulkan_device(d);
    return true;
}
struct preflight_failure
{
    i32 callbacks = 0;
    static void capture(i32, const char *, void *user)
    {
        ++static_cast<preflight_failure *>(user)->callbacks;
    }
};
VKAPI_ATTR VkResult VKAPI_CALL preflight_fail_allocate(VkDevice, const VkMemoryAllocateInfo *,
                                                       const VkAllocationCallbacks *,
                                                       VkDeviceMemory *out)
{
    *out = VK_NULL_HANDLE;
    return VK_ERROR_OUT_OF_DEVICE_MEMORY;
}
VKAPI_ATTR VkResult VKAPI_CALL preflight_fail_map(VkDevice, VkDeviceMemory, VkDeviceSize,
                                                  VkDeviceSize, VkMemoryMapFlags, void **out)
{
    *out = nullptr;
    return VK_ERROR_MEMORY_MAP_FAILED;
}
bool preflight_failures(vulkan_device_opts options)
{
    for (bool map : {false, true})
    {
        preflight_failure capture;
        options.error_callback = preflight_failure::capture;
        options.user = &capture;
        auto *d = static_cast<vk_detail::device *>(create_vulkan_device(options));
        CHECK(d);
        auto *surface = create_vulkan_surface(d, {.width = 192, .height = 128});
        CHECK(surface);
        CHECK(vulkan_wait_idle(d));
        const auto allocate = d->vkAllocateMemory;
        const auto mapping = d->vkMapMemory;
        if (map)
            d->vkMapMemory = preflight_fail_map;
        else
            d->vkAllocateMemory = preflight_fail_allocate;
        CHECK(!vulkan_prepare_surface_geometry(surface, {4096, 8192}));
        d->vkAllocateMemory = allocate;
        d->vkMapMemory = mapping;
        CHECK(!d->preparing_geometry && capture.callbacks == 1);
        CHECK(vulkan_last_result(d) ==
              (map ? VK_ERROR_MEMORY_MAP_FAILED : VK_ERROR_OUT_OF_DEVICE_MEMORY));
        CHECK(!d->stats.validation_errors);
        d->destroy_surface(surface);
        destroy_vulkan_device(d);
    }
    return true;
}
} // namespace

int main(int argc, char **argv)
{
    bool staged = false;
    if (argc == 2 && !std::strcmp(argv[1], "--staged"))
        staged = true;
    else if (argc != 1)
        return 2;
    for (bool compact : {false, true})
        for (u32 slots : {1u, 2u})
        {
            vulkan_device_opts options;
            options.validation = true;
            options.error_callback = diagnostic;
            options.compact_vertices = compact;
            options.frames_in_flight = slots;
            options.upload_mode = staged ? vulkan_upload_mode::STAGED : vulkan_upload_mode::DIRECT;
            if (!preflight_frames(options)) return 1;
        }
    vulkan_device_opts options;
    options.validation = true;
    options.error_callback = diagnostic;
    options.upload_mode = staged ? vulkan_upload_mode::STAGED : vulkan_upload_mode::DIRECT;
    if (!preflight_budget(options) || !preflight_failures(options) || errors) return 1;
    std::puts("all Vulkan surface preparation regressions passed");
    return 0;
}
