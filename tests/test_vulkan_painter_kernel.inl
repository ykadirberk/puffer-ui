// Verify the compiled work boundary, independently of source spelling or timing.
namespace
{
template <usize N> bool painter_kernel_binding(const u32 (&module)[N], i32 set, i32 binding)
{
    CHECK(N >= 5 && module[0] == 0x07230203u);
    if (N < 5 || module[0] != 0x07230203u) return false;
    std::vector<i32> sets(module[3], -1), bindings(module[3], -1);
    for (usize at = 5; at < N;)
    {
        const u32 words = module[at] >> 16, operation = module[at] & 0xffffu;
        CHECK(words && words <= N - at);
        if (!words || words > N - at) return false;
        if (operation == 71 && words == 4 && module[at + 1] < module[3]) // OpDecorate
        {
            const u32 id = module[at + 1], decoration = module[at + 2];
            if (decoration == 34) sets[id] = static_cast<i32>(module[at + 3]);
            if (decoration == 33) bindings[id] = static_cast<i32>(module[at + 3]);
        }
        at += words;
    }
    for (usize id = 0; id < sets.size(); ++id)
        if (sets[id] == set && bindings[id] == binding) return true;
    return false;
}
} // namespace

PUI_TEST(test_integrated_interpreter_does_not_trace_uncached_slug_twice)
{
    tf_env env;
    // The interpreter reads exact resident alpha, not outline curves. These
    // positive controls also prevent disabling Slug in the producer/replay.
    CHECK(painter_kernel_binding(vulkan_integrated_paint, 1, 4));
    CHECK(!painter_kernel_binding(vulkan_integrated_paint, 1, 1));
    CHECK(painter_kernel_binding(vulkan_glyph_source_fill, 1, 1));
    CHECK(painter_kernel_binding(vulkan_integrated_frag, 1, 1));
    CHECK(painter_kernel_binding(vulkan_sdf_frag, 1, 1));
}
