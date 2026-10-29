// Exact native Slug coverage, independent of bitmap text and shape painters.
// One bounded R32 image, four recyclable regions, exact operand identities.
// Image preparation and retirement occur only at surface frame boundaries.
// Included inside namespace pui by the single implementation TU.
#if defined(PUFFERUI_ENABLE_VULKAN) && defined(PUFFERUI_ENABLE_HARFBUZZ)
namespace vk_detail
{
bool device::initialize_coverage()
{
    if (options.glyph_coverage_budget_bytes < 128 * 128 * 4) return true;
    VkFormatProperties format{};
    vkGetPhysicalDeviceFormatProperties(native.physical_device, VK_FORMAT_R32_SFLOAT, &format);
    constexpr VkFormatFeatureFlags required = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
                                              VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT |
                                              VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if ((format.optimalTilingFeatures & required) != required) return true;
    text->coverage = new glyph_coverage();
    auto &cache = *text->coverage;
    cache.extent = 1024;
    while (static_cast<u64>(cache.extent) * cache.extent * 4 > options.glyph_coverage_budget_bytes)
        cache.extent /= 2;
    cache.fills.reserve(text->pending.capacity());
    // Compile both specialization variants before any UI frame. The image is
    // allocated later at a frame boundary, only after repeated exact operands.
    return pipeline(VK_FORMAT_R32_SFLOAT, true, true) != VK_NULL_HANDLE;
}
bool device::prepare_coverage()
{
    if (!text || !text->coverage) return true;
    auto &cache = *text->coverage;
    if (paint_depth || !text->pending.empty()) return true; // defer; queued work owns the boundary
    ++cache.epoch;
    if (cache.requested && !cache.atlas)
    {
        auto *atlas = allocate_texture(
            cache.extent, cache.extent,
            {.format = VK_FORMAT_R32_SFLOAT, .render_target = true, .internal = true});
        if (!atlas) return false;
        if (atlas->allocation > options.glyph_coverage_budget_bytes)
        {
            free_texture(atlas);
            delete text->coverage;
            text->coverage = nullptr;
            return true; // unsupported allocation granularity: exact native fallback
        }
        cache.atlas = atlas;
        stats.glyph_coverage_bytes = atlas->allocation;
    }
    cache.requested = false;
    bool reclaimed = false;
    for (auto &region : cache.regions)
        if (region.retiring && region.last_use <= known_completed)
        {
            const u64 generation = new_generation();
            if (!generation) return false;
            region = {};
            region.generation = generation;
            reclaimed = true;
            ++stats.glyph_coverage_evictions;
        }
    // A pressure report predates this boundary. Give a freshly reclaimed
    // region one frame of admissions before choosing another region to retire.
    if (cache.pressure && !reclaimed)
    {
        bool retiring = false;
        for (const auto &region : cache.regions) retiring |= region.retiring;
        if (!retiring)
        {
            auto *oldest = &cache.regions[0];
            for (auto &region : cache.regions)
                if (region.seen < oldest->seen) oldest = &region;
            // Stop using this region for a few ordinary frames rather than
            // waiting for the GPU. Its immutable old pixels retire by timeline.
            oldest->retiring = true;
        }
    }
    cache.pressure = false;
    return true;
}
bool device::cache_glyph(glyph_instance &instance)
{
    auto &cache = *text->coverage;
    glyph_coverage::key key;
    key.words[0] = text->pending_chunk;
    key.words[1] = instance.location;
    std::memcpy(key.words + 2, &instance.left, 8 * sizeof(f32));
    u64 hash = 1469598103934665603ull;
    for (u32 word : key.words) hash = (hash ^ word) * 1099511628211ull;
    // Float coordinates on an integer grid have identical low mantissa bits.
    // Avalanche before selecting a set; otherwise all positions of one glyph
    // collapse to the same four ways despite the bounded table's spare space.
    hash = direct_cache<u32, 2>::mix(hash);
    const u32 first = static_cast<u32>(hash) & (glyph_coverage::ENTRIES / glyph_coverage::WAYS - 1);
    auto *entry = &cache.entries[first * glyph_coverage::WAYS];
    auto *oldest = entry;
    bool found = false;
    for (u32 i = 0; i < glyph_coverage::WAYS; ++i)
    {
        auto &candidate = cache.entries[first * glyph_coverage::WAYS + i];
        if (candidate.tag == hash && candidate.identity == key)
        {
            entry = &candidate;
            found = true;
            break;
        }
        if (!candidate.tag || candidate.seen < oldest->seen) oldest = &candidate;
    }
    if (!found)
    {
        entry = oldest;
        *entry = {};
        entry->identity = key;
        entry->tag = hash;
    }
    const bool repeated =
        entry->seen && entry->seen < cache.epoch && cache.epoch - entry->seen <= 2;
    entry->seen = cache.epoch;
    auto use = [&]
    {
        auto &region = cache.regions[entry->page];
        instance.location |= glyph_coverage::CACHED;
        instance.reserved[0] = entry->atlas;
        // Bounds and their absolute rounding are part of the exact key.
        const i32 x = static_cast<i32>(std::floor(instance.left)) - 1;
        const i32 y = static_cast<i32>(std::floor(instance.top)) - 1;
        std::memcpy(instance.reserved + 1, &x, sizeof(x));
        std::memcpy(instance.reserved + 2, &y, sizeof(y));
        region.seen = cache.epoch;
    };
    if (entry->resident && cache.atlas &&
        entry->generation == cache.regions[entry->page].generation &&
        !cache.regions[entry->page].retiring)
    {
        use();
        ++stats.glyph_coverage_hits;
        return true;
    }
    ++stats.glyph_coverage_misses;
    if (!repeated) return false;
    for (u32 i = 2; i < 10; ++i)
        if ((key.words[i] & 0x7f800000u) == 0x7f800000u) return false;
    if (!(instance.scale > 0) || std::fabs(instance.left) > 1048576 ||
        std::fabs(instance.top) > 1048576 || std::fabs(instance.right) > 1048576 ||
        std::fabs(instance.bottom) > 1048576)
        return false;
    const i32 x = static_cast<i32>(std::floor(instance.left)) - 1;
    const i32 y = static_cast<i32>(std::floor(instance.top)) - 1;
    const i32 width = static_cast<i32>(std::ceil(instance.right)) + 1 - x;
    const i32 height = static_cast<i32>(std::ceil(instance.bottom)) + 1 - y;
    const u32 side = static_cast<u32>(cache.extent / 2);
    if (width <= 0 || height <= 0 || static_cast<u32>(width) > side ||
        static_cast<u32>(height) > side)
        return false;
    if (!cache.atlas)
    {
        cache.requested = true;
        return false;
    }
    for (u32 i = 0; i < 4; ++i)
    {
        auto &region = cache.regions[i];
        if (region.retiring) continue;
        u32 left = region.x, top = region.y, row_height = region.height;
        if (left + static_cast<u32>(width) > side)
        {
            left = 0;
            top += row_height;
            row_height = 0;
        }
        if (top + static_cast<u32>(height) > side) continue;
        region.x = left + static_cast<u32>(width);
        region.y = top;
        region.height = std::max(row_height, static_cast<u32>(height));
        left += (i % 2) * side;
        top += (i / 2) * side;
        entry->page = i;
        entry->generation = region.generation;
        entry->atlas = left | (top << 16);
        entry->resident = true;
        glyph_instance fill = instance;
        fill.left = static_cast<f32>(left);
        fill.top = static_cast<f32>(top);
        fill.right = static_cast<f32>(left + width);
        fill.bottom = static_cast<f32>(top + height);
        fill.reserved[0] = entry->atlas;
        std::memcpy(fill.reserved + 1, &x, sizeof(x));
        std::memcpy(fill.reserved + 2, &y, sizeof(y));
        cache.fills.push_back(fill);
        ++stats.glyph_coverage_fills;
        use();
        return true;
    }
    cache.pressure = true;
    return false;
}
bool device::paint_coverage()
{
    auto &cache = *text->coverage;
    if (cache.fills.empty()) return true;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    if (!upload_glyphs(cache.fills.data(), cache.fills.size(), buffer, offset)) return false;
    transition(white, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (!start_rendering(cache.atlas)) return false;
    const VkPipeline pipe = pipeline(VK_FORMAT_R32_SFLOAT, true, true);
    if (!pipe) return false;
    const u32 extent = static_cast<u32>(cache.extent);
    const VkRect2D scissor{{0, 0}, {extent, extent}};
    vkCmdBindVertexBuffers(frame->command, 0, 1, &buffer, &offset);
    bind_graphics(pipe, text->pipeline_layout, {extent, extent}, scissor, white->descriptors[0], 0);
    bind_graphics(pipe, text->pipeline_layout, {extent, extent}, scissor,
                  text->chunks[text->pending_chunk].descriptor, 1);
    vkCmdDraw(frame->command, 6, static_cast<u32>(cache.fills.size()), 0, 0);
    ++stats.draws;
    ++stats.glyph_coverage_draws;
    cache.atlas->last_use = next_serial;
    ++cache.atlas->revision;
    end_rendering();
    transition(cache.atlas, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    cache.fills.clear();
    return true;
}
} // namespace vk_detail
#endif
