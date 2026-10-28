// Windows-only diagnostic: GPU stages, C++ heap stacks and this module's C heap.
// Extra queries and stack capture affect timings; never use this executable as
// an acceptance benchmark. Build/run with tools/profile_vulkan_painter.py.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <intrin.h>
#include <cstdlib>
#include <new>
#include <cstdio>
#include <cstdint>
namespace probe
{
struct allocation
{
    size_t bytes;
    const char *kind;
    int frame;
    unsigned short depth;
    void *stack[12];
};
static allocation allocations[200000];
static unsigned allocation_count = 0, allocation_dropped = 0;
static thread_local bool watch = false, nesting = false;
static bool trace_startup = false;
static int current_frame = -1;
void allocated(size_t bytes, const char *kind = "new");
void *raw_malloc(size_t);
void *traced_malloc(size_t);
void *traced_calloc(size_t, size_t);
void *traced_realloc(void *, size_t);
} // namespace probe
// Link-time interposition affects only imports in this diagnostic executable
// (including its static HarfBuzz/SDL libraries), not another DLL or process.
extern "C"
{
    void *(__cdecl *__imp_malloc)(size_t) = probe::traced_malloc;
    void *(__cdecl *__imp_calloc)(size_t, size_t) = probe::traced_calloc;
    void *(__cdecl *__imp_realloc)(void *, size_t) = probe::traced_realloc;
}
void *operator new(size_t n)
{
    if (auto *p = probe::raw_malloc(n ? n : 1))
    {
        probe::allocated(n);
        return p;
    }
    throw std::bad_alloc();
}
void *operator new[](size_t n)
{
    return ::operator new(n);
}
void operator delete(void *p) noexcept
{
    std::free(p);
}
void operator delete[](void *p) noexcept
{
    std::free(p);
}
void operator delete(void *p, size_t) noexcept
{
    std::free(p);
}
void operator delete[](void *p, size_t) noexcept
{
    std::free(p);
}
void *operator new(size_t n, std::align_val_t a)
{
    if (auto *p = _aligned_malloc(n ? n : 1, static_cast<size_t>(a)))
    {
        probe::allocated(n, "aligned_new");
        return p;
    }
    throw std::bad_alloc();
}
void *operator new[](size_t n, std::align_val_t a)
{
    return ::operator new(n, a);
}
void operator delete(void *p, std::align_val_t) noexcept
{
    _aligned_free(p);
}
void operator delete[](void *p, std::align_val_t) noexcept
{
    _aligned_free(p);
}
void operator delete(void *p, size_t, std::align_val_t) noexcept
{
    _aligned_free(p);
}
void operator delete[](void *p, size_t, std::align_val_t) noexcept
{
    _aligned_free(p);
}
#define PUFFERUI_IMPLEMENTATION
#include <pufferui/pufferui.h>
#include <Windows.h>
#include <DbgHelp.h>
#undef DELETE
#undef near
#undef far
#undef small
#include "../examples/example_common.h"
namespace probe
{
void *raw_malloc(size_t bytes)
{
    using function = void *(__cdecl *)(size_t);
    static auto original =
        reinterpret_cast<function>(GetProcAddress(GetModuleHandleW(L"ucrtbase.dll"), "malloc"));
    return original(bytes);
}
void *traced_malloc(size_t bytes)
{
    void *pointer = raw_malloc(bytes);
    if (pointer) allocated(bytes, "malloc");
    return pointer;
}
void *traced_calloc(size_t count, size_t bytes)
{
    using function = void *(__cdecl *)(size_t, size_t);
    static auto original =
        reinterpret_cast<function>(GetProcAddress(GetModuleHandleW(L"ucrtbase.dll"), "calloc"));
    void *pointer = original(count, bytes);
    if (pointer) allocated(count * bytes, "calloc");
    return pointer;
}
void *traced_realloc(void *previous, size_t bytes)
{
    using function = void *(__cdecl *)(void *, size_t);
    static auto original =
        reinterpret_cast<function>(GetProcAddress(GetModuleHandleW(L"ucrtbase.dll"), "realloc"));
    void *pointer = original(previous, bytes);
    if (pointer) allocated(bytes, "realloc");
    return pointer;
}
void allocated(size_t bytes, const char *kind)
{
    if (!watch || nesting) return;
    nesting = true;
    if (allocation_count < 200000)
    {
        auto &r = allocations[allocation_count++];
        r.bytes = bytes;
        r.kind = kind;
        r.frame = current_frame;
        r.depth = CaptureStackBackTrace(2, 12, r.stack, nullptr);
    }
    else
        ++allocation_dropped;
    nesting = false;
}
using namespace pui;
static vk_detail::device *owner = nullptr;
static VkQueryPool queries = VK_NULL_HANDLE;
static constexpr unsigned max_samples = 65536;
static uint64_t ticks[max_samples * 2];
struct sample
{
    unsigned stage;
    int frame;
};
static sample samples[max_samples];
static unsigned sample_count = 0, stage = 10, stats_calls = 0;
static bool trace_program = false;
static PFN_vkCmdDispatch dispatch_fn;
static PFN_vkCmdDispatchIndirect indirect_fn;
static PFN_vkCmdDrawIndexed draw_fn;
static PFN_vkCmdBindPipeline bind_fn;
static const char *names[] = {"coarse",         "count",
                              "scan",           "emit",
                              "paint",          "args",
                              "compare",        "compact",
                              "restore",        "commit",
                              "other_compute",  "graphics",
                              "function_begin", "function_prepare",
                              "function_reset", "function_ready"};
static constexpr unsigned stage_count = sizeof(names) / sizeof(names[0]);
unsigned start(VkCommandBuffer command, unsigned which)
{
    if (sample_count == max_samples) std::abort();
    unsigned i = sample_count++;
    samples[i] = {which, current_frame};
    owner->vkCmdWriteTimestamp2(command, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, queries, i * 2);
    return i;
}
void finish(VkCommandBuffer command, unsigned i)
{
    owner->vkCmdWriteTimestamp2(command, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, queries, i * 2 + 1);
}
VKAPI_ATTR void VKAPI_CALL dispatch(VkCommandBuffer c, uint32_t x, uint32_t y, uint32_t z)
{
    auto i = start(c, stage);
    dispatch_fn(c, x, y, z);
    finish(c, i);
}
VKAPI_ATTR void VKAPI_CALL indirect(VkCommandBuffer c, VkBuffer b, VkDeviceSize o)
{
    auto i = start(c, stage);
    indirect_fn(c, b, o);
    finish(c, i);
}
VKAPI_ATTR void VKAPI_CALL draw(VkCommandBuffer c, uint32_t a, uint32_t b, uint32_t d, int32_t e,
                                uint32_t f)
{
    auto i = start(c, 11);
    draw_fn(c, a, b, d, e, f);
    finish(c, i);
}
VKAPI_ATTR void VKAPI_CALL bind(VkCommandBuffer c, VkPipelineBindPoint p, VkPipeline pipeline)
{
    if (p == VK_PIPELINE_BIND_POINT_COMPUTE)
    {
        stage = 10;
        for (unsigned i = 0; i < 4; ++i)
            if (pipeline == owner->bin_pipelines[i]) stage = i;
        for (unsigned i = 0; i < 6; ++i)
            if (pipeline == owner->sdf_pipelines[i]) stage = i + 4;
        if (auto *cache = owner->sdf_functions)
        {
            const VkPipeline kernels[] = {cache->begin, cache->prepare, cache->reset, cache->ready};
            for (unsigned i = 0; i < 4; ++i)
                if (pipeline == kernels[i]) stage = i + 12;
        }
    }
    bind_fn(c, p, pipeline);
}
bool init(sdl3_app &app, const char *title, i32 w, i32 h, int argc, char **argv)
{
    if (!sdl3_app_init(app, title, w, h, argc, argv)) return false;
    owner = static_cast<vk_detail::device *>(app.device);
#if defined(PUI_PROFILE_HEAP_ONLY)
    return true;
#endif
    if (!owner->timestamp_bits)
    {
        sdl3_app_shutdown(app);
        return false;
    }
    VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    info.queryCount = max_samples * 2;
    if (owner->vkCreateQueryPool(owner->native.device, &info, nullptr, &queries) != VK_SUCCESS)
        return false;
    if (!owner->commands()) return false;
    owner->vkCmdResetQueryPool(owner->frame->command, queries, 0, max_samples * 2);
    auto serial = owner->submit();
    if (!serial || !owner->wait(serial)) return false;
    dispatch_fn = owner->vkCmdDispatch;
    owner->vkCmdDispatch = dispatch;
    indirect_fn = owner->vkCmdDispatchIndirect;
    owner->vkCmdDispatchIndirect = indirect;
    draw_fn = owner->vkCmdDrawIndexed;
    owner->vkCmdDrawIndexed = draw;
    bind_fn = owner->vkCmdBindPipeline;
    owner->vkCmdBindPipeline = bind;
    return true;
}
vulkan_stats statistics(render_device *device)
{
    watch = false;
    auto result = vulkan_statistics(device);
    if (trace_program && stats_calls >= 2 && stats_calls % 2 == 0 && owner &&
        !owner->surfaces.empty())
    {
        auto &surface = *owner->surfaces.front();
        auto *program = surface.retained[1 - surface.retained_bank];
        auto *cache = surface.program_cache;
        if (program && cache)
            std::fprintf(
                stderr,
                "PROGRAM frame=%u valid=%d equal=%d draws=%zu images=%zu bytes=%zu "
                "serial=%llu next=%llu warm=%d epoch=%llu confirmed=%llu probes=%llu hits=%llu "
                "glyph_bytes=%zu glyph_resident=%llu glyph_capacity=%llu surfaces=%zu\n",
                (stats_calls - 2) / 2, program->valid, owner->retained_equal, program->draw_count,
                program->image_count, program->payload_used, program->serial, owner->next_serial,
                cache->warm, cache->epoch, cache->confirmed, result.whole_program_probes,
                result.whole_program_hits, owner->slug_data.size(), owner->slug_storage.used,
                owner->slug_storage.capacity, owner->surfaces.size());
    }
    // First call is the benchmark's calibration report; then before/after each frame.
    if (stats_calls == 0 && trace_startup) watch = true;
    if (stats_calls >= 1 && stats_calls % 2 == 1)
    {
        current_frame = static_cast<int>((stats_calls - 1) / 2);
        watch = true;
    }
    ++stats_calls;
    return result;
}
void shutdown(sdl3_app &app)
{
    watch = false;
    vulkan_wait_idle(app.device);
#if defined(PUI_PROFILE_HEAP_ONLY)
    sdl3_app_shutdown(app);
    return;
#endif
    auto result = owner->vkGetQueryPoolResults(
        owner->native.device, queries, 0, sample_count * 2, sizeof(uint64_t) * sample_count * 2,
        ticks, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
    std::fprintf(stderr, "PROFILE queries=%u stats_calls=%u result=%d heap=%u dropped=%u\n",
                 sample_count, stats_calls, result, allocation_count, allocation_dropped);
    FILE *gpu = std::fopen("gpu-stages.csv", "w");
    std::fprintf(gpu, "sample,frame,stage,nanoseconds\n");
    double totals[stage_count]{};
    unsigned counts[stage_count]{};
    const uint64_t mask =
        owner->timestamp_bits == 64 ? ~uint64_t(0) : (uint64_t(1) << owner->timestamp_bits) - 1;
    for (unsigned i = 0; i < sample_count; ++i)
    {
        double ns = double((ticks[i * 2 + 1] - ticks[i * 2]) & mask) *
                    owner->properties.limits.timestampPeriod;
        totals[samples[i].stage] += ns;
        counts[samples[i].stage]++;
        std::fprintf(gpu, "%u,%d,%s,%.3f\n", i, samples[i].frame, names[samples[i].stage], ns);
    }
    std::fclose(gpu);
    for (unsigned i = 0; i < stage_count; ++i)
        std::fprintf(stderr, "STAGE %s count=%u ms=%.6f\n", names[i], counts[i], totals[i] / 1e6);
    owner->vkCmdDispatch = dispatch_fn;
    owner->vkCmdDispatchIndirect = indirect_fn;
    owner->vkCmdDrawIndexed = draw_fn;
    owner->vkCmdBindPipeline = bind_fn;
    owner->vkDestroyQueryPool(owner->native.device, queries, nullptr);
    queries = VK_NULL_HANDLE;
    sdl3_app_shutdown(app);
}
void heap_report()
{
    watch = false;
    auto process = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(process, nullptr, TRUE);
    FILE *file = std::fopen("heap.csv", "w");
    std::fprintf(file, "allocation,frame,bytes,kind,depth,address,symbol,file,line\n");
    for (unsigned i = 0; i < allocation_count; ++i)
    {
        const auto &r = allocations[i];
        for (unsigned j = 0; j < r.depth; ++j)
        {
            alignas(SYMBOL_INFO) char memory[sizeof(SYMBOL_INFO) + 1024]{};
            auto *symbol = reinterpret_cast<SYMBOL_INFO *>(memory);
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = 1023;
            DWORD64 displacement = 0;
            IMAGEHLP_LINE64 line{};
            line.SizeOfStruct = sizeof(line);
            DWORD line_displacement = 0;
            SymFromAddr(process, reinterpret_cast<DWORD64>(r.stack[j]), &displacement, symbol);
            SymGetLineFromAddr64(process, reinterpret_cast<DWORD64>(r.stack[j]), &line_displacement,
                                 &line);
            std::fprintf(file, "%u,%d,%zu,%s,%u,%p,\"%s\",\"%s\",%u\n", i, r.frame, r.bytes, r.kind,
                         j, r.stack[j], symbol->Name, line.FileName ? line.FileName : "",
                         line.LineNumber);
        }
    }
    std::fclose(file);
    SymCleanup(process);
}
} // namespace probe
#define sdl3_app_init probe::init
#define sdl3_app_shutdown probe::shutdown
#define vulkan_statistics probe::statistics
#include PUI_PROFILE_BENCHMARK
#undef sdl3_app_init
#undef sdl3_app_shutdown
#undef vulkan_statistics
int main(int argc, char **argv)
{
    probe::trace_program = std::getenv("PUI_PROFILE_PROGRAM_STATE") != nullptr;
    // Confirm that C imports are intercepted and C++ new is counted once.
    probe::watch = true;
    void *c = std::malloc(123);
    void *z = std::calloc(7, 19);
    c = std::realloc(c, 257);
    void *cpp = ::operator new(157);
    void *aligned = ::operator new(193, std::align_val_t(64));
    probe::watch = false;
    bool valid = probe::allocation_count == 5;
    const char *expected[] = {"malloc", "calloc", "realloc", "new", "aligned_new"};
    for (unsigned i = 0; i < probe::allocation_count && i < 5; ++i)
        valid = valid && std::strcmp(probe::allocations[i].kind, expected[i]) == 0;
    std::free(c);
    std::free(z);
    ::operator delete(cpp);
    ::operator delete(aligned, std::align_val_t(64));
    if (!valid)
    {
        std::fprintf(stderr, "heap interposition selftest failed\n");
        return 2;
    }
    probe::allocation_count = 0;
    if (argc == 2 && std::strcmp(argv[1], "--trace-selftest") == 0)
    {
        std::puts("heap trace selftest passed");
        return 0;
    }
    const char *startup = std::getenv("PUI_PROFILE_STARTUP");
    probe::trace_startup = startup && std::strcmp(startup, "1") == 0;
    probe::watch = probe::trace_startup;
    int result = original_benchmark_main(argc, argv);
    probe::heap_report();
    return result;
}
