// Bounded isolated-scene tiles. Included inside vk_detail after vector_packet.
// All Slug/paint operands are unchanged; integer origins preserve the absolute
// pixel center. Independent layers may be grouped; final source-over composites
// retain application order, including overlapping destinations.
inline rect vector_group_roi(rect bounds, rect viewport, affine2 matrix, VkRect2D scissor)
{
    bounds = rect::intersect(
        vector_pixel_bounds(rect::intersect(bounds, viewport), matrix),
        {static_cast<f32>(scissor.offset.x), static_cast<f32>(scissor.offset.y),
         static_cast<f32>(scissor.extent.width), static_cast<f32>(scissor.extent.height)});
    if (bounds.w <= 0 || bounds.h <= 0) return {};
    const f32 left = std::floor(bounds.x), top = std::floor(bounds.y);
    return {left, top, std::ceil(bounds.right()) - left, std::ceil(bounds.bottom()) - top};
}
bool native_vectors::queue_scene(vector_asset &asset, const scene_instance &instance,
                                 VkRect2D scissor)
{
    if (!pending.empty() ||
        (scene_count && (pending_asset != &asset || scene_count == layer_columns * layer_columns ||
                         std::memcmp(&pending_scissor, &scissor, sizeof(scissor)) != 0)))
        if (!flush()) return false;
    pending_asset = &asset;
    pending_scissor = scissor;
    scenes[scene_count++] = instance;
    return true;
}
bool native_vectors::draw_scenes()
{
    auto &asset = *pending_asset;
    struct restore_state
    {
        device &dev;
        texture *target;
        bool clipped;
        rect clip;
        ~restore_state()
        {
            dev.set_target(target);
            dev.set_clip(clipped ? &clip : nullptr);
        }
    } restore{dev, dev.target, dev.has_clip, dev.clip};
    const auto tile = [&](usize i) -> vec2
    {
        return {static_cast<f32>((i % layer_columns) * layer_side),
                static_cast<f32>((i / layer_columns) * layer_side)};
    };
    const auto origin = [&](usize depth, usize i) -> vec2
    {
        const auto offset = tile(i);
        return {group_rois[depth][i].x - offset.x, group_rois[depth][i].y - offset.y};
    };
    const VkRect2D atlas_scissor{
        {0, 0}, {static_cast<u32>(layers[0]->width), static_cast<u32>(layers[0]->height)}};
    const auto clear = [&](usize depth) -> bool
    {
        dev.set_target(layers[depth]);
        dev.set_clip(nullptr);
        VkClearRect regions[SCENE_BATCH]{};
        u32 count = 0;
        for (usize i = 0; i < scene_count; ++i)
        {
            const auto roi = group_rois[depth][i];
            if (roi.w <= 0 || roi.h <= 0) continue;
            if (roi.w > layer_side || roi.h > layer_side) return false;
            const auto offset = tile(i);
            regions[count++] = {{{static_cast<i32>(offset.x), static_cast<i32>(offset.y)},
                                 {static_cast<u32>(roi.w), static_cast<u32>(roi.h)}},
                                0,
                                1};
        }
        if (!count) return true;
        if (!dev.start_rendering(layers[depth])) return false;
        VkClearAttachment attachment{};
        attachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        dev.vkCmdClearAttachments(dev.frame->command, 1, &attachment, count, regions);
        layers[depth]->last_use = dev.next_serial;
        ++layers[depth]->revision;
        return true;
    };
    vector_instance packets[SCENE_BATCH]{};
    usize skip_until[SCENE_BATCH]{};
    for (usize i = 0; i < scene_count; ++i) group_rois[0][i] = scenes[i].bounds;
    if (!clear(0)) return false;
    usize depth = 0;
    for (usize step_index = 0; step_index < asset.scene.steps.size(); ++step_index)
    {
        const auto &step = asset.scene.steps[step_index];
        if (step.type == vector_step::PUSH)
        {
            if (++depth >= layers.size() || depth >= 66) return false;
            for (usize i = 0; i < scene_count; ++i)
            {
                group_rois[depth][i] = {};
                if (step_index < skip_until[i]) continue;
                const auto &item = scenes[i];
                auto roi =
                    vector_group_roi(step.bounds, item.viewport, item.matrix, pending_scissor);
                if (step.opacity == 0 || step.clip == VECTOR_EMPTY_CLIP || roi.w <= 0 || roi.h <= 0)
                {
                    skip_until[i] = step.pair + 1ull;
                    continue;
                }
                group_rois[depth][i] = roi;
            }
            if (!clear(depth)) return false;
            continue;
        }
        const bool composite = step.type == vector_step::POP;
        if (composite && !depth) return false;
        const usize child = depth;
        if (composite) --depth;
        dev.set_target(layers[depth]);
        usize count = 0;
        for (usize i = 0; i < scene_count; ++i)
        {
            if (step_index < skip_until[i]) continue;
            const auto &item = scenes[i];
            auto bounds =
                composite ? group_rois[child][i] : vector_pixel_bounds(step.bounds, item.matrix);
            bounds = rect::intersect(bounds, group_rois[depth][i]);
            if (bounds.w <= 0 || bounds.h <= 0) continue;
            auto tint = composite ? color::white() : item.tint;
            tint.a = 255;
            auto packet = vector_packet(
                bounds, item.inverse, item.viewport, tint, item.current, origin(depth, i),
                composite ? origin(child, i) : vec2{}, composite ? VECTOR_NO_CLIP : step.path,
                composite ? VECTOR_NO_CLIP : step.paint, step.clip, composite ? 1u : 0u);
            if (composite) packet.tint[3] = step.opacity;
            packets[count++] = packet;
        }
        if (!graphics.draw(composite ? layers[child] : dev.white, asset.buffer.descriptor, packets,
                           count, atlas_scissor))
            return false;
    }
    if (depth) return false;
    // One instanced draw preserves the primitive/submission order for blending.
    dev.set_target(restore.target);
    for (usize i = 0; i < scene_count; ++i)
    {
        const auto &item = scenes[i];
        packets[i] =
            vector_packet(item.bounds, item.inverse, item.viewport, color::white(), item.current,
                          {}, origin(0, i), VECTOR_NO_CLIP, VECTOR_NO_CLIP, VECTOR_NO_CLIP, 1);
        packets[i].tint[3] = item.tint.a / 255.0f;
    }
    return graphics.draw(layers[0], asset.buffer.descriptor, packets, scene_count, pending_scissor);
}
