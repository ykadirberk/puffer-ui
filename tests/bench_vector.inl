// Readback-free vector representation comparison. Setup/cold/outlier frames stay visible.
#include <chrono>
static int vector_benchmark(int argc, char **argv)
{
    using namespace vk_detail;
    bool portable = false, staged = false, complex = false;
    for (int i = 2; i < argc; ++i)
        if (std::string_view(argv[i]) == "--portable")
            portable = true;
        else if (std::string_view(argv[i]) == "--staged")
            staged = true;
        else if (std::string_view(argv[i]) == "--complex")
            complex = true;
        else
            return 2;
    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    auto *base = create_vulkan_device(
        {.vector_text = false,
         .upload_mode = staged ? vulkan_upload_mode::STAGED : vulkan_upload_mode::DIRECT,
         .vector_paths = !portable});
    if (!base) return 1;
    auto &dev = *static_cast<device *>(base);
    auto *surface = create_vulkan_surface(base, {.width = 512, .height = 512});
    if (!surface)
    {
        destroy_vulkan_device(base);
        return 1;
    }
    auto *ctx = create_context(base, surface);
    auto *win = add_window(ctx, nullptr, surface, {0, 0, 512, 512});
    ui u(ctx);
    auto source = load_svg(
        complex
            ? R"svg(<svg viewBox="0 0 64 64"><defs><linearGradient id="g" x2="1" y2="1"><stop stop-color="currentColor"/><stop offset="1" stop-color="#7562db"/></linearGradient><clipPath id="c"><circle cx="32" cy="32" r="28"/></clipPath></defs><circle cx="32" cy="32" r="28" fill="url(#g)"/><g clip-path="url(#c)" opacity=".7"><path d="M0 44Q16 20 32 44T64 44V64H0Z" fill="white"/><circle cx="32" cy="32" r="12" fill="#192838" fill-opacity=".5"/></g></svg>)svg"
            : R"(<svg viewBox="0 0 64 64"><path d="M4 32C4 4 60 4 60 32S4 60 4 32Z" fill="currentColor"/></svg>)");
    bool okay = static_cast<bool>(source);
    const usize instances = complex ? 96 : 5000;
    constexpr color palette[] = {{82, 191, 255, 255}, {248, 128, 132, 255}};
    for (auto current : palette)
        okay = u.prepare_vector(source.image, {.max_pixel_size = {64, 64},
                                               .max_instances = instances,
                                               .resource_budget_bytes = 32 * 1024 * 1024,
                                               .style = {.current_color = current}}) &&
               okay;
    const geometry_capacity capacity{instances * 4 + 64, instances * 6 + 96};
    okay = u.prepare_geometry(capacity) && okay;
    okay = vulkan_wait_idle(base) && okay;
    okay = vulkan_prepare_surface_geometry(surface, capacity) && okay;
    if (!okay)
    {
        destroy_vector(source.image);
        destroy_context(ctx);
        base->destroy_surface(surface);
        destroy_vulkan_device(base);
        return 1;
    }
    const auto initial = vulkan_statistics(base);
    const f64 setup = std::chrono::duration<f64, std::milli>(clock::now() - start).count();
    struct sample
    {
        f64 ms;
        usize allocations;
        vulkan_stats before, after;
    };
    sample samples[180]{};
    for (usize frame = 0; frame < 180; ++frame)
    {
        const auto t = clock::now();
        samples[frame].before = vulkan_statistics(base);
        begin_frame(ctx, *win, frame / 60.0, 1.0 / 60);
        const usize allocations = vector_allocation_count;
        vector_count_allocations = true;
        const i32 columns = complex ? 12 : 71;
        const f32 cell = complex ? 42.0f : 7.0f, size = complex ? 38.0f : 6.0f;
        const f32 angle = .12f * std::sin(static_cast<f32>(frame) * .03f), c = std::cos(angle),
                  s = std::sin(angle);
        for (usize i = 0; i < instances; ++i)
        {
            const rect area{static_cast<f32>(i % columns) * cell + 2,
                            static_cast<f32>(i / columns) * cell + 2, size, size};
            const vec2 center{area.x + size * .5f, area.y + size * .5f};
            const affine2 transform{c,
                                    s,
                                    -s,
                                    c,
                                    center.x - c * center.x + s * center.y,
                                    center.y - s * center.x - c * center.y};
            okay = u.draw_vector(area, source.image,
                                 {.transform = transform,
                                  .style = {.current_color = palette[(frame / 30) % 2],
                                            .opacity = complex ? .8f : 1.0f}}) &&
                   okay;
        }
        ctx->painter->flush();
        vector_count_allocations = false;
        samples[frame].allocations = vector_allocation_count - allocations;
        end_frame(ctx);
        samples[frame].after = vulkan_statistics(base);
        samples[frame].ms = std::chrono::duration<f64, std::milli>(clock::now() - t).count();
    }
    okay = vulkan_wait_idle(base) && okay;
    const auto final = vulkan_statistics(base);
    const auto *painter = static_cast<native_painter *>(ctx->painter);
    const usize residency = portable ? (painter->vector_images ? painter->vector_images->bytes : 0)
                                     : (dev.paths ? dev.paths->usage() : 0);
    std::puts(
        "frame,total_ms,draw_cpp_allocations,draws,upload_bytes,device_local_upload_bytes,draw_"
        "buffer_allocations,total_upload_bytes");
    for (usize frame = 0; frame < 180; ++frame)
    {
        const auto &v = samples[frame];
        std::printf("%zu,%.9f,%zu,%llu,%llu,%llu,%llu,%llu\n", frame, v.ms, v.allocations,
                    static_cast<unsigned long long>(v.after.draws - v.before.draws),
                    static_cast<unsigned long long>(v.after.upload_bytes - v.before.upload_bytes),
                    static_cast<unsigned long long>(v.after.device_upload_allocated_bytes),
                    static_cast<unsigned long long>(v.after.draw_buffer_allocations -
                                                    v.before.draw_buffer_allocations),
                    static_cast<unsigned long long>(v.after.upload_allocated_bytes));
    }
    std::fprintf(
        stderr,
        "vector_bench scene=%s representation=%s instances=%zu setup_ms=%.9f gpu_total_ms=%.9f "
        "gpu_samples=%llu path_residency_bytes=%zu image_bytes=%llu readbacks=%llu\n",
        complex ? "complex" : "simple", portable ? "portable" : "native", instances, setup,
        final.completed_gpu_ms - initial.completed_gpu_ms,
        static_cast<unsigned long long>(final.gpu_samples - initial.gpu_samples), residency,
        static_cast<unsigned long long>(final.image_bytes),
        static_cast<unsigned long long>(final.readbacks - initial.readbacks));
    okay = !final.validation_errors && vulkan_last_result(base) == VK_SUCCESS && okay;
    auto handle = source.image;
    (void)u.release_vector(handle);
    destroy_vector(handle);
    destroy_context(ctx);
    base->destroy_surface(surface);
    destroy_vulkan_device(base);
    return okay ? 0 : 1;
}
