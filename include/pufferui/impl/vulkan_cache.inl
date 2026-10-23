// Vulkan retained-pixel cache implementation.
// Cache metadata is device/target generation owned.
// Full reference rendering remains available independently of the cache.
// Included inside namespace pui by the single implementation TU.
#if defined(PUFFERUI_ENABLE_VULKAN)
namespace vk_detail
{
u64 device::cache_usage() const
{
    u64 bytes = events.capacity() * sizeof(cache_event) +
                records.capacity() * sizeof(cache_record) +
                cache_written.capacity() * sizeof(texture *);
    for (const auto &e : events)
        bytes += e.vertices.capacity() * sizeof(vertex) + e.indices.capacity() * sizeof(i32);
    for (const auto &r : records) bytes += r.canonical.capacity();
    for (const auto *s : surfaces)
    {
        bytes += s->history.capacity() * sizeof(cache_record);
        for (const auto &r : s->history) bytes += r.canonical.capacity();
    }
    return bytes;
}
bool device::cache_reserve(u64 bytes)
{
    if (bytes > options.cache_budget_bytes) return false;
    for (auto *s : surfaces)
    {
        if (cache_usage() <= options.cache_budget_bytes - bytes) return true;
        if (s->history.capacity())
        {
            std::vector<cache_record>().swap(s->history);
            s->history_valid = false;
            s->history_bytes = 0;
            ++stats.cache_evictions;
        }
    }
    return cache_usage() <= options.cache_budget_bytes - bytes;
}
u64 device::new_generation()
{
    if (generation == 0)
    {
        if (cache_active) cache_flush(false);
        for (auto *s : surfaces)
        {
            s->history_valid = false;
            s->history.clear();
            s->history_bytes = 0;
        }
        generation = 1;
        for (auto *t : textures) t->generation = generation++;
    }
    return generation++;
}
inline bool cache_records_equal(const cache_record &a, const cache_record &b)
{
    return a.hash_a == b.hash_a && a.hash_b == b.hash_b && a.canonical == b.canonical;
}
void device::cache_begin(surface *s)
{
    cache_active = options.tile_cache && options.cache_budget_bytes && s && s->scene;
    if (s && s->cache_cooldown)
    {
        --s->cache_cooldown;
        cache_active = false;
        if (options.tile_cache) ++stats.cache_bypasses;
    }
    cache_pending = nullptr;
    cache_bytes = 0;
    events.clear();
    records.clear();
    cache_written.clear();
    if (!cache_active && s)
    {
        s->history_valid = false;
        std::vector<cache_record>().swap(s->history);
        s->history_bytes = 0;
        std::vector<cache_event>().swap(events);
        std::vector<cache_record>().swap(records);
        std::vector<texture *>().swap(cache_written);
    }
}
bool device::cache_add(cache_event::kind kind, texture *sample, const vertex *v, i32 vc,
                       const i32 *idx, i32 ic, color c, const rect *sr, const rect *dr)
{
    if (!cache_active || !current_surface || !valid(target)) return false;
    struct prepare_timer
    {
        vulkan_stats &stats;
        f64 start = wall_ms(), previous_flush = stats.cache_ms;
        ~prepare_timer()
        {
            // A texture/budget fallback can flush inside this call. Attribute
            // that separately instead of counting its replay time twice.
            const f64 elapsed = wall_ms() - start - (stats.cache_ms - previous_flush);
            if (elapsed > 0) stats.cache_prepare_ms += elapsed;
        }
    } timer{stats};
    const bool draw = kind == cache_event::DRAW;
    if ((sample && !valid(sample)) || (draw && (!v || !idx || vc <= 0 || ic <= 0 || ic % 3)))
    {
        cache_flush(false);
        return false;
    }
    const u64 payload = draw ? static_cast<u64>(vc) * 20 + static_cast<u64>(ic) * 4 : 0;
    const u64 event_capacity =
        events.size() == events.capacity() ? (events.capacity() ? events.capacity() * 2 : 8) : 0;
    const u64 record_capacity = records.size() == records.capacity()
                                    ? (records.capacity() ? records.capacity() * 2 : 8)
                                    : 0;
    const u64 written_capacity = cache_written.size() == cache_written.capacity()
                                     ? (cache_written.capacity() ? cache_written.capacity() * 2 : 8)
                                     : 0;
    const u64 needed = payload * 2 + 128 + event_capacity * sizeof(cache_event) +
                       record_capacity * sizeof(cache_record) +
                       written_capacity * sizeof(texture *);
    if (!cache_reserve(needed))
    {
        ++stats.cache_fallbacks;
        cache_flush(false);
        return false;
    }
    cache_record record;
    if (event_capacity) events.reserve(static_cast<usize>(event_capacity));
    if (record_capacity) records.reserve(static_cast<usize>(record_capacity));
    if (written_capacity) cache_written.reserve(static_cast<usize>(written_capacity));
    record.canonical.reserve(static_cast<usize>(payload + 128));
    auto integer = [&](u64 value, u32 bytes)
    {
        for (u32 i = 0; i < bytes; ++i)
            record.canonical.push_back(static_cast<u8>(value >> (8 * i)));
    };
    auto number = [&](float value)
    {
        u32 bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        integer(bits, 4);
    };
    auto rectangle = [&](rect r)
    {
        number(r.x);
        number(r.y);
        number(r.w);
        number(r.h);
    };
    integer(kind, 1);
    integer(target->generation, 8);
    integer(target->format, 4);
    integer(target->width, 4);
    integer(target->height, 4);
    bool target_written = false;
    for (auto *t : cache_written)
        if (t == target) target_written = true;
    const bool replaces_target =
        kind == cache_event::CLEAR ||
        (kind == cache_event::COPY && !has_clip && dr && dr->x <= 0 && dr->y <= 0 &&
         dr->right() >= target->width && dr->bottom() >= target->height);
    // LOAD/partial writes depend on the previous contents. An identical alpha
    // draw without a clear must accumulate again rather than becoming a hit.
    integer(target_written || replaces_target ? 0 : target->revision, 8);
    integer(has_clip, 1);
    if (has_clip) rectangle(clip);
    integer(sample ? sample->generation : 0, 8);
    bool written = false;
    for (auto *t : cache_written)
        if (t == sample) written = true;
    integer(sample && !written ? sample->revision : 0, 8);
    integer(sample ? sample->linear : false, 1);
    cache_event event;
    event.type = kind;
    event.target = target;
    event.sample = sample;
    event.clipped = has_clip;
    event.clip = clip;
    event.clear_color = c;
    if (kind == cache_event::CLEAR)
    {
        integer(c.r, 1);
        integer(c.g, 1);
        integer(c.b, 1);
        integer(c.a, 1);
    }
    else if (kind == cache_event::COPY)
    {
        event.source_rect = *sr;
        event.destination_rect = *dr;
        rectangle(*sr);
        rectangle(*dr);
    }
    else
    {
        integer(vc, 4);
        integer(ic, 4);
        static_assert(sizeof(vertex) == 20 && offsetof(vertex, x) == 0 &&
                      offsetof(vertex, y) == 4 && offsetof(vertex, u) == 8 &&
                      offsetof(vertex, v) == 12 && offsetof(vertex, c) == 16);
        static_assert(sizeof(color) == 4 && offsetof(color, r) == 0 && offsetof(color, g) == 1 &&
                      offsetof(color, b) == 2 && offsetof(color, a) == 3);
        const u32 endian = 1;
        const bool packed_little = *reinterpret_cast<const unsigned char *>(&endian) == 1;
        float left = v[0].x, right = left, top = v[0].y, bottom = top;
        for (i32 i = 0; i < vc; ++i)
        {
            if (!std::isfinite(v[i].x) || !std::isfinite(v[i].y) || !std::isfinite(v[i].u) ||
                !std::isfinite(v[i].v))
            {
                ++stats.cache_fallbacks;
                cache_flush(false);
                return false;
            }
            if (!packed_little)
            {
                number(v[i].x);
                number(v[i].y);
                number(v[i].u);
                number(v[i].v);
                integer(v[i].c.r, 1);
                integer(v[i].c.g, 1);
                integer(v[i].c.b, 1);
                integer(v[i].c.a, 1);
            }
            left = min2(left, v[i].x);
            right = max2(right, v[i].x);
            top = min2(top, v[i].y);
            bottom = max2(bottom, v[i].y);
        }
        for (i32 i = 0; i < ic; ++i)
        {
            if (idx[i] < 0 || idx[i] >= vc)
            {
                cache_flush(false);
                return false;
            }
            if (!packed_little) integer(static_cast<u32>(idx[i]), 4);
        }
        if (packed_little)
        {
            // Layout assertions above exclude every padding byte. Copying the
            // packed little-endian fields is exactly the original serialization.
            const usize at = record.canonical.size();
            record.canonical.resize(at + static_cast<usize>(payload));
            std::memcpy(record.canonical.data() + at, v, static_cast<usize>(vc) * 20);
            std::memcpy(record.canonical.data() + at + static_cast<usize>(vc) * 20, idx,
                        static_cast<usize>(ic) * 4);
        }
        record.bounds = rect::make(left, top, right - left, bottom - top);
        event.vertices.assign(v, v + vc);
        event.indices.assign(idx, idx + ic);
    }
    // Hashes are only filters; canonical bytes are always compared after a hit.
    record.hash_a = 1469598103934665603ull;
    record.hash_b = 0x9e3779b97f4a7c15ull;
    usize at = 0;
    const auto mix = [&](u64 value)
    {
        record.hash_a = (record.hash_a ^ value) * 1099511628211ull;
        record.hash_b ^= value;
        record.hash_b = (record.hash_b << 7) | (record.hash_b >> 57);
        record.hash_b *= 0x9e3779b185ebca87ull;
    };
    for (; at + sizeof(u64) <= record.canonical.size(); at += sizeof(u64))
    {
        u64 value = 0;
        std::memcpy(&value, record.canonical.data() + at, sizeof(value));
        mix(value);
    }
    for (; at < record.canonical.size(); ++at) mix(record.canonical[at]);
    cache_bytes += needed;
    cache_written.push_back(target);
    events.push_back(std::move(event));
    records.push_back(std::move(record));
    const u64 usage = cache_usage();
    if (usage > stats.peak_cache_metadata_bytes) stats.peak_cache_metadata_bytes = usage;
    return true;
}
void device::cache_flush(bool final)
{
    if (!cache_active) return;
    const f64 start = wall_ms();
    cache_active = false;
    surface *s = current_surface;
    texture *saved_target = target;
    const bool saved_has_clip = has_clip;
    const rect saved_clip = clip;
    auto equal = cache_records_equal;
    bool identical = final && s->history_valid && records.size() == s->history.size();
    for (usize i = 0; identical && i < records.size(); ++i)
        identical = equal(records[i], s->history[i]);
    bool partial = final && s->history_valid && !records.empty() && !s->history.empty() &&
                   events[0].type == cache_event::CLEAR && events[0].target == s->scene &&
                   equal(records[0], s->history[0]);
    for (usize i = 1; partial && i < events.size(); ++i)
        partial = events[i].type == cache_event::DRAW && events[i].target == s->scene;
    // A previous effect pass cannot be reconstructed from final pixels.
    // History tags distinguish draw-only tapes from clear/copy/effect tapes.
    for (usize i = 1; partial && i < s->history.size(); ++i)
        partial =
            !s->history[i].canonical.empty() && s->history[i].canonical[0] == cache_event::DRAW;
    double left = s->extent.width, top = s->extent.height, right = 0, bottom = 0;
    auto include = [&](rect r)
    {
        const double x0 = static_cast<double>(r.x) - 1, y0 = static_cast<double>(r.y) - 1;
        const double x1 = static_cast<double>(r.x) + r.w + 1,
                     y1 = static_cast<double>(r.y) + r.h + 1;
        if (x0 < left) left = x0;
        if (y0 < top) top = y0;
        if (x1 > right) right = x1;
        if (y1 > bottom) bottom = y1;
    };
    if (partial && !identical)
    {
        const usize count = records.size() > s->history.size() ? records.size() : s->history.size();
        for (usize i = 1; i < count; ++i)
        {
            if (i < records.size() && i < s->history.size() && equal(records[i], s->history[i]))
                continue;
            if (i < records.size()) include(records[i].bounds);
            if (i < s->history.size()) include(s->history[i].bounds);
        }
        left = std::floor(left / 32) * 32;
        top = std::floor(top / 32) * 32;
        right = std::ceil(right / 32) * 32;
        bottom = std::ceil(bottom / 32) * 32;
        if (left < 0) left = 0;
        if (top < 0) top = 0;
        if (right > s->extent.width) right = s->extent.width;
        if (bottom > s->extent.height) bottom = s->extent.height;
        partial = right > left && bottom > top &&
                  (right - left) * (bottom - top) <
                      static_cast<double>(s->extent.width) * s->extent.height * 0.6;
    }
    if (identical)
    {
        ++stats.cache_hits;
        s->cache_full_misses = 0;
    }
    else
    {
        ++stats.cache_misses;
        if (!partial)
            stats.dirty_tiles +=
                static_cast<u64>((s->extent.width + 31) / 32) * ((s->extent.height + 31) / 32);
        usize first = 0;
        if (partial && commands() && start_rendering(s->scene))
        {
            s->cache_full_misses = 0;
            ++stats.partial_replays;
            stats.dirty_tiles += static_cast<u64>((right - left + 31) / 32) *
                                 static_cast<u64>((bottom - top + 31) / 32);
            const color c = events[0].clear_color;
            VkClearAttachment attachment{};
            attachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            attachment.clearValue.color.float32[0] = c.r / 255.0f;
            attachment.clearValue.color.float32[1] = c.g / 255.0f;
            attachment.clearValue.color.float32[2] = c.b / 255.0f;
            attachment.clearValue.color.float32[3] = c.a / 255.0f;
            VkClearRect area{};
            area.rect.offset = {static_cast<i32>(left), static_cast<i32>(top)};
            area.rect.extent = {static_cast<u32>(right - left), static_cast<u32>(bottom - top)};
            area.layerCount = 1;
            vkCmdClearAttachments(frame->command, 1, &attachment, 1, &area);
            replay_clipped = true;
            replay_clip =
                rect::make(static_cast<float>(left), static_cast<float>(top),
                           static_cast<float>(right - left), static_cast<float>(bottom - top));
            ++s->scene->revision;
            first = 1;
        }
        for (usize i = first; i < events.size(); ++i)
        {
            auto &e = events[i];
            set_target(e.target);
            has_clip = e.clipped;
            clip = e.clip;
            if (e.type == cache_event::CLEAR)
                clear(e.clear_color);
            else if (e.type == cache_event::DRAW)
                paint(e.sample, e.vertices.data(), static_cast<i32>(e.vertices.size()),
                      e.indices.data(), static_cast<i32>(e.indices.size()), true);
            else
                blit(e.sample, &e.source_rect, e.target, &e.destination_rect);
        }
        replay_clipped = false;
        const bool large_tape = cache_bytes >= 1024 * 1024;
        if (!partial && ++s->cache_full_misses >= (large_tape ? 2u : 8u))
        {
            s->cache_cooldown = large_tape ? 64u : 16u;
            s->cache_full_misses = 0;
        }
    }
    target = saved_target;
    has_clip = saved_has_clip;
    clip = saved_clip;
    s->history_valid = false;
    if (final && result == VK_SUCCESS)
    {
        s->history = std::move(records);
        s->history_bytes = 0;
        for (auto &r : s->history) s->history_bytes += r.canonical.capacity();
        s->history_bytes += s->history.capacity() * sizeof(cache_record);
        cache_pending = s;
    }
    else
    {
        s->history.clear();
        s->history_bytes = 0;
    }
    events.clear();
    records.clear();
    cache_written.clear();
    cache_bytes = 0;
    stats.total_tiles +=
        static_cast<u64>((s->extent.width + 31) / 32) * ((s->extent.height + 31) / 32);
    stats.cache_ms += wall_ms() - start;
}
void device::cache_commit(bool success)
{
    if (cache_pending) cache_pending->history_valid = success && result == VK_SUCCESS;
    cache_pending = nullptr;
}
} // namespace vk_detail
#endif
