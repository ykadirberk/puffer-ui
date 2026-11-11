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
bool analytic_advisory_capacity(vulkan_device_opts options)
{
    options.analytic_rounded_rects = true;
    auto *d = static_cast<vk_detail::device *>(create_vulkan_device(options));
    CHECK(d);
    auto *surface =
        static_cast<vk_detail::surface *>(create_vulkan_surface(d, {.width = 1360, .height = 860}));
    CHECK(surface && vulkan_wait_idle(d));
    constexpr VkDeviceSize mib = 1024 * 1024;
    const geometry_capacity oversized{2000000, 8000000};
    CHECK(!vulkan_prepare_surface_geometry(surface, oversized));
    for (const auto &slot : surface->slots) CHECK(slot.geometry_chunk_bytes == 4 * mib);
    const auto reserved = geometry_buffers(*surface);
    const auto initial = d->stats;
    CHECK(!vulkan_prepare_surface_geometry(surface, oversized));
    CHECK(d->stats.buffer_allocations == initial.buffer_allocations);

    context *ctx = create_context(d, surface);
    CHECK(ctx);
    begin_frame(ctx, 0, 1.0 / 60.0, {0, 0, 1360, 860});
    CHECK(!d->prepare_geometry(oversized));
    ui(ctx).draw_rounded_rect({10, 10, 64, 64}, color::white(), 4);
    end_frame(ctx);
    CHECK(vulkan_wait_idle(d));
    CHECK(geometry_buffers(*surface) == reserved);
    CHECK(d->stats.draw_buffer_allocations == initial.draw_buffer_allocations);

    // Emulate a completed frame with a larger measured geometry footprint.
    surface->geometry_peak = 3 * mib;
    CHECK(!vulkan_prepare_surface_geometry(surface, oversized));
    for (const auto &slot : surface->slots) CHECK(slot.geometry_chunk_bytes == 8 * mib);
    const geometry_capacity fitting{500000, 500000};
    CHECK(vulkan_prepare_surface_geometry(surface, fitting));
    for (const auto &slot : surface->slots) CHECK(slot.geometry_chunk_bytes == 16 * mib);
    CHECK(!violation_count(ctx) && !d->stats.validation_errors);
    destroy_context(ctx);
    d->destroy_surface(surface);
    destroy_vulkan_device(d);
    return true;
}
bool logical_slot_selection()
{
    vk_detail::surface surface;
    surface.slots.resize(2);
    surface.slots[1].group_serial = 9;
    CHECK(surface.next_frame_slot(5) == 1); // default cyclic policy
    surface.slots.resize(3);
    surface.slots[0].group_serial = 7;
    surface.slots[2].group_serial = 3;
    CHECK(surface.next_frame_slot(9) == 1);
    CHECK(surface.next_frame_slot(4) == 2); // completed owner
    CHECK(surface.next_frame_slot(2) == 2); // oldest pending owner
    surface.slots[2].group_serial = 10;
    CHECK(surface.next_frame_slot(2) == 0);
    return true;
}
bool window_blur_preflight(vulkan_device_opts options)
{
    options.frames_in_flight = 3;
    auto *d = static_cast<vk_detail::device *>(create_vulkan_device(options));
    CHECK(d);
    constexpr i32 width = 193, height = 129;
    auto *surface = static_cast<vk_detail::surface *>(
        create_vulkan_surface(d, {.width = width, .height = height}));
    auto *second = create_vulkan_surface(d, {.width = width, .height = height});
    CHECK(surface && second);
    context *ctx = create_context(d, surface);
    CHECK(ctx);
    auto *early = add_window(ctx, nullptr, surface, {0, 0, width, height});
    auto *late = add_window(ctx, nullptr, second, {0, 0, width, height});
    CHECK(early && late && surface->slots.size() == 3);
    CHECK(!vulkan_prepare_window_blur(nullptr, *early));
    window unregistered;
    CHECK(!vulkan_prepare_window_blur(ctx, unregistered));
    CHECK(!vulkan_prepare_window_blur(ctx, *early, {.gaussian_area = {0, 0, -1, 1}}));
    surface->resize();
    CHECK(!vulkan_prepare_window_blur(ctx, *early));
    CHECK(surface->rebuild());

    const vulkan_blur_capacity capacity{.gaussian_area = {0, 0, 109, 58}};
    const auto setup = d->stats;
    const auto *frame = d->frame;
    const bool recording = frame && frame->recording;
    CHECK(vulkan_prepare_window_blur(ctx, *early, capacity));
    CHECK(!late->blur && early->blur);
    const auto *a = static_cast<vk_detail::texture *>(early->blur->half);
    const auto *b = static_cast<vk_detail::texture *>(early->blur->quarter);
    CHECK(a && b && a->layout == VK_IMAGE_LAYOUT_UNDEFINED && b->layout == a->layout);
    CHECK(!a->last_use && !b->last_use && d->frame == frame);
    CHECK(!frame || frame->recording == recording);
    CHECK(d->stats.live_textures == setup.live_textures + 2);
    CHECK(d->stats.submissions == setup.submissions && d->stats.slot_wait_ms == setup.slot_wait_ms);
    const auto reserved = d->stats.image_bytes;
    CHECK(vulkan_prepare_window_blur(ctx, *early, capacity));
    CHECK(early->blur->half == a && early->blur->quarter == b && d->stats.image_bytes == reserved);

    std::vector<u8> reference(width * height * 4), actual(reference.size());
    for (f32 dpi : {1.0f, 2.0f, 4.0f})
    {
        set_window_scale(*early, dpi);
        set_window_scale(*late, dpi);
        for (bool cache : {false, true})
        {
            vulkan_set_cache(d, cache);
            CHECK(vulkan_prepare_window_blur(ctx, *early, capacity));
            for (auto *win : {early, late})
            {
                const auto image_bytes = d->stats.image_bytes;
                const auto prepared = win == early ? *win->blur : blur_store{};
                begin_frame(ctx, *win, 0, 1.0 / 60.0);
                CHECK(!vulkan_prepare_window_blur(ctx, *early, capacity));
                // On the first frame, initialize the reserved images at the
                // same in-frame boundary. Later draws also work with only the
                // preflight call, including when a larger DPI hint grows A.
                if (win == late || dpi == 1) CHECK(vulkan_prepare_blur(ctx, capacity));
                ui u(ctx);
                const f32 w = win->area.w, h = win->area.h;
                u.draw_rect({0, 0, w, h}, {21, 35, 72, 255});
                u.draw_rect({w * .31f, 0, w * .36f, h}, {188, 53, 82, 255});
                u.draw_rect({0, h * .38f, w, h * .31f}, {38, 144, 201, 211});
                u.blur({w * .18f, h * .1f, w * .54f, h * .43f}, 3.0f / dpi, 4.5f / dpi, .75f);
                u.blur({w * .37f, h * .54f, w * .56f, h * .4f}, 14.0f / dpi, 4.5f / dpi, .9f);
                end_frame(ctx);
                if (win == early)
                {
                    CHECK(win->blur->half == prepared.half &&
                          win->blur->quarter == prepared.quarter);
                    // A completed slot can retire the old scratch after a DPI change.
                    CHECK(d->stats.image_bytes <= image_bytes);
                }
                CHECK(d->read_pixels(win->surface->scene_target(), 0, 0, width, height,
                                     (win == early ? actual : reference).data()));
            }
            CHECK(actual == reference);
        }
    }
    CHECK(vulkan_wait_idle(d) && !d->stats.validation_errors && !violation_count(ctx));
    destroy_context(ctx);
    d->destroy_surface(second);
    d->destroy_surface(surface);
    destroy_vulkan_device(d);
    return true;
}
struct blur_allocation_fault
{
    static inline PFN_vkAllocateMemory original;
    static inline i32 calls = 0;
    static VKAPI_ATTR VkResult VKAPI_CALL fail_second(VkDevice device,
                                                      const VkMemoryAllocateInfo *info,
                                                      const VkAllocationCallbacks *allocator,
                                                      VkDeviceMemory *out)
    {
        if (++calls == 2) return preflight_fail_allocate(device, info, allocator, out);
        return original(device, info, allocator, out);
    }
};
bool window_blur_allocation_failure(vulkan_device_opts options)
{
    preflight_failure capture;
    options.error_callback = preflight_failure::capture;
    options.user = &capture;
    auto *d = static_cast<vk_detail::device *>(create_vulkan_device(options));
    CHECK(d);
    auto *surface = create_vulkan_surface(d, {.width = 193, .height = 129});
    CHECK(surface && vulkan_wait_idle(d));
    context *ctx = create_context(d, surface);
    CHECK(ctx);
    auto *win = add_window(ctx, nullptr, surface, {0, 0, 193, 129});
    CHECK(win);
    const auto before = d->stats;
    blur_allocation_fault::calls = 0;
    blur_allocation_fault::original = d->vkAllocateMemory;
    d->vkAllocateMemory = blur_allocation_fault::fail_second;
    const bool prepared = vulkan_prepare_window_blur(ctx, *win);
    d->vkAllocateMemory = blur_allocation_fault::original;
    CHECK(!prepared && !win->blur && capture.callbacks == 1 && blur_allocation_fault::calls == 2);
    CHECK(vulkan_last_result(d) == VK_ERROR_OUT_OF_DEVICE_MEMORY);
    d->collect();
    CHECK(d->stats.image_bytes == before.image_bytes &&
          d->stats.live_textures == before.live_textures);
    CHECK(d->stats.submissions == before.submissions &&
          d->stats.slot_wait_ms == before.slot_wait_ms);
    CHECK(!d->stats.validation_errors && !violation_count(ctx));
    destroy_context(ctx);
    d->destroy_surface(surface);
    destroy_vulkan_device(d);
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
    if (!preflight_budget(options) || !preflight_failures(options) ||
        !analytic_advisory_capacity(options) || !logical_slot_selection() ||
        !window_blur_preflight(options) || !window_blur_allocation_failure(options) || errors)
        return 1;
    std::puts("all Vulkan surface preparation regressions passed");
    return 0;
}
