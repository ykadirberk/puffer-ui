// Actual Vulkan compute results must match the independent ordered CPU oracle.
// GPU readback is test-only; rendering consumes these buffers without readback.
#undef near
#undef far
#undef small
#include "test_util.h"

namespace
{
vk_detail::device *gpu_device = nullptr;
i32 gpu_errors = 0;
void gpu_diagnostic(i32 code, const char *message, void *)
{
    std::fprintf(stderr, "Vulkan %d: %s\n", code, message);
    ++gpu_errors;
}
void compare_gpu(const paint_detail::command_tape &tape, u32 width, u32 height, u32 tile,
                 u32 coarse, u32 capacity, bool expected_overflow)
{
    paint_detail::tile_lists oracle, actual;
    const bool cpu_ok = paint_detail::bin_reference(tape, width, height, tile, capacity, oracle);
    vk_detail::gpu_bin_job job;
    const bool initialized =
        job.initialize(*gpu_device, tape, width, height, tile, coarse, capacity);
    CHECK(initialized);
    if (!initialized) return;
    CHECK(job.record());
    bool overflow = false;
    CHECK(job.readback(actual, overflow));
    CHECK(overflow == expected_overflow && cpu_ok != overflow);
    CHECK(actual.offsets == oracle.offsets && actual.indices == oracle.indices);
    job.release();
    CHECK(gpu_device->stats.gpu_metadata_allocated_bytes == 0);
}
} // namespace

PUI_TEST(gpu_binning_preserves_order_partial_tiles_and_bitset_word_boundaries)
{
    using namespace paint_detail;
    for (u32 count : {0u, 1u, 31u, 32u, 33u, 511u, 5000u})
    {
        command_tape tape;
        CHECK(tape.prepare(count, 0, static_cast<u64>(count) * sizeof(command)));
        u32 random = 0x9e3779b9u;
        for (u32 i = 0; i < count; ++i)
        {
            auto next = [&]()
            {
                random = random * 1664525u + 1013904223u;
                return random;
            };
            command c;
            c.x0 = static_cast<f32>(static_cast<i32>(next() % 210) - 35) + 0.25f;
            c.y0 = static_cast<f32>(static_cast<i32>(next() % 160) - 35) - 0.5f;
            c.x1 = c.x0 + static_cast<f32>(next() % 60);
            c.y1 = c.y0 + static_cast<f32>(next() % 60);
            CHECK(tape.append(c, {}));
        }
        compare_gpu(tape, 137, 91, 16, 64, count * 54, false);
    }
}
PUI_TEST(gpu_binning_overflow_cannot_publish_truncated_lists)
{
    using namespace paint_detail;
    command_tape tape;
    CHECK(tape.prepare(5000, 0, 5000 * sizeof(command)));
    command c;
    c.x0 = c.y0 = -1;
    c.x1 = c.y1 = 129;
    for (u32 i = 0; i < 5000; ++i) CHECK(tape.append(c, {}));
    compare_gpu(tape, 128, 128, 32, 64, 320000, false);
    compare_gpu(tape, 128, 128, 32, 64, 64, true);
    compare_gpu(tape, 128, 128, 32, 64, 0, true);
}
PUI_TEST(gpu_binning_budget_and_invalid_grid_rejection_leave_device_usable)
{
    paint_detail::command_tape tape;
    CHECK(tape.prepare(0, 0, 0));
    {
        vk_detail::gpu_bin_job invalid;
        CHECK(!invalid.initialize(*gpu_device, tape, 64, 64, 32, 48, 0));
    }
    const u64 budget = gpu_device->options.gpu_command_budget_bytes;
    gpu_device->options.gpu_command_budget_bytes = 0;
    {
        vk_detail::gpu_bin_job denied;
        CHECK(!denied.initialize(*gpu_device, tape, 64, 64, 32, 64, 0));
    }
    CHECK(gpu_device->stats.gpu_metadata_allocated_bytes == 0 && gpu_device->result == VK_SUCCESS);
    gpu_device->options.gpu_command_budget_bytes = budget;
    compare_gpu(tape, 64, 64, 32, 64, 0, false);
}
int main()
{
    vulkan_device_opts options;
    options.validation = true;
    options.error_callback = gpu_diagnostic;
    auto *device = create_vulkan_device(options);
    gpu_device = dynamic_cast<vk_detail::device *>(device);
    if (!gpu_device) return 1;
    for (const auto &test : test_registry()) test.fn();
    if (!vulkan_wait_idle(device) || gpu_errors || vulkan_statistics(device).validation_errors)
        ++g_failures;
    destroy_vulkan_device(device);
    if (!g_failures)
        std::printf("all GPU binning regressions passed (%zu tests)\n", test_registry().size());
    return g_failures ? 1 : 0;
}
