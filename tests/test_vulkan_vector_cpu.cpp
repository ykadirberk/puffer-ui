// Independent native CPU regressions and full-frame simple-path goldens.
// Reuse the actual unchanged vector benchmark, including its frame workload.
#define PUFFERUI_IMPLEMENTATION
#include <pufferui/vulkan.h>
#include "test_util.h"
#include <array>
#include <filesystem>
#include <fstream>

namespace vector_cpu_test
{
struct frame_capture
{
    bool active = false, capture = false, okay = true;
    usize calls = 0, frames = 0;
    std::filesystem::path directory;
    std::vector<u8> actual, expected;
};
frame_capture images;
vulkan_stats capture_statistics(render_device *base)
{
    const auto stats = vulkan_statistics(base);
    if (!images.active || !images.okay) return stats;
    // The unchanged benchmark queries initial stats, then before/after each
    // frame, then final stats. Read back only after each completed UI frame.
    const usize call = ++images.calls;
    if (call < 3 || (call - 3) % 2 || images.frames == 180) return stats;
    auto *dev = static_cast<vk_detail::device *>(base);
    if (!dev->current_surface || !base->read_pixels(dev->current_surface->scene_target(), 0, 0, 512,
                                                    512, images.actual.data()))
    {
        images.okay = false;
        return stats;
    }
    char name[32];
    std::snprintf(name, sizeof(name), "frame-%03zu.rgba", images.frames);
    const auto path = images.directory / name;
    if (images.capture)
    {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char *>(images.actual.data()), images.actual.size());
        images.okay = file.good();
    }
    else
    {
        std::ifstream file(path, std::ios::binary);
        file.read(reinterpret_cast<char *>(images.expected.data()), images.expected.size());
        images.okay = file && file.peek() == std::char_traits<char>::eof() &&
                      images.actual == images.expected;
    }
    if (!images.okay)
        std::fprintf(stderr, "simple path reference mismatch at frame %zu\n", images.frames);
    ++images.frames;
    return stats;
}
} // namespace vector_cpu_test

// Only this independent test's imported workload uses the capture wrapper.
// The implementation above and the existing test executable are unaffected.
#define vulkan_statistics vector_cpu_test::capture_statistics
#define main vector_cpu_original_main
#include "test_vector_data.inl"
#undef main
#undef vulkan_statistics

#if !defined(PUFFERUI_VECTOR_CPU_BASELINE)
#include "test_vulkan_vector_cpu.inl"
#endif

int main(int argc, char **argv)
{
    bool staged = false, capture = false;
    const char *directory = nullptr;
    for (i32 i = 1; i < argc; ++i)
        if (!std::strcmp(argv[i], "--staged"))
            staged = true;
        else if ((!std::strcmp(argv[i], "--capture-reference") ||
                  !std::strcmp(argv[i], "--reference")) &&
                 i + 1 < argc)
        {
            if (directory) return 2;
            capture = !std::strcmp(argv[i], "--capture-reference");
            directory = argv[++i];
        }
        else
            return 2;
    if (directory)
    {
        auto &frames = vector_cpu_test::images;
        frames.directory = directory;
        frames.capture = capture;
        if (capture && (std::filesystem::exists(frames.directory) ||
                        !std::filesystem::create_directories(frames.directory)))
        {
            std::fputs("simple path capture requires a new directory\n", stderr);
            return 2;
        }
        frames.actual.resize(512 * 512 * 4);
        frames.expected.resize(frames.actual.size());
        frames.active = true;
        char program[] = "simple-path-goldens", bench[] = "--bench", mode[] = "--staged";
        char *arguments[] = {program, bench, mode};
        const i32 result = vector_benchmark(staged ? 3 : 2, arguments);
        frames.active = false;
        if (result || !frames.okay || frames.frames != 180) return 1;
        std::printf("180 exact simple Slug frames passed (%s, capture=%d)\n",
                    staged ? "staged" : "direct", capture);
        return 0;
    }
#if !defined(PUFFERUI_VECTOR_CPU_BASELINE)
    if (!vector_cpu_test::placement(staged) || !vector_cpu_test::planner()) return 1;
    std::puts("all native vector CPU regressions passed (single placement, errors, DPI and "
              "independent block extrema)");
    return g_failures ? 1 : 0;
#else
    return 2;
#endif
}
