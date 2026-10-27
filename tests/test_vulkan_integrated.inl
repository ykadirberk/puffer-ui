#include "vulkan_test_options.h"
// Execute the actual immutable Spiral fixture through the integrated float
// painter and the independent native SDF oracle. No existing test is modified.
#undef near
#undef far
#undef small
#define PUFFERUI_COMPUTE_SPIRAL_HELPERS_ONLY
#include "test_vulkan_compute_spiral.cpp"
#undef PUFFERUI_COMPUTE_SPIRAL_HELPERS_ONLY
#define REQUIRE(x)                                                                                 \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            std::fprintf(stderr, "FAIL line%d %s\n", __LINE__, #x);                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
int main(int argc, char **argv)
{
    bool cache = false, staged = false, bitmap = false, low_budget = false, production = false;
    i32 tiles = 5000;
    i32 frames = FRAMES;
    bool long_run = false;
    for (i32 i = 1; i < argc; ++i)
        if (!std::strcmp(argv[i], "--cache"))
            cache = true;
        else if (!std::strcmp(argv[i], "--staged"))
            staged = true;
        else if (!std::strcmp(argv[i], "--bitmap"))
            bitmap = true;
        else if (!std::strcmp(argv[i], "--low-budget"))
            low_budget = true;
        else if (!std::strcmp(argv[i], "--production"))
            production = true;
        else if (!std::strcmp(argv[i], "--long-run"))
        {
            frames = 3600;
            first_add = 30;
            add_period = 60;
            long_run = true;
        }
        else if (!std::strcmp(argv[i], "--tiles") && i + 1 < argc)
            tiles = std::atoi(argv[++i]);
        else
            return 1;
    REQUIRE(tiles == 1150 || tiles == 5000);
    g_start_page = PAGE_LAYOUT;
    g_no_hud = true;
    vulkan_device_opts options = test_detail::reference_options();
    options.painter = vulkan_painter_mode::INTEGRATED;
    options.validation = true;
    options.error_callback = diagnostic;
    options.tile_cache = cache;
    options.whole_program_cache = production;
    options.upload_mode = staged ? vulkan_upload_mode::STAGED : vulkan_upload_mode::DIRECT;
#if defined(PUFFERUI_ENABLE_HARFBUZZ)
    options.vector_text = !bitmap;
#endif
    if (low_budget) options.cache_budget_bytes = 1;
    render_device *devices[2] = {create_vulkan_device(options), nullptr};
    REQUIRE(devices[0]);
    const auto native = vulkan_handles(devices[0]);
    options.native = &native;
    options.whole_program_cache = false;
    options.tile_cache = false;
    options.painter = vulkan_painter_mode::SDF_RASTER;
    devices[1] = create_vulkan_device(options);
    REQUIRE(devices[1]);
    auto &d = *static_cast<vk_detail::device *>(devices[0]);
    REQUIRE(!d.sdf_blend); // the old 17 MiB startup calibration is not constructed
    if (production)
        REQUIRE(!d.sdf_functions); // automatic observation needs no fine-cache kernels
    else
        REQUIRE(low_budget ? !d.sdf_functions->valid : d.sdf_functions->valid);
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(native.physical_device, &properties);
    const bool immutable_domain = !long_run && options.vector_text && properties.vendorID == 4098 &&
                                  properties.deviceID == 30032 &&
                                  properties.driverVersion == 8389024;
    example_app apps[2];
    showcase states[2];
    std::vector<u8> pixels[2];
    for (i32 v = 0; v < 2; ++v)
    {
        pixels[v].resize(PIXEL_BYTES);
        REQUIRE(initialize(apps[v], devices[v], tiles));
        std::swap(g_app, states[v]);
    }
    u64 immutable_frames = 0;
    for (i32 f = 0; f < frames; ++f)
    {
        for (i32 v = 0; v < 2; ++v)
        {
            set_current_context(apps[v].ctx);
            std::swap(g_app, states[v]);
            REQUIRE(render(apps[v], f, pixels[v]));
            std::swap(g_app, states[v]);
        }
        if (pixels[0] != pixels[1])
        {
            usize first = 0;
            while (pixels[0][first] == pixels[1][first]) ++first;
            std::fprintf(
                stderr,
                "integrated frame=%d mismatch pixel=(%zu,%zu) channel=%zu actual=%u native=%u\n", f,
                (first / 4) % WIDTH, (first / 4) / WIDTH, first % 4, pixels[0][first],
                pixels[1][first]);
            return 1;
        }
        if (immutable_domain)
        {
            u64 hash = 14695981039346656037ull;
            for (const auto byte : pixels[0]) hash = (hash ^ byte) * 1099511628211ull;
            REQUIRE(hash == painter_goldens[1][tiles == 5000 ? 1 : 0][f]);
            ++immutable_frames;
        }
        if (long_run)
        {
            u64 hash = 14695981039346656037ull;
            for (const auto byte : pixels[0]) hash = (hash ^ byte) * 1099511628211ull;
            std::fprintf(stderr, "long_frame=%d tiles=%zu rgba_fnv64=%016llx\n", f,
                         states[0].tiles.size(), hash);
        }
    }
    for (i32 v = 0; v < 2; ++v)
    {
        const i32 additions = frames <= first_add ? 0 : 1 + (frames - 1 - first_add) / add_period;
        REQUIRE(states[v].tiles.size() == static_cast<usize>(tiles + additions));
        REQUIRE(vulkan_wait_idle(devices[v]));
    }
    const auto stats = vulkan_statistics(devices[0]);
    REQUIRE(!stats.validation_errors && !diagnostic_errors && !stats.shadow_overflows);
    REQUIRE(!stats.draw_memory_allocations && !stats.draw_resource_creations);
    // Forced tile cases prove the interpreter. Production observes changing
    // programs natively and uses a whole-image certificate before fine reuse;
    // test_vulkan_identity's feedback cases require the automatic compute path.
    if (!low_budget && !production) REQUIRE(stats.gpu_sdf_batches && stats.gpu_sdf_pixels);
    std::fprintf(
        stderr,
        "integrated_spiral frames=%d tiles=%d immutable_frames=%llu cache=%u staged=%u "
        "gpu_pixels=%llu aa_pixels=%llu glyph_pixels=%llu float_pixels=%llu function_misses=%llu "
        "metadata_peak=%llu image_bytes=%llu draw_allocations=%llu validation_errors=%llu\n",
        frames, tiles, immutable_frames, cache, staged, stats.gpu_sdf_pixels,
        d.integrated_counts[0], d.integrated_counts[1], d.integrated_counts[3],
        d.integrated_counts[2], stats.peak_gpu_metadata_allocated_bytes, stats.image_bytes,
        stats.draw_memory_allocations, stats.validation_errors);
    for (i32 v = 0; v < 2; ++v)
    {
        destroy_context(apps[v].ctx);
        devices[v]->destroy_surface(apps[v].surface);
    }
    destroy_vulkan_device(devices[1]);
    destroy_vulkan_device(devices[0]);
    std::printf("all %d integrated Spiral frames matched native SDF exactly\n", frames);
    return 0;
}
