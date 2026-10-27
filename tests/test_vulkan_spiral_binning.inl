#include "vulkan_test_options.h"
// Actual 5000-item showcase tape through the native GPU binner, including peaks.
// The unchanged raster path paints; G2 validates preprocessing independently.
#undef near
#undef far
#undef small
#define PUFFERUI_COMPUTE_SPIRAL_HELPERS_ONLY
#include "test_vulkan_compute_spiral.cpp"
#undef PUFFERUI_COMPUTE_SPIRAL_HELPERS_ONLY

int main(int argc, char **argv)
{
    fs::path reference;
    if (argc == 3 && std::strcmp(argv[1], "--reference") == 0)
        reference = argv[2];
    else if (argc != 1)
        return 1;
    g_start_page = PAGE_LAYOUT;
    g_no_hud = true;
    vulkan_device_opts options = test_detail::reference_options();
    options.validation = true;
    options.painter = vulkan_painter_mode::COMMAND_SHADOW;
    options.error_callback = diagnostic;
    auto *device = create_vulkan_device(options);
    auto *native = dynamic_cast<vk_detail::device *>(device);
    if (!native) return 1;
    example_app app;
    if (!initialize(app, device, 5000)) return 1;
    std::vector<u8> pixels(PIXEL_BYTES), expected(PIXEL_BYTES);
    u32 checked = 0;
    for (i32 frame = 0; frame < FRAMES; ++frame)
    {
        if (!render(app, frame, pixels)) return 1;
        if (!reference.empty() &&
            (!read_pixels(frame_path(reference, frame), expected) || pixels != expected))
        {
            std::fprintf(stderr, "frozen Spiral reference mismatch at frame %d\n", frame);
            return 1;
        }
        if (frame != 0 && frame != 90 && frame != 120 && frame != 127 && frame != 150 &&
            frame != 179)
            continue;
        const auto &tape = native->primitive_commands.tape;
        u64 family_commands[8]{}, family_vertices[8]{};
        for (const auto &command : tape.commands)
        {
            if (command.operation >= 8 || command.payload_size < 128) return 1;
            u32 vertices;
            std::memcpy(&vertices, tape.payload.data() + command.payload_offset, sizeof(vertices));
            ++family_commands[command.operation];
            family_vertices[command.operation] += vertices;
        }
        std::printf(
            "frame=%d legacy=%llu/%llu rect=%llu/%llu rounded=%llu/%llu (commands/vertices)\n",
            frame, static_cast<unsigned long long>(family_commands[0]),
            static_cast<unsigned long long>(family_vertices[0]),
            static_cast<unsigned long long>(family_commands[1]),
            static_cast<unsigned long long>(family_vertices[1]),
            static_cast<unsigned long long>(family_commands[2]),
            static_cast<unsigned long long>(family_vertices[2]));
        paint_detail::tile_lists oracle, actual;
        constexpr u32 CAPACITY = 4 * 1024 * 1024;
        if (!tape.valid || !paint_detail::bin_reference(tape, WIDTH, HEIGHT, 32, CAPACITY, oracle))
            return 1;
        vk_detail::gpu_bin_job job;
        bool overflow = false;
        if (!job.initialize(*native, tape, WIDTH, HEIGHT, 32, 256, CAPACITY) || !job.record() ||
            !job.readback(actual, overflow) || overflow || oracle.offsets != actual.offsets ||
            oracle.indices != actual.indices)
        {
            std::fprintf(stderr, "actual Spiral GPU binning mismatch at frame %d\n", frame);
            return 1;
        }
        std::printf("frame=%d commands=%zu tile_indices=%zu exact ordered match\n", frame,
                    tape.commands.size(), actual.indices.size());
        ++checked;
    }
    if (checked != 6 || g_app.tiles.size() != 5003 || diagnostic_errors ||
        native->stats.validation_errors || native->stats.shadow_overflows)
        return 1;
    destroy_context(app.ctx);
    device->destroy_surface(app.surface);
    if (!vulkan_wait_idle(device)) return 1;
    destroy_vulkan_device(device);
    std::puts("all actual 5000-item Spiral GPU binning/peak regressions passed");
    return 0;
}
