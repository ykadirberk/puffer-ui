// Independent all-frame Kawase references; legacy goldens remain immutable.
#define PUFFERUI_COMPUTE_SPIRAL_HELPERS_ONLY
#include "test_vulkan_compute_spiral.cpp"
#include "golden/vulkan_kawase_rx9070xt.inl"

int main(int argc, char **argv)
{
    const bool staged = argc == 2 && std::string_view(argv[1]) == "--staged";
    if (argc > 1 && !staged) return 1;
    g_start_page = PAGE_LAYOUT;
    g_no_hud = true;
    vulkan_device_opts options;
    options.painter = vulkan_painter_mode::SDF_RASTER;
    options.vector_text = true;
    options.blur = vulkan_blur_mode::KAWASE;
    options.validation = true;
    options.error_callback = diagnostic;
    if (staged) options.upload_mode = vulkan_upload_mode::STAGED;
    render_device *devices[2] = {create_vulkan_device(options), nullptr};
    if (!devices[0]) return 1;
    const auto native = vulkan_handles(devices[0]);
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(native.physical_device, &properties);
    const bool qualified = properties.vendorID == 4098 && properties.deviceID == 30032 &&
                           properties.driverVersion == 8389024;
    options.native = &native;
    options.painter = vulkan_painter_mode::COMPUTE_SDF;
    options.tile_cache = true;
    devices[1] = create_vulkan_device(options);
    if (!devices[1])
    {
        destroy_vulkan_device(devices[0]);
        return 1;
    }
    example_app apps[2];
    showcase states[2];
    std::vector<u8> pixels[2];
    for (i32 variant = 0; variant < 2; ++variant)
    {
        pixels[variant].resize(PIXEL_BYTES);
        if (!initialize(apps[variant], devices[variant], 5000)) return 1;
        std::swap(g_app, states[variant]);
    }
    std::puts("frame,fnv1a64");
    for (i32 frame = 0; frame < FRAMES; ++frame)
    {
        for (i32 variant = 0; variant < 2; ++variant)
        {
            std::swap(g_app, states[variant]);
            if (!render(apps[variant], frame, pixels[variant])) return 1;
            std::swap(g_app, states[variant]);
        }
        if (pixels[0] != pixels[1])
        {
            std::fprintf(stderr, "Kawase raster/cache mismatch frame=%d\n", frame);
            return 1;
        }
        u64 hash = 14695981039346656037ull;
        for (const auto byte : pixels[0]) hash = (hash ^ byte) * 1099511628211ull;
        if (qualified && hash != kawase_spiral_goldens[frame])
        {
            std::fprintf(stderr, "Kawase immutable golden mismatch frame=%d\n", frame);
            return 1;
        }
        std::printf("%d,%016llx\n", frame, hash);
        if ((frame == 0 || frame == 90 || frame == 120 || frame == 127 || frame == 150 ||
             frame == 179) &&
            fs::is_directory("build-vulkan-resources/kawase-captures"))
            if (!fs::exists(frame_path("build-vulkan-resources/kawase-captures", frame)) &&
                !write_pixels(frame_path("build-vulkan-resources/kawase-captures", frame),
                              pixels[0]))
                return 1;
    }
    for (i32 variant = 1; variant >= 0; --variant)
    {
        if (!vulkan_wait_idle(devices[variant])) return 1;
        const auto stats = vulkan_statistics(devices[variant]);
        if (!stats.kawase_passes || stats.validation_errors || diagnostic_errors) return 1;
        if (variant && (!stats.gpu_sdf_batches || !stats.gpu_sdf_pixels)) return 1;
        destroy_context(apps[variant].ctx);
        devices[variant]->destroy_surface(apps[variant].surface);
        destroy_vulkan_device(devices[variant]);
    }
    std::fprintf(stderr, "all 180 Kawase 5000-item raster/cache frames matched (%s)\n",
                 staged ? "staged" : "direct");
    return 0;
}
