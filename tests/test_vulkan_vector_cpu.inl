// New work-count and merge-boundary regressions; original suites stay unchanged.
namespace vector_cpu_test
{
struct fixture
{
    render_device *base = nullptr;
    render_surface *surface = nullptr;
    context *ctx = nullptr;
    window *win = nullptr;
    vector_handle image = nullptr, unprepared = nullptr;
    ~fixture()
    {
        destroy_vector(image);
        destroy_vector(unprepared);
        if (ctx) destroy_context(ctx);
        if (surface) base->destroy_surface(surface);
        destroy_vulkan_device(base);
    }
    bool open(bool staged)
    {
        base = create_vulkan_device(
            {.validation = true,
             .vector_text = false,
             .upload_mode = staged ? vulkan_upload_mode::STAGED : vulkan_upload_mode::DIRECT});
        if (!base) return false;
        surface = create_vulkan_surface(base, {.width = 512, .height = 512});
        if (!surface) return false;
        ctx = create_context(base, surface);
        win = add_window(ctx, nullptr, surface, {0, 0, 512, 512});
        auto source = load_svg("<svg viewBox='0 0 64 64'><path d='M4 32C4 4 60 4 60 32S4 60 4 "
                               "32Z' fill='currentColor'/></svg>");
        image = source.image;
        auto other = load_svg("<svg viewBox='0 0 64 64'><circle cx='32' cy='32' r='24'/></svg>");
        unprepared = other.image;
        return image && unprepared &&
               ui(ctx).prepare_vector(image, {.max_pixel_size = {64, 64}, .max_instances = 5000}) &&
               vulkan_wait_idle(base);
    }
};
bool placement(bool staged)
{
    const i32 previous_failures = g_failures;
    fixture env;
    if (!env.open(staged)) return false;
    ui u(env.ctx);
    using renderer_detail::vector_placement_calls;
    vector_placement_calls = 0;
    CHECK(u.draw_vector({0, 0, 6, 6}, nullptr).error.code == vector_error_code::WRONG_CONTEXT);
    CHECK(vector_placement_calls == 0);
    begin_frame(env.ctx, *env.win, 0, 1.0 / 60);
    const auto initial = vulkan_statistics(env.base);
    const usize allocations = vector_allocation_count;
    vector_placement_calls = 0;
    vector_count_allocations = true;
    for (usize i = 0; i < 5000; ++i)
        CHECK(u.draw_vector(
            {static_cast<f32>(i % 71) * 7 + 2, static_cast<f32>(i / 71) * 7 + 2, 6, 6}, env.image));
    env.ctx->painter->flush();
    vector_count_allocations = false;
    CHECK(vector_placement_calls == 5000);
    CHECK(vector_allocation_count == allocations);
    const auto drawn = vulkan_statistics(env.base);
    CHECK(drawn.draws - initial.draws == 1);
    CHECK(drawn.upload_bytes - initial.upload_bytes == 5000 * sizeof(vk_detail::vector_instance));
    CHECK(drawn.draw_buffer_allocations == initial.draw_buffer_allocations);

    const auto check =
        [&](rect area, vector_handle source, vector_draw_opts options, vector_error_code error)
    {
        vector_placement_calls = 0;
        const auto status = u.draw_vector(area, source, options);
        CHECK(status.error.code == error);
        CHECK(vector_placement_calls == 1);
        if (error == vector_error_code::NOT_PREPARED)
            CHECK(!std::strcmp(status.error.message, "native vector extent is not prepared"));
    };
    vector_placement_calls = 0;
    CHECK(env.ctx->painter->vector_ready({{0, 0, 6, 6}, env.image}));
    CHECK(vector_placement_calls == 1);
    check({0, 0, 6, 6}, env.image, {}, vector_error_code::NONE);
    check({0, 0, 6, 6}, env.unprepared, {}, vector_error_code::NOT_PREPARED);
    check({0, 0, 128, 128}, env.image, {}, vector_error_code::NOT_PREPARED);
    check({0, 0, 6, 6}, nullptr, {}, vector_error_code::INVALID_PATH);
    check({0, 0, -1, 6}, env.image, {}, vector_error_code::INVALID_PATH);
    check({0, 0, 6, 6}, env.image,
          {.transform = {1, 0, 0, 1, std::numeric_limits<f32>::quiet_NaN(), 0}},
          vector_error_code::INVALID_PATH);
    check({0, 0, 6, 6}, env.image, {.style = {.opacity = 2}}, vector_error_code::INVALID_PATH);
    check({0, 0, 0, 6}, env.unprepared, {}, vector_error_code::NONE);
    check({0, 0, 6, 6}, env.unprepared, {.style = {.opacity = 0}}, vector_error_code::NONE);
    check({0, 0, 6, 6}, env.image, {.transform = {0, 0, 0, 0, 0, 0}}, vector_error_code::NONE);
    check({0, 0, 6, 6}, env.image, {.transform = {-1, 0, 0, 1, 20, 0}}, vector_error_code::NONE);
    {
        auto clip = u.region({0, 0, 16, 16}, "vector_cpu_clip"_id);
        check({32, 32, 6, 6}, env.unprepared, {}, vector_error_code::NONE);
    }
    check({32, 32, 6, 6}, env.image, {}, vector_error_code::NONE);
    auto *surface = static_cast<vk_detail::surface *>(env.surface);
    surface->suspended = true;
    check({0, 0, 6, 6}, env.image, {}, vector_error_code::NONE);
    check({0, 0, 6, 6}, env.unprepared, {}, vector_error_code::NOT_PREPARED);
    surface->suspended = false;
    end_frame(env.ctx);
    set_window_scale(*env.win, 2);
    begin_frame(env.ctx, *env.win, 1.0 / 60, 1.0 / 60);
    check({0, 0, 32, 32}, env.image, {}, vector_error_code::NONE);
    check({0, 0, 64, 64}, env.image, {}, vector_error_code::NOT_PREPARED);
    end_frame(env.ctx);
    CHECK(u.release_vector(env.image));
    begin_frame(env.ctx, *env.win, 2.0 / 60, 1.0 / 60);
    check({0, 0, 6, 6}, env.image, {}, vector_error_code::NOT_PREPARED);
    end_frame(env.ctx);
    CHECK(vulkan_wait_idle(env.base));
    CHECK(!violation_count(env.ctx));
    CHECK(!vulkan_statistics(env.base).validation_errors);
    CHECK(vulkan_last_result(env.base) == VK_SUCCESS);
    return g_failures == previous_failures;
}

vk_detail::geometry_span scalar_span(const i32 *indices, i32 start, i32 count, VkDeviceSize stride,
                                     VkDeviceSize capacity)
{
    vk_detail::geometry_span result{start, 0x7fffffff, 0};
    while (result.end < count)
    {
        const i32 low =
            std::min(result.low, std::min(indices[result.end], std::min(indices[result.end + 1],
                                                                        indices[result.end + 2])));
        const i32 high =
            std::max(result.high, std::max(indices[result.end], std::max(indices[result.end + 1],
                                                                         indices[result.end + 2])));
        if ((static_cast<VkDeviceSize>(high) - low + 1) * stride +
                static_cast<VkDeviceSize>(result.end - start + 3) * 4 >
            capacity)
            break;
        result = {result.end + 3, low, high};
    }
    return result;
}
bool planner()
{
    const i32 previous_failures = g_failures;
    std::array<i32, 643> storage{};
    u64 cases = 0;
    // Place each extremum in every SIMD lane of the final unrolled group of
    // three consecutive blocks. Exercise exact fits, refinement and tails.
    for (i32 alignment = 0; alignment < 4; ++alignment)
        for (i32 block = 0; block < 3; ++block)
            for (i32 lane = 0; lane < 16; ++lane)
                for (const i32 extreme : {-2000, 0, 1000000, 0x7fffffff})
                {
                    i32 *indices = storage.data() + alignment;
                    std::fill_n(indices, 639, 407);
                    indices[block * 192 + 176 + lane] = extreme;
                    for (const i32 count : {189, 192, 195, 381, 384, 387, 639})
                        for (const i32 start : {0, 3, 15, 189, 192, 195})
                        {
                            if (start >= count) continue;
                            for (const VkDeviceSize stride : {20, 32})
                            {
                                const VkDeviceSize low = extreme < 407 ? extreme : 407;
                                const VkDeviceSize high = extreme > 407 ? extreme : 407;
                                const VkDeviceSize fit =
                                    (high - low + 1) * stride + (count - start) * 4;
                                for (const VkDeviceSize capacity :
                                     {VkDeviceSize(0), VkDeviceSize(71), VkDeviceSize(72),
                                      VkDeviceSize(4096), fit - 1, fit, fit + 1,
                                      VkDeviceSize(1) << 38})
                                {
                                    const auto expected =
                                        scalar_span(indices, start, count, stride, capacity);
                                    const auto actual = vk_detail::plan_geometry_span(
                                        indices, start, count, stride, capacity);
                                    const auto fallback = vk_detail::plan_geometry_span<false>(
                                        indices, start, count, stride, capacity);
                                    CHECK(actual.end == expected.end &&
                                          actual.low == expected.low &&
                                          actual.high == expected.high);
                                    CHECK(fallback.end == expected.end &&
                                          fallback.low == expected.low &&
                                          fallback.high == expected.high);
                                    ++cases;
                                }
                            }
                        }
                }
    std::vector<i32> dense(98313);
    for (usize i = 0; i < dense.size(); ++i) dense[i] = static_cast<i32>(i) + 407;
    const usize allocations = vector_allocation_count;
    vk_detail::index_work = {};
    vector_count_allocations = true;
    const auto span = vk_detail::plan_geometry_span(dense.data(), 0, static_cast<i32>(dense.size()),
                                                    20, 32 * 1024 * 1024);
    vector_count_allocations = false;
    CHECK(span.end == dense.size() && span.low == 407 && span.high == dense.back());
    CHECK(vector_allocation_count == allocations);
    if constexpr (vk_detail::INTEGER_SIMD)
        CHECK(vk_detail::index_work.planning_vectors == 98304 / 4 &&
              vk_detail::index_work.planning_scalars == 0);
    if (g_failures == previous_failures)
        std::printf("native vector CPU: %llu exact merge-boundary cases, SIMD=%d\n",
                    static_cast<unsigned long long>(cases), vk_detail::INTEGER_SIMD);
    return g_failures == previous_failures;
}
} // namespace vector_cpu_test
