// Private CPU-only tests compile the actual optional renderer implementation.
// Deliberately equal hashes must not create a false cache hit.
// No GPU or window is needed by this executable.
// Included at the end of the one implementation TU.
#undef near
#undef far
#undef small
#include "test_util.h"
PUI_TEST(vulkan_cache_hash_collision_compares_exact_input)
{
    vk_detail::cache_record a, b;
    a.hash_a = b.hash_a = 7;
    a.hash_b = b.hash_b = 11;
    a.canonical = {1, 2, 3, 4};
    b.canonical = {1, 2, 3, 5};
    CHECK(!vk_detail::cache_records_equal(a, b));
    b.canonical = a.canonical;
    CHECK(vk_detail::cache_records_equal(a, b));
    b.canonical.push_back(0);
    CHECK(!vk_detail::cache_records_equal(a, b));
}
PUI_TEST(vulkan_cache_canonicalizes_fields_and_tracks_content_revision)
{
    vk_detail::device d;
    vk_detail::surface surface;
    vk_detail::texture target, sample;
    target.width = target.height = 64;
    target.generation = 5;
    sample.generation = 7;
    sample.revision = 9;
    surface.scene = &target;
    surface.extent = {64, 64};
    d.target = &target;
    d.current_surface = &surface;
    d.textures = {&target, &sample};
    d.options.tile_cache = true;
    const vertex v[] = {
        {0, 0, 0, 0, {1, 2, 3, 4}}, {10, 0, 1, 0, {5, 6, 7, 8}}, {0, 10, 0, 1, {9, 10, 11, 12}}};
    const i32 idx[] = {0, 1, 2};
    d.cache_begin(&surface);
    CHECK(d.cache_add(vk_detail::cache_event::DRAW, &sample, v, 3, idx, 3));
    auto original = d.records[0];
    // Frozen pre-optimization little-endian field stream, including all color
    // channels and the original index order. This is not struct memory.
    const u8 expected_payload[] = {0,   0,  0, 0,  0,  0,  0, 0, 0, 0, 0,  0,  0,   0,  0, 0, 1, 2,
                                   3,   4,  0, 0,  32, 65, 0, 0, 0, 0, 0,  0,  128, 63, 0, 0, 0, 0,
                                   5,   6,  7, 8,  0,  0,  0, 0, 0, 0, 32, 65, 0,   0,  0, 0, 0, 0,
                                   128, 63, 9, 10, 11, 12, 0, 0, 0, 0, 1,  0,  0,   0,  2, 0, 0, 0};
    CHECK(original.canonical.size() >= sizeof(expected_payload));
    CHECK(std::memcmp(original.canonical.data() + original.canonical.size() -
                          sizeof(expected_payload),
                      expected_payload, sizeof(expected_payload)) == 0);
    d.cache_begin(&surface);
    CHECK(d.cache_add(vk_detail::cache_event::DRAW, &sample, v, 3, idx, 3));
    CHECK(vk_detail::cache_records_equal(original, d.records[0]));
    ++sample.revision;
    d.cache_begin(&surface);
    CHECK(d.cache_add(vk_detail::cache_event::DRAW, &sample, v, 3, idx, 3));
    CHECK(!vk_detail::cache_records_equal(original, d.records[0]));
    ++sample.generation;
    d.cache_begin(&surface);
    CHECK(d.cache_add(vk_detail::cache_event::DRAW, &sample, v, 3, idx, 3));
    CHECK(!vk_detail::cache_records_equal(original, d.records[0]));
}
PUI_TEST(vulkan_invalid_factory_options_are_captured)
{
    struct capture
    {
        i32 calls = 0, code = 0;
    } log;
    vulkan_device_opts opts;
    opts.frames_in_flight = 0;
    opts.user = &log;
    opts.error_callback = [](i32 code, const char *, void *user)
    {
        auto *c = static_cast<capture *>(user);
        ++c->calls;
        c->code = code;
    };
    CHECK(create_vulkan_device(opts) == nullptr);
    CHECK(log.calls == 1 && log.code == VK_ERROR_INITIALIZATION_FAILED);
    opts.frames_in_flight = 2;
    opts.upload_mode = static_cast<vulkan_upload_mode>(255);
    CHECK(create_vulkan_device(opts) == nullptr);
    CHECK(log.calls == 2 && log.code == VK_ERROR_INITIALIZATION_FAILED);
}
PUI_TEST(vulkan_generation_wrap_invalidates_and_reidentifies_live_resources)
{
    vk_detail::device d;
    vk_detail::surface s;
    vk_detail::texture a, b;
    s.history_valid = true;
    s.history.push_back({});
    d.surfaces.push_back(&s);
    d.textures = {&a, &b};
    d.generation = UINT64_MAX;
    CHECK(d.new_generation() == UINT64_MAX);
    const u64 next = d.new_generation();
    CHECK(next && next != a.generation && next != b.generation && a.generation != b.generation);
    CHECK(!s.history_valid && s.history.empty());
    CHECK(!d.wait(UINT64_MAX));
    CHECK(d.wait(0));
}
PUI_TEST(vulkan_cache_budget_fallback_preserves_the_full_reference_route)
{
    vk_detail::device d;
    vk_detail::surface s;
    vk_detail::texture target;
    target.width = target.height = 16;
    s.scene = &target;
    s.extent = {16, 16};
    d.textures = {&target};
    d.current_surface = &s;
    d.target = &target;
    d.options.tile_cache = true;
    d.options.cache_budget_bytes = 8;
    d.cache_begin(&s);
    CHECK(d.cache_active);
    CHECK(!d.cache_add(vk_detail::cache_event::CLEAR));
    CHECK(!d.cache_active && !s.history_valid && d.events.empty());
    CHECK(d.stats.cache_fallbacks == 1 && d.cache_usage() <= 8);
}
int main()
{
    for (const auto &test : test_registry()) test.fn();
    if (!g_failures)
        std::printf("all Vulkan internal regressions passed (%zu tests)\n", test_registry().size());
    return g_failures ? 1 : 0;
}
