// Independent scalar contracts and GPU guards for integer SIMD preparation.
// Existing tests and golden references remain unchanged.
#define main integer_blur_fixture_main
#include "test_vulkan_blur.cpp"
#undef main
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace integer_test
{
u64 validation_cases = 0, span_cases = 0;
bool scalar_valid(const i32 *indices, i32 count, i32 base, i32 vertices)
{
    for (i32 i = 0; i < count; ++i)
        if (indices[i] < base || static_cast<i64>(indices[i]) - base >= vertices) return false;
    return true;
}
vk_detail::geometry_span scalar_span(const i32 *indices, i32 start, i32 count, VkDeviceSize stride,
                                     VkDeviceSize capacity)
{
    vk_detail::geometry_span result{start, 0x7fffffff, 0};
    while (result.end < count)
    {
        const i32 lo =
            std::min(result.low, std::min(indices[result.end], std::min(indices[result.end + 1],
                                                                        indices[result.end + 2])));
        const i32 hi =
            std::max(result.high, std::max(indices[result.end], std::max(indices[result.end + 1],
                                                                         indices[result.end + 2])));
        if ((static_cast<VkDeviceSize>(hi) - lo + 1) * stride +
                static_cast<VkDeviceSize>(result.end - start + 3) * 4 >
            capacity)
            break;
        result = {result.end + 3, lo, hi};
    }
    return result;
}
bool validation_matrix()
{
    u32 random = 0x73829cab;
    const auto next = [&]()
    {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        return random;
    };
    constexpr i32 LOW = std::numeric_limits<i32>::min(), HIGH = std::numeric_limits<i32>::max();
    for (i32 alignment = 0; alignment < 4; ++alignment)
        for (const i32 base : {LOW, LOW + 1, -10000, -1, 0, 1, 407, 0x00fffff4, HIGH - 511, HIGH})
            for (const i32 vertices : {1, 511, HIGH})
                for (const i32 count :
                     {0, 1, 2, 3, 7, 15, 16, 17, 31, 32, 33, 191, 192, 193, 384, 1025})
                {
                    std::vector<i32> storage(static_cast<usize>(count) + 4);
                    i32 *indices = storage.data() + alignment;
                    const i64 delta = std::min<i64>(vertices - 1, static_cast<i64>(HIGH) - base);
                    for (i32 i = 0; i < count; ++i)
                        indices[i] =
                            static_cast<i32>(static_cast<i64>(base) + next() % (delta + 1));
                    const auto check = [&]()
                    {
                        ++validation_cases;
                        const bool expected = scalar_valid(indices, count, base, vertices);
                        return vk_detail::valid_geometry_indices(indices, count, base, vertices) ==
                                   expected &&
                               vk_detail::valid_geometry_indices<false>(indices, count, base,
                                                                        vertices) == expected;
                    };
                    CHECK(check());
                    for (i32 at = 0; at < count; ++at)
                    {
                        if (count > 193 && at != 0 && at != 15 && at != 16 && at != 191 &&
                            at != 192 && at != count - 1)
                            continue;
                        const i32 old = indices[at];
                        for (const i32 bad : {LOW, -1, 0, 1, base, HIGH})
                        {
                            indices[at] = bad;
                            CHECK(check());
                        }
                        indices[at] = old;
                    }
                }
    return true;
}
bool planner_matrix()
{
    u32 random = 0x51a7e29;
    for (i32 alignment = 0; alignment < 4; ++alignment)
        for (const i32 triangles : {1, 7, 8, 9, 15, 16, 17, 63, 64, 65, 127, 128, 129, 341})
            for (i32 pattern = 0; pattern < 8; ++pattern)
            {
                std::vector<i32> storage(static_cast<usize>(triangles) * 3 + 4);
                i32 *indices = storage.data() + alignment;
                for (i32 i = 0; i < triangles * 3; ++i)
                {
                    random ^= random << 13;
                    random ^= random >> 17;
                    random ^= random << 5;
                    switch (pattern)
                    {
                    case 0:
                        indices[i] = 407 + i;
                        break;
                    case 1:
                        indices[i] = 407 + (i % 3 ? i / 3 + i % 3 : 0);
                        break;
                    case 2:
                        indices[i] = 407 + triangles * 3 - i;
                        break;
                    case 3:
                        indices[i] = static_cast<i32>(random % 90000);
                        break;
                    case 4:
                        indices[i] = i % 3 == 1 ? 0x7fffffff : i % 13;
                        break;
                    case 5:
                        indices[i] = 0;
                        break;
                    case 6:
                        indices[i] = -4096 + i % 127;
                        break;
                    default:
                        indices[i] = i % 2 ? std::numeric_limits<i32>::min() : 0x7fffffff;
                        break;
                    }
                }
                for (const VkDeviceSize stride : {20, 32})
                    for (const VkDeviceSize capacity :
                         {VkDeviceSize(0), VkDeviceSize(3 * (stride + 4) - 1),
                          VkDeviceSize(3 * (stride + 4)), VkDeviceSize(3 * (stride + 4) + 1),
                          VkDeviceSize(511), VkDeviceSize(512), VkDeviceSize(513),
                          VkDeviceSize(4096), VkDeviceSize(1024 * 1024), VkDeviceSize(1) << 38})
                        for (i32 start = 0; start < triangles * 3;)
                        {
                            const auto expected =
                                scalar_span(indices, start, triangles * 3, stride, capacity);
                            const auto actual = vk_detail::plan_geometry_span(
                                indices, start, triangles * 3, stride, capacity);
                            const auto fallback = vk_detail::plan_geometry_span<false>(
                                indices, start, triangles * 3, stride, capacity);
                            ++span_cases;
                            CHECK(actual.end == expected.end && actual.low == expected.low &&
                                  actual.high == expected.high);
                            CHECK(fallback.end == expected.end && fallback.low == expected.low &&
                                  fallback.high == expected.high);
                            start = actual.end == start ? start + 3 : actual.end;
                        }
            }
    return true;
}
bool bounded_work()
{
    std::vector<i32> dense(98304);
    for (usize i = 0; i < dense.size(); ++i) dense[i] = static_cast<i32>(i) + 407;
    vk_detail::index_work = {};
    CHECK(vk_detail::valid_geometry_indices(dense.data(), static_cast<i32>(dense.size()), 407,
                                            static_cast<i32>(dense.size())));
    if constexpr (vk_detail::INTEGER_SIMD)
        CHECK(vk_detail::index_work.validation_vectors == dense.size() / 4 &&
              vk_detail::index_work.validation_scalars == 0);
    vk_detail::index_work = {};
    CHECK(vk_detail::valid_geometry_indices<false>(dense.data(), static_cast<i32>(dense.size()),
                                                   407, static_cast<i32>(dense.size())));
    CHECK(vk_detail::index_work.validation_vectors == 0 &&
          vk_detail::index_work.validation_scalars == dense.size());
    vk_detail::index_work = {};
    const auto span =
        vk_detail::plan_geometry_span(dense.data(), 0, static_cast<i32>(dense.size()), 20, 1 << 28);
    CHECK(span.end == static_cast<i32>(dense.size()));
    if constexpr (vk_detail::INTEGER_SIMD)
        CHECK(vk_detail::index_work.planning_vectors == dense.size() / 4 &&
              vk_detail::index_work.planning_scalars == 0);
    dense[0] = 0;
    dense[1] = 0x7fffffff;
    dense[2] = 1;
    vk_detail::index_work = {};
    CHECK(vk_detail::plan_geometry_span(dense.data(), 0, static_cast<i32>(dense.size()), 20, 512)
              .end == 0);
    CHECK(vk_detail::index_work.planning_vectors == 0 &&
          vk_detail::index_work.planning_scalars == 0);
    return true;
}
struct guarded_page
{
    void *memory = nullptr;
    usize page = 0;
    bool initialize()
    {
#if defined(_WIN32)
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        page = info.dwPageSize;
        memory = VirtualAlloc(nullptr, page * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        DWORD old = 0;
        return memory &&
               VirtualProtect(static_cast<u8 *>(memory) + page, page, PAGE_NOACCESS, &old);
#else
        page = static_cast<usize>(sysconf(_SC_PAGESIZE));
        memory =
            mmap(nullptr, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (memory == MAP_FAILED)
        {
            memory = nullptr;
            return false;
        }
        return mprotect(static_cast<u8 *>(memory) + page, page, PROT_NONE) == 0;
#endif
    }
    ~guarded_page()
    {
        if (!memory) return;
#if defined(_WIN32)
        VirtualFree(memory, 0, MEM_RELEASE);
#else
        munmap(memory, page * 2);
#endif
    }
};
bool guard_tails()
{
    guarded_page allocation;
    CHECK(allocation.initialize());
    for (const i32 count : {0,   1,   2,   3,   15,  16,  17,  23,  24,  25, 189,
                            190, 191, 192, 193, 194, 195, 383, 384, 385, 387})
    {
        auto *indices =
            reinterpret_cast<i32 *>(static_cast<u8 *>(allocation.memory) + allocation.page) - count;
        for (i32 i = 0; i < count; ++i) indices[i] = 407 + i % 64;
        CHECK(vk_detail::valid_geometry_indices(indices, count, 407, 64));
        CHECK(vk_detail::valid_geometry_indices<false>(indices, count, 407, 64));
        if (count % 3 == 0 && count)
        {
            const auto expected = scalar_span(indices, 0, count, 20, 1 << 28);
            const auto actual = vk_detail::plan_geometry_span(indices, 0, count, 20, 1 << 28);
            CHECK(actual.end == expected.end && actual.low == expected.low &&
                  actual.high == expected.high);
        }
        if (count)
        {
            indices[count - 1] = -1;
            CHECK(!vk_detail::valid_geometry_indices(indices, count, 407, 64));
        }
    }
    return true;
}
struct safe_view : view
{
    ~safe_view()
    {
        if (ctx && ctx->current_window) end_frame(ctx);
    }
};
struct guard_result
{
    i32 calls = 0;
    bool correct = true;
};
void expected_guard(i32, const char *text, void *user)
{
    auto &out = *static_cast<guard_result *>(user);
    ++out.calls;
    out.correct = out.correct && std::strcmp(text, "Vulkan index out of bounds") == 0;
}
bool invalid_prefix(vulkan_device_opts options)
{
    constexpr i32 COUNT = 90000;
    for (const i32 at : {0, 15, 16, COUNT - 1})
        for (const i32 bad : {-1, COUNT})
        {
            guard_result result;
            options.error_callback = expected_guard;
            options.user = &result;
            auto *d = static_cast<vk_detail::device *>(create_vulkan_device(options));
            CHECK(d);
            bool clean = false;
            {
                safe_view v;
                CHECK(v.initialize(d, 64, 64));
                begin_frame(v.ctx, *v.win, 0, 1.0 / 60.0);
                std::vector<vertex> vertices(COUNT, {8, 8, 0, 0, {255, 0, 0, 255}});
                std::vector<i32> indices(COUNT);
                for (i32 i = 0; i < COUNT; ++i) indices[i] = i;
                indices[at] = bad;
                const auto before = vulkan_statistics(d);
                d->draw(nullptr, vertices.data(), COUNT, indices.data(), COUNT);
                const auto after = vulkan_statistics(d);
                clean = result.calls == 1 && result.correct && after.draws == before.draws &&
                        after.upload_bytes == before.upload_bytes &&
                        after.submissions == before.submissions && !after.validation_errors;
                end_frame(v.ctx);
            }
            destroy_vulkan_device(d);
            CHECK(clean);
        }
    return true;
}
bool pixels(vk_detail::device &d)
{
    constexpr i32 COUNT = 12288, BASE = 407;
    std::vector<vertex> vertices(COUNT);
    std::vector<i32> indices;
    for (i32 i = 0; i < COUNT; i += 3)
    {
        const f32 x = static_cast<f32>((i / 3 * 7) % 59) + .25f,
                  y = static_cast<f32>((i / 3 * 11) % 59) + .375f;
        const color c{static_cast<u8>(i * 13), static_cast<u8>(i * 29), static_cast<u8>(i * 47),
                      static_cast<u8>(83 + i % 161)};
        vertices[i] = {x, y, 0, 0, c};
        vertices[i + 1] = {x + 2.75f, y, 0, 0, c};
        vertices[i + 2] = {x, y + 3.125f, 0, 0, c};
        indices.insert(indices.end(), {BASE + i, BASE + i + 1, BASE + i + 2});
    }
    for (i32 i = 0; i < 65; ++i)
        indices.insert(indices.end(), {BASE + i, BASE + COUNT - 1 - i, BASE + COUNT / 2});
    safe_view v;
    CHECK(v.initialize(&d, 64, 64));
    std::vector<u8> actual(64 * 64 * 4), expected(actual.size());
    for (i32 variant = 0; variant < 2; ++variant)
    {
        begin_frame(v.ctx, *v.win, variant / 60.0, 1.0 / 60.0);
        if (!variant)
            for (auto &slot : static_cast<vk_detail::surface *>(v.surface)->slots)
                CHECK(d.prepare_slot_geometry(slot, 4096));
        const auto before = vulkan_statistics(&d);
        if (!variant)
            d.paint(d.white, vertices.data(), COUNT, indices.data(),
                    static_cast<i32>(indices.size()), true, BASE);
        else
            for (usize at = 0; at < indices.size();)
            {
                vertex batch[96];
                i32 local[96];
                const i32 count = static_cast<i32>(std::min<usize>(96, indices.size() - at));
                for (i32 j = 0; j < count; ++j)
                {
                    batch[j] = vertices[indices[at + j] - BASE];
                    local[j] = j;
                }
                d.draw(nullptr, batch, count, local, count);
                at += count;
            }
        CHECK(d.stats.draw_buffer_allocations == before.draw_buffer_allocations);
        end_frame(v.ctx);
        CHECK(d.read_pixels(v.surface->scene_target(), 0, 0, 64, 64,
                            variant ? expected.data() : actual.data()));
    }
    CHECK(actual == expected && !violation_count(v.ctx));
    return true;
}
} // namespace integer_test

int main(int argc, char **argv)
{
    bool staged = false, cpu_only = false;
    for (i32 i = 1; i < argc; ++i)
        if (!std::strcmp(argv[i], "--staged"))
            staged = true;
        else if (!std::strcmp(argv[i], "--cpu"))
            cpu_only = true;
        else
            return 2;
    if (!integer_test::validation_matrix() || !integer_test::planner_matrix() ||
        !integer_test::bounded_work() || !integer_test::guard_tails())
        return 1;
    std::printf(
        "integer CPU: %llu validation cases, %llu exact span tuples, guarded tails, SIMD=%d\n",
        static_cast<unsigned long long>(integer_test::validation_cases),
        static_cast<unsigned long long>(integer_test::span_cases), vk_detail::INTEGER_SIMD);
    if (cpu_only) return 0;
    for (const bool compact : {false, true})
        for (const u32 slots : {1u, 2u})
        {
            vulkan_device_opts options;
            options.validation = true;
            options.compact_vertices = compact;
            options.frames_in_flight = slots;
            options.upload_mode = staged ? vulkan_upload_mode::STAGED : vulkan_upload_mode::DIRECT;
            options.error_callback = diagnostic;
            if (!integer_test::invalid_prefix(options)) return 1;
            auto *d = static_cast<vk_detail::device *>(create_vulkan_device(options));
            if (!d) return 1;
            if (!compact && slots == 1)
                std::fprintf(stderr, "integer device=%s vendor=%u device=%u driver=%u\n",
                             d->properties.deviceName, d->properties.vendorID,
                             d->properties.deviceID, d->properties.driverVersion);
            const bool result =
                integer_test::pixels(*d) && vulkan_wait_idle(d) && !d->stats.validation_errors;
            destroy_vulkan_device(d);
            if (!result || errors) return 1;
        }
    std::puts(
        "integer Vulkan: exact dense/sparse pixels, pre-prefix validation and bounded work passed");
    return 0;
}
