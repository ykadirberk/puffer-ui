// Allocation/locality invariants of the real renderer, not a replacement backend.
#undef near
#undef far
#undef small
#include "test_util.h"

namespace
{
bool locality_staged = false;
struct locality_scene
{
    render_device *base = nullptr;
    vk_detail::device *native = nullptr;
    render_surface *surface = nullptr;
    context *ctx = nullptr;
    window *win = nullptr;
    u64 initial_cache_images = 0;
    explicit locality_scene(vulkan_device_opts options)
    {
        // Preserve the observation-specific fixture; defaults have a separate
        // compute/Slug policy regression. Assertions and pixel oracles stay fixed.
        options.native_observation = true;
        options.validation = true;
        options.vector_text = false;
        options.upload_mode =
            locality_staged ? vulkan_upload_mode::STAGED : vulkan_upload_mode::DIRECT;
        options.error_callback = [](i32 code, const char *, void *) { CHECK(code == 0); };
        base = create_vulkan_device(options);
        CHECK(base);
        if (!base) return;
        native = static_cast<vk_detail::device *>(base);
        initial_cache_images = native->stats.gpu_sdf_cache_image_bytes;
        surface = create_vulkan_surface(base, {.width = 64, .height = 48});
        CHECK(surface);
        if (!surface) return;
        ctx = create_context(base, surface);
        CHECK(ctx);
        if (ctx) win = add_window(ctx, nullptr, surface, {0, 0, 64, 48});
        CHECK(win);
    }
    ~locality_scene()
    {
        release_surface();
        if (base) destroy_vulkan_device(base);
    }
    void release_surface()
    {
        if (ctx) destroy_context(ctx);
        ctx = nullptr;
        win = nullptr;
        if (surface) base->destroy_surface(surface);
        surface = nullptr;
    }
    void frame(u32 index)
    {
        begin_frame(ctx, *win, static_cast<f64>(index) / 60, 1.0 / 60);
        {
            ui u(ctx);
            u.draw_rect(win->area, {13, 71, 131, 255});
        }
        end_frame(ctx);
        u8 pixel[4]{};
        CHECK(base->read_pixels(surface->scene_target(), 12, 12, 1, 1, pixel));
        CHECK(pixel[0] == 13 && pixel[1] == 71 && pixel[2] == 131 && pixel[3] == 255);
        CHECK(!violation_count(ctx));
        CHECK(vulkan_last_result(base) == VK_SUCCESS);
        CHECK(!native->stats.validation_errors);
    }
};
void check_job_pool(vk_detail::surface &surface)
{
    for (auto &slot : surface.slots)
    {
        CHECK(slot.sdf_resources);
        if (!slot.sdf_resources) continue;
        auto &resources = *slot.sdf_resources;
        CHECK(resources.job_storage);
        CHECK(resources.jobs.size() == vk_detail::sdf_slot_resources::MAX_JOBS);
        for (usize index = 0; index < resources.jobs.size(); ++index)
        {
            CHECK(resources.jobs[index] == resources.job_storage + index);
            CHECK(resources.jobs[index]->bin.dev == surface.dev);
        }
    }
}
} // namespace

PUI_TEST(integrated_observation_reserves_no_unused_cpu_tile_tape)
{
    locality_scene scene({});
    if (!scene.win) return;
    auto &d = *scene.native;
    CHECK(!d.sdf_tape.commands.capacity() && !d.sdf_tape.payload.capacity());
    auto &surface = *static_cast<vk_detail::surface *>(scene.surface);
    for (u32 frame = 0; frame < 6; ++frame)
    {
        scene.frame(frame);
        CHECK(!d.sdf_tape.commands.capacity() && !d.sdf_tape.payload.capacity());
        for (auto &slot : surface.slots) CHECK(!slot.sdf_resources);
    }
    // A later explicit fine-frame preparation must remain possible. Actual
    // LOAD-driven automatic transitions also retain their independent tests.
    d.options.whole_program_cache = false;
    surface.sdf_compute_requested = true;
    surface.program_compute_preparation_pending = true;
    scene.frame(6);
    CHECK(d.sdf_tape.commands.capacity() && d.sdf_tape.payload.capacity());
    check_job_pool(surface);
    CHECK(!d.stats.draw_memory_allocations && !d.stats.draw_resource_creations);
}

PUI_TEST(fine_jobs_are_contiguous_and_stable_across_frames_and_rebuilds)
{
    vulkan_device_opts options;
    options.whole_program_cache = false;
    locality_scene scene(options);
    if (!scene.win) return;
    auto &surface = *static_cast<vk_detail::surface *>(scene.surface);
    check_job_pool(surface);
    std::vector<vk_detail::sdf_gpu_job *> pools;
    for (auto &slot : surface.slots) pools.push_back(slot.sdf_resources->job_storage);
    const u64 created = scene.native->stats.sdf_job_creations;
    CHECK(created == surface.slots.size() * vk_detail::sdf_slot_resources::MAX_JOBS);
    for (u32 frame = 0; frame < 8; ++frame)
    {
        scene.frame(frame);
        check_job_pool(surface);
        for (usize slot = 0; slot < surface.slots.size(); ++slot)
            CHECK(surface.slots[slot].sdf_resources->job_storage == pools[slot]);
        CHECK(scene.native->stats.sdf_job_creations == created);
    }
    vulkan_set_cache(scene.base, false);
    scene.frame(8);
    check_job_pool(surface);
    vulkan_set_cache(scene.base, true);
    scene.frame(9);
    check_job_pool(surface);
    CHECK(vulkan_wait_idle(scene.base));
    scene.release_surface();
    CHECK(scene.native->stats.gpu_sdf_cache_image_bytes == scene.initial_cache_images);
    CHECK(!scene.native->stats.validation_errors);
}

PUI_TEST(rejected_fine_budget_creates_no_job_pool_and_preserves_pixels)
{
    vulkan_device_opts options;
    options.whole_program_cache = false;
    options.gpu_command_budget_bytes = 8192;
    locality_scene scene(options);
    if (!scene.win) return;
    auto &surface = *static_cast<vk_detail::surface *>(scene.surface);
    for (auto &slot : surface.slots) CHECK(!slot.sdf_resources);
    CHECK(!scene.native->stats.sdf_job_creations);
    for (u32 frame = 0; frame < 4; ++frame) scene.frame(frame);
}

#include "test_vulkan_function_admission.inl"

int main(int argc, char **argv)
{
    if (argc == 2 && !std::strcmp(argv[1], "--staged"))
        locality_staged = true;
    else if (argc != 1)
        return 1;
    for (const auto &test : test_registry()) test.fn();
    if (!g_failures) std::puts("all Vulkan allocation/locality regressions passed");
    return g_failures ? 1 : 0;
}
