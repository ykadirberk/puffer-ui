// Exact native program reuse. Metadata and CPU payload arenas are prepared per
// surface. Comparisons read ordinary CPU memory rather than mapped GPU pages.
// A hit discards this frame's unsubmitted GPU tape, preserving every output.
// Unsupported resource side effects and bounded-capacity exhaustion render fully.
#if defined(PUFFERUI_ENABLE_VULKAN)
namespace vk_detail
{
// Glyph cache cell/generation tags are acceleration state. Bounds, outline,
// scale and gamma remain authoritative; an expired tag paints original Slug.
bool equivalent_shape_sources(const u8 *current, const u8 *previous, usize bytes)
{
    if (!std::memcmp(current, previous, bytes)) return true;
    if (bytes % sizeof(sdf_shape)) return false;
    for (usize at = 0; at < bytes; at += sizeof(sdf_shape))
    {
        if (std::memcmp(current + at, previous + at, 48)) return false;
        f32 kind;
        std::memcpy(&kind, current + at + 36, sizeof(kind));
        if (kind != 1 && std::memcmp(current + at + 48, previous + at + 48, 16)) return false;
    }
    return true;
}
void device::retained_begin(surface &s)
{
    retaining = retained_previous = nullptr;
    retained_equal = retained_deferred = false;
    if (!options.whole_program_cache || !paint_detail::sdf_painter(options.painter) ||
        !s.program_cache || !s.retained[0] || !s.scene)
        return;
    auto &resources = *s.program_cache;
    if (resources.retry_in)
    {
        // Repeatedly changing pages cannot reuse a whole program. Draw them
        // normally without copying another canonical CPU tape every frame.
        // Periodically observe consecutive frames so a settled page recovers.
        --resources.retry_in;
        resources.warm = resources.recording = false;
        return;
    }
    auto &now = *s.retained[s.retained_bank];
    auto &previous = *s.retained[1 - s.retained_bank];
    now.draw_count = now.image_count = 0;
    now.payload_used = 0;
    now.serial = next_serial;
    now.valid = true;
    now.draws_before = stats.draws;
    now.vertices_before = stats.vertices;
    now.indices_before = stats.indices;
    now.batches_before = stats.gpu_sdf_batches;
    now.kawase_before = stats.kawase_passes;
    now.kawase_pixels_before = stats.kawase_pixels;
    retaining = &now;
    retained_previous = &previous;
    retained_equal = previous.valid;
    // LOAD dependencies also appear in event keys. Validate written outputs:
    // outside writes between frames must never be hidden by an identical clear.
    for (usize i = 0; retained_equal && i < previous.image_count; ++i)
    {
        const auto &image = previous.images[i];
        if (image.writer)
            retained_equal = valid(image.image) && image.image->generation == image.generation &&
                             image.image->revision == image.final_revision;
    }
    retained_probe_begin(s);
    retained_deferred =
        (options.painter != vulkan_painter_mode::INTEGRATED || options.native_observation) &&
        retained_equal && retained_confirmed(s);
    // Native effect kernels keep the general record/discard route. The cheap
    // prefix replay below supports complete clear and raster operations only.
    for (usize i = 0; retained_deferred && i < previous.draw_count; ++i)
        retained_deferred = previous.draws[i].key[0] == 1 || previous.draws[i].key[0] == 2;
}
void device::retained_materialize()
{
    if (!retained_deferred || !retaining) return;
    retained_deferred = false;
    auto *program = retaining;
    retaining = nullptr;
    const auto saved_target = target;
    const auto saved_clip = clip;
    const auto saved_has_clip = has_clip;
    // Skipped operations already advanced logical revisions. Replaying them
    // must advance those revisions exactly once, as an ordinary execution does.
    for (usize i = 0; i < program->image_count; ++i)
        if (program->images[i].writer)
            program->images[i].image->revision = program->images[i].revision;
    const auto image_for = [&](u64 generation) -> texture *
    {
        for (usize i = 0; i < program->image_count; ++i)
            if (program->images[i].generation == generation) return program->images[i].image;
        return nullptr;
    };
    for (usize i = 0; i < program->draw_count && result == VK_SUCCESS; ++i)
    {
        const auto &event = program->draws[i];
        set_target(image_for(event.key[1]));
        if (event.key[0] == 1)
        {
            color value;
            std::memcpy(&value, &event.key[6], sizeof(value));
            clear(value);
        }
        else
        {
            VkRect2D bounds;
            std::memcpy(&bounds, &event.key[6], sizeof(bounds));
            clip = {static_cast<f32>(bounds.offset.x), static_cast<f32>(bounds.offset.y),
                    static_cast<f32>(bounds.extent.width), static_cast<f32>(bounds.extent.height)};
            has_clip = true;
            paint(image_for(event.key[2]), reinterpret_cast<const vertex *>(event.vertices),
                  static_cast<i32>(event.vertex_count),
                  reinterpret_cast<const i32 *>(event.indices),
                  static_cast<i32>(event.index_bytes / sizeof(i32)), event.key[8] != 0, &event);
        }
    }
    end_rendering();
    target = saved_target;
    clip = saved_clip;
    has_clip = saved_has_clip;
    retaining = program;
}
retained_texture *device::retained_touch(texture *image)
{
    if (!retaining || !retaining->valid || !image || image->intrinsic) return nullptr;
    if (image->external || image->ready)
    {
        retained_abort();
        return nullptr;
    }
    for (usize i = 0; i < retaining->image_count; ++i)
        if (retaining->images[i].image == image) return &retaining->images[i];
    if (retaining->image_count == retained_program::MAX_IMAGES)
    {
        retained_abort();
        return nullptr;
    }
    auto &entry = retaining->images[retaining->image_count++];
    entry = {image, image->generation, image->revision, 0, image->layout, 0};
    return &entry;
}
void device::retained_add(retained_draw event, texture *output, texture *input, bool replaces)
{
    if (!retaining || !retaining->valid) return;
    if (event.key[0] != 1 && event.key[0] != 2) retained_materialize();
    auto *destination = retained_touch(output);
    auto *source = retained_touch(input);
    if (!destination || !retaining->valid) return;
    if (retaining->draw_count == retained_program::MAX_DRAWS)
    {
        retained_abort();
        return;
    }
    const usize vertex_bytes = static_cast<usize>(event.vertex_count) * event.stride;
    const usize bytes = vertex_bytes + event.index_bytes + event.shape_bytes + event.span_bytes;
    if (bytes > retaining->payload_capacity - retaining->payload_used)
    {
        retained_abort();
        return;
    }
    event.key[1] = destination->generation;
    event.key[2] = source ? source->generation : 0;
    const u32 shape_origin = event.key[0] == 2 ? static_cast<u32>(event.key[9]) : 0;
    if (event.key[0] == 2) event.key[9] = 0; // canonical operation-local spans
    // Geometry equality cannot establish fixed-point state for partial writes
    // and LOAD/blur feedback. The separate GPU certificate covers every output.
    event.key[3] = 0;
    (void)replaces;
    event.key[4] = source ? (source->writer ? source->writer : source->revision) : 0;
    // Separate a producer ordinal from an external content revision.
    event.key[5] = source ? (source->writer ? 2 : 0) | (input->linear ? 1 : 0) : 0;
    if (source && !source->writer) source->read_before_write = true;
    if (!destination->writer && !destination->read_before_write)
        destination->reset = event.key[0] == 1 || (event.key[0] == 2 && replaces);
    ++destination->writer;
    if (retained_equal)
    {
        retained_equal = retaining->draw_count < retained_previous->draw_count;
        if (retained_equal)
        {
            const auto &old = retained_previous->draws[retaining->draw_count];
            retained_equal = !std::memcmp(event.key, old.key, sizeof(event.key)) &&
                             event.vertex_count == old.vertex_count &&
                             event.index_bytes == old.index_bytes &&
                             event.shape_bytes == old.shape_bytes && event.stride == old.stride &&
                             event.span_bytes == old.span_bytes;
            if (retained_equal && vertex_bytes)
                retained_equal = !std::memcmp(event.vertices, old.vertices, vertex_bytes);
            if (retained_equal && event.index_bytes)
                retained_equal = !std::memcmp(event.indices, old.indices, event.index_bytes);
            if (retained_equal && event.shape_bytes)
                retained_equal =
                    equivalent_shape_sources(event.shapes, old.shapes, event.shape_bytes);
            for (usize i = 0; retained_equal && i < event.span_bytes / 12; ++i)
            {
                u64 vertex;
                u32 shape;
                std::memcpy(&vertex, old.spans + i * 12, 8);
                std::memcpy(&shape, old.spans + i * 12 + 8, 4);
                retained_equal =
                    vertex == sdf_spans[i].vertex && shape == sdf_spans[i].shape - shape_origin;
            }
            if (retained_equal)
            {
                event.vertices = old.vertices;
                event.indices = old.indices;
                event.shapes = old.shapes;
                event.spans = old.spans;
            }
        }
    }
    if (!retained_equal)
    {
        retained_materialize();
        // Equal prefixes can keep either prepared arena's canonical bytes. A
        // first mismatch writes only after that prefix and disables all further
        // comparisons, so recycling an arena cannot overwrite a live prefix.
        u8 *payload = retaining->payload + retaining->payload_used;
        const auto copy = [&](const u8 *input, usize size, const u8 *&output)
        {
            if (size) std::memcpy(payload, input, size);
            output = payload;
            payload += size;
        };
        copy(event.vertices, vertex_bytes, event.vertices);
        copy(event.indices, event.index_bytes, event.indices);
        copy(event.shapes, event.shape_bytes, event.shapes);
        event.spans = payload;
        for (usize i = 0; i < event.span_bytes / 12; ++i)
        {
            const u64 vertex = sdf_spans[i].vertex;
            const u32 shape = sdf_spans[i].shape - shape_origin;
            std::memcpy(payload, &vertex, 8);
            std::memcpy(payload + 8, &shape, 4);
            payload += 12;
        }
    }
    retaining->payload_used += bytes;
    retaining->draws[retaining->draw_count++] = event;
}
bool device::retained_finish(surface &s)
{
    if (!retaining) return true;
    auto &now = *retaining;
    const bool same = now.valid && retained_equal && now.serial == next_serial &&
                      now.draw_count == retained_previous->draw_count;
    if (!same) retained_materialize();
    retaining = nullptr; // Present, readback and submission are outside the program.
    auto &resources = *s.program_cache;
    resources.warm = same;
    if (same)
    {
        resources.changing_frames = 0;
        resources.invalid_observations = 0;
    }
    else if (options.painter == vulkan_painter_mode::INTEGRATED &&
             (!now.valid || !retained_previous->valid))
    {
        // Initialization/upload side effects and the first valid baseline are
        // not observations of a persistently changing program. Retry a cold
        // invalid frame promptly; repeated unsupported inputs still back off.
        resources.changing_frames = 0;
        if (!now.valid)
            resources.retry_in = ++resources.invalid_observations < 3 ? 1u : 120u;
        else
            resources.invalid_observations = 0;
    }
    else if (++resources.changing_frames ==
             (options.painter == vulkan_painter_mode::INTEGRATED ? 2u : 8u))
    {
        resources.changing_frames = 0;
        resources.retry_in = 120;
    }
    if (!same)
    {
        resources.partial_reuse = false;
        if (++resources.epoch == 0)
        {
            resources.epoch = 1;
            for (auto &probe : resources.probes) probe.serial = 0;
        }
        resources.confirmed = 0;
        resources.probes[s.slot].serial = 0;
    }
    const bool hit = same && retained_confirmed(s);
    if (hit)
    {
        if (retained_deferred)
        {
            // Nothing except the prepared timestamp prefix was recorded. Keep
            // that tape and append presentation without resetting the slot twice.
            retained_deferred = false;
            ++stats.whole_program_hits;
        }
        else
        {
            end_rendering();
            // These layouts describe the discarded tape. Restore actual layouts
            // before recording the sole remaining operation: presentation.
            for (usize i = 0; i < now.image_count; ++i)
                now.images[i].image->layout = now.images[i].layout;
            for (auto *job : frame->sdf_jobs)
            {
                job->published_bank = -1;
                job->bin.serial = 0;
                job->bin.recorded = false;
            }
            frame->sdf_jobs.clear();
            frame->recording = false;
            stats.draws = now.draws_before;
            stats.vertices = now.vertices_before;
            stats.indices = now.indices_before;
            stats.gpu_sdf_batches = now.batches_before;
            stats.kawase_passes = now.kawase_before;
            stats.kawase_pixels = now.kawase_pixels_before;
            if (!begin(*frame))
            {
                now.valid = false;
                return false;
            }
            ++stats.whole_program_hits;
        }
    }
    else
    {
        ++stats.whole_program_misses;
        if (same && !retained_probe_end(s))
        {
            now.valid = false;
            return false;
        }
    }
    now.valid = now.valid && now.serial == next_serial;
    if (now.valid)
        for (usize i = 0; i < now.image_count; ++i)
            now.images[i].final_revision = now.images[i].image->revision;
    s.retained_bank = 1 - s.retained_bank;
    return true;
}
} // namespace vk_detail
#endif
