// Native Vulkan public types, shared by the optional header and implementation.
// The main declaration section remains free of SDK dependencies.
// This slice is included outside namespace pui and can be amalgamated.
// Exactly one definition, independent of public header inclusion order.
#ifndef PUFFERUI_VULKAN_NATIVE_TYPES
#define PUFFERUI_VULKAN_NATIVE_TYPES
#include <vulkan/vulkan.h>

namespace pui
{
// Borrowed handles. The caller enabled these three Vulkan 1.3 features and
// externally serializes its queue submissions with PufferUI calls.
struct vulkan_native_device
{
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    u32 queue_family = 0;
    bool dynamic_rendering = false;
    bool synchronization2 = false;
    bool timeline_semaphore = false;
    PFN_vkGetInstanceProcAddr get_instance_proc_addr = nullptr;
    PFN_vkGetDeviceProcAddr get_device_proc_addr = nullptr;
};

// Platform adapter; the instance owns the returned surface. The host and native
// window outlive their surfaces. SDL's adapter is supplied by the shorthand.
struct vulkan_host
{
    virtual ~vulkan_host() = default;
    virtual std::span<const char *const> instance_extensions() = 0;
    virtual VkSurfaceKHR create_surface(VkInstance instance, void *window) = 0;
    virtual void output_size(void *window, i32 &w, i32 &h) = 0;
    virtual void set_cursor(cursor) {}
    // nullptr selects the backend loader. Public consumers may define
    // VK_NO_PROTOTYPES and provide their engine's entry point when needed.
    virtual PFN_vkGetInstanceProcAddr instance_proc_addr() { return nullptr; }
};

// Image and view remain owned by the caller. They must support SAMPLED usage;
// use image_handle{import_vulkan_image(...), width, height} with ui::image.
// Queue family must be the UI family or IGNORED (concurrent sharing).
// A ready semaphore can transfer producer completion; timeline value 0 means
// a binary semaphore. Do not modify/destroy the image until release completes.
struct vulkan_image_opts
{
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    i32 width = 0, height = 0;
    u32 queue_family = VK_QUEUE_FAMILY_IGNORED;
    VkSemaphore ready = VK_NULL_HANDLE;
    u64 ready_value = 0;
    bool linear = true;
};
texture_handle import_vulkan_image(render_device *device, const vulkan_image_opts &opts);
// Submit pending uses, transition to final_layout, and return a value on the
// backend-owned timeline. Wait for that value before producer writes/destroy.
u64 release_vulkan_image(render_device *device, texture_handle image,
                         VkImageLayout final_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
VkSemaphore vulkan_completion_semaphore(render_device *device);
bool vulkan_wait(render_device *device, u64 value);
bool vulkan_wait_idle(render_device *device);
vulkan_native_device vulkan_handles(render_device *device);
VkResult vulkan_last_result(render_device *device);
const char *vulkan_last_error(render_device *device);
struct vulkan_stats
{
    u64 glyph_upload_bytes = 0, glyph_instances = 0, glyph_draws = 0;
    u64 glyph_coverage_hits = 0, glyph_coverage_misses = 0, glyph_coverage_fills = 0;
    u64 glyph_coverage_draws = 0, glyph_coverage_bytes = 0, glyph_coverage_evictions = 0;
    u64 buffer_allocations = 0, draw_buffer_allocations = 0;
    u64 pipeline_binds = 0, viewport_sets = 0, scissor_sets = 0, descriptor_binds = 0;
    u64 submissions = 0, draws = 0, vertices = 0, indices = 0;
    u64 upload_bytes = 0, upload_allocated_bytes = 0, budget_waits = 0;
    u64 cache_hits = 0, cache_misses = 0, cache_fallbacks = 0;
    u64 cache_bypasses = 0;
    u64 cache_metadata_bytes = 0, peak_cache_metadata_bytes = 0, cache_evictions = 0;
    u64 validation_errors = 0, live_textures = 0;
    u64 requested_draws = 0, requested_vertices = 0, requested_indices = 0;
    u64 peak_upload_allocated_bytes = 0, image_bytes = 0, retired_bytes = 0, descriptor_sets = 0;
    u64 device_upload_allocated_bytes = 0;
    u64 readbacks = 0, readback_bytes = 0, partial_replays = 0, dirty_tiles = 0, total_tiles = 0;
    f64 acquire_ms = 0, slot_wait_ms = 0, submit_ms = 0, present_ms = 0, readback_ms = 0,
        cache_ms = 0, cache_prepare_ms = 0;
    bool timestamps_supported = false;
    u64 gpu_samples = 0;
    f64 completed_gpu_ms = 0; // delayed completed submissions, not current CPU frame
};
vulkan_stats vulkan_statistics(render_device *device);
void vulkan_set_cache(render_device *device, bool enabled);
VkPresentModeKHR vulkan_present_mode(render_surface *surface); // MAX_ENUM for offscreen/non-Vulkan
} // namespace pui
#endif
