// Native shadow encoder for the private GPU command tape.
// Retains the original transformed mesh as an exact expansion oracle.
// Diagnostic mode only: the established raster path still paints every draw.
// Included inside namespace pui by the one implementation translation unit.
#if defined(PUFFERUI_ENABLE_VULKAN)
namespace vk_detail
{
void primitive_recorder::invalidate()
{
    tape.valid = false;
    if (!reported_overflow && owner)
    {
        reported_overflow = true;
        ++owner->stats.shadow_overflows;
    }
}
void primitive_recorder::reset()
{
    depth = 0;
    reported_overflow = false;
    pending_shape = 0xffffffffu;
    if (owner->options.painter == vulkan_painter_mode::COMPUTE_SDF)
    {
        const u64 budget = owner->options.gpu_command_budget_bytes;
        const u64 limit = budget / sizeof(paint_detail::command);
        owner->sdf_tape.prepare(static_cast<usize>(limit < 65536 ? limit : 65536), 0, budget);
    }
    if (owner->options.painter != vulkan_painter_mode::COMMAND_SHADOW) return;
    const u64 budget = owner->options.command_budget_bytes;
    const u64 available = budget / (sizeof(paint_detail::command) * 4);
    const u64 count = available < 65536 ? available : 65536;
    const u64 bytes = budget - count * sizeof(paint_detail::command);
    tape.valid = false;
    if (!tape.prepare(static_cast<usize>(count), static_cast<usize>(bytes), budget)) invalidate();
}
void primitive_recorder::enter(const paint_detail::primitive_input &input)
{
    if (depth < 8)
        stack[depth] = input;
    else
        invalidate();
    ++depth;
}
void primitive_recorder::leave()
{
    if (depth)
        --depth;
    else
        invalidate();
}
void primitive_recorder::mesh(texture_handle handle, const vertex *v, i32 vc, const i32 *idx,
                              i32 ic)
{
    if (owner && owner->options.painter == vulkan_painter_mode::SDF_RASTER)
    {
        pending_shape = 0xffffffffu;
        return; // independent raster oracle needs shapes/spans, never a command tape
    }
    if (owner && owner->options.painter == vulkan_painter_mode::COMPUTE_SDF)
    {
        const u32 shape = pending_shape;
        pending_shape = 0xffffffffu; // consume even when the bounded tape is already invalid
        if (!owner->sdf_tape.valid || !v || vc <= 0) return;
        paint_detail::command command;
        command.x0 = command.x1 = v[0].x;
        command.y0 = command.y1 = v[0].y;
        for (i32 i = 0; i < vc; ++i)
        {
            if (!std::isfinite(v[i].x) || !std::isfinite(v[i].y))
            {
                owner->sdf_tape.valid = false;
                return;
            }
            command.x0 = min2(command.x0, v[i].x);
            command.y0 = min2(command.y0, v[i].y);
            command.x1 = max2(command.x1, v[i].x);
            command.y1 = max2(command.y1, v[i].y);
        }
        if (shape != 0xffffffffu)
        {
            command.operation = 2;
            command.resource = shape;
        }
        else if (depth && depth <= 8 &&
                 stack[depth - 1].operation == paint_detail::opcode::RECT_COMPAT)
            command.operation = 1;
        command.flags = static_cast<u32>(v[0].c.r) | (static_cast<u32>(v[0].c.g) << 8) |
                        (static_cast<u32>(v[0].c.b) << 16) | (static_cast<u32>(v[0].c.a) << 24);
        if (command.operation == 1)
        {
            std::memcpy(&command.resource, &command.x0, 4);
            std::memcpy(&command.clip, &command.y0, 4);
            std::memcpy(&command.pass, &command.x1, 4);
            std::memcpy(&command.compatibility, &command.y1, 4);
        }
        // Raster subpixel snapping can cover a sample just outside the exact
        // float AABB. Keep a conservative physical-pixel guard for every opcode.
        command.x0 -= 1;
        command.y0 -= 1;
        command.x1 += 1;
        command.y1 += 1;
        owner->sdf_tape.append(command, {});
        return;
    }
    if (!tape.valid || !owner) return;
    const u32 little = 1;
    if (*reinterpret_cast<const u8 *>(&little) != 1 || !v || !idx || vc <= 0 || ic <= 0)
    {
        invalidate();
        return;
    }
    static_assert(sizeof(vertex) == 20 && offsetof(vertex, c) == 16 && sizeof(color) == 4);
    auto bits = [](f32 value)
    {
        u32 result;
        std::memcpy(&result, &value, sizeof(result));
        return result;
    };
    const auto *target = owner->target;
    const auto *sample = static_cast<const texture *>(handle);
    const paint_detail::primitive_input input =
        depth && depth <= 8 ? stack[depth - 1] : paint_detail::primitive_input{};
    u32 meta[32]{};
    meta[0] = static_cast<u32>(vc);
    meta[1] = static_cast<u32>(ic);
    meta[2] = paint_detail::SCHEMA_VERSION;
    meta[3] = static_cast<u32>(input.operation);
    auto serial = [&](u32 at, u64 value)
    {
        meta[at] = static_cast<u32>(value);
        meta[at + 1] = static_cast<u32>(value >> 32);
    };
    serial(4, target ? target->generation : 0);
    serial(6, target ? target->revision : 0);
    serial(8, sample ? sample->generation : 0);
    serial(10, sample ? sample->revision : 0);
    meta[12] = sample && sample->linear;
    meta[13] = owner->has_clip;
    const rect clip = owner->has_clip ? owner->clip : rect{};
    meta[14] = bits(clip.x);
    meta[15] = bits(clip.y);
    meta[16] = bits(clip.w);
    meta[17] = bits(clip.h);
    meta[18] = bits(input.shape.x);
    meta[19] = bits(input.shape.y);
    meta[20] = bits(input.shape.w);
    meta[21] = bits(input.shape.h);
    meta[22] = bits(input.radii.tl);
    meta[23] = bits(input.radii.tr);
    meta[24] = bits(input.radii.br);
    meta[25] = bits(input.radii.bl);
    meta[26] = static_cast<u32>(input.tint.r) | (static_cast<u32>(input.tint.g) << 8) |
               (static_cast<u32>(input.tint.b) << 16) | (static_cast<u32>(input.tint.a) << 24);
    meta[27] = bits(input.scale);
    meta[28] = bits(input.disabled_alpha);
    paint_detail::command c;
    c.operation = meta[3];
    c.x0 = c.x1 = v[0].x;
    c.y0 = c.y1 = v[0].y;
    for (i32 i = 0; i < vc; ++i)
    {
        if (!std::isfinite(v[i].x) || !std::isfinite(v[i].y) || !std::isfinite(v[i].u) ||
            !std::isfinite(v[i].v))
        {
            invalidate();
            return;
        }
        c.x0 = min2(c.x0, v[i].x);
        c.y0 = min2(c.y0, v[i].y);
        c.x1 = max2(c.x1, v[i].x);
        c.y1 = max2(c.y1, v[i].y);
    }
    const bool recorded =
        tape.append_parts(c, {std::span<const u8>(reinterpret_cast<const u8 *>(meta), sizeof(meta)),
                              std::span<const u8>(reinterpret_cast<const u8 *>(v),
                                                  static_cast<usize>(vc) * sizeof(vertex)),
                              std::span<const u8>(reinterpret_cast<const u8 *>(idx),
                                                  static_cast<usize>(ic) * sizeof(i32))});
    if (recorded)
    {
        ++owner->stats.shadow_commands;
        owner->stats.shadow_payload_bytes += tape.commands.back().payload_size;
    }
    else
        invalidate();
}
bool primitive_recorder::shape(context *c, const paint_detail::primitive_input &input)
{
    if (!owner || !paint_detail::sdf_painter(owner->options.painter) ||
        (input.operation != paint_detail::opcode::ROUNDED_FAN_COMPAT &&
         input.operation != paint_detail::opcode::ROUNDED_GRADIENT) ||
        !c || !c->dl || !std::isfinite(input.scale) || input.scale <= 0 || !input.shape.is_valid())
        return false;
    const auto &r = input.shape;
    const auto &a = input.radii;
    if (!std::isfinite(r.x) || !std::isfinite(r.y) || !std::isfinite(r.w) || !std::isfinite(r.h) ||
        !std::isfinite(a.tl) || !std::isfinite(a.tr) || !std::isfinite(a.br) ||
        !std::isfinite(a.bl))
        return false;
    texture_handle texture =
        c->ts && !c->ts->atlases.empty() ? c->ts->atlases.front().tex : nullptr;
    const rect *clip = c->clip_depth ? &c->clip_stack[c->clip_depth - 1] : nullptr;
    detail::dl_prepare(c, texture, clip);
    if (owner->sdf_shapes.size() >=
        owner->properties.limits.maxStorageBufferRange / sizeof(sdf_shape))
        return false;
    const f32 s = input.scale;
    sdf_shape shape{
        {r.x * s, r.y * s, r.w * s, r.h * s}, {a.tl * s, a.tr * s, a.br * s, a.bl * s}, s};
    const bool gradient = input.operation == paint_detail::opcode::ROUNDED_GRADIENT;
    if (gradient)
    {
        shape.reserved[0] = 2;
        for (u32 i = 0; i < 4; ++i)
        {
            auto tint = input.corners[i];
            tint.a = static_cast<u8>(tint.a * input.disabled_alpha);
            shape.corners[i] = static_cast<u32>(tint.r) | (static_cast<u32>(tint.g) << 8) |
                               (static_cast<u32>(tint.b) << 16) | (static_cast<u32>(tint.a) << 24);
        }
    }
    const rect box = r.pad(-1.0f);
    const color tint = input.tint;
    const vertex v[] = {{box.x, box.y, 0, 0, tint},
                        {box.right(), box.y, 0, 0, tint},
                        {box.right(), box.bottom(), 0, 0, tint},
                        {box.x, box.bottom(), 0, 0, tint}};
    constexpr i32 indices[] = {0, 1, 2, 0, 2, 3};
    const usize base = c->dl->verts.size();
    owner->sdf_shapes.push_back(shape);
    owner->sdf_spans.push_back({base, static_cast<u32>(owner->sdf_shapes.size() - 1)});
    pending_shape = static_cast<u32>(owner->sdf_shapes.size() - 1);
    detail::dl_add(c, v, 4, indices, 6);
    ++owner->stats.sdf_shapes;
    owner->stats.sdf_avoided_vertices += gradient ? 25 : 69;
    owner->stats.sdf_avoided_indices += gradient ? 78 : 318;
    return true;
}
bool primitive_recorder::vector_text() const
{
    return owner && owner->options.vector_text;
}
bool primitive_recorder::glyph(context *c, font_handle font, u32 index, vec2 baseline, f32 scale,
                               color tint)
{
#if defined(PUFFERUI_ENABLE_HARFBUZZ)
    if (!vector_text() || !c || !c->dl || !std::isfinite(scale) || scale <= 0) return false;
    auto *outline = detail::harfbuzz_outline(c, font, index);
    if (!outline) return false;
    if (outline->data.empty()) return true;
    const auto &e = outline->extents;
    const rect box{baseline.x + e.x_bearing * scale - 1, baseline.y - e.y_bearing * scale - 1,
                   e.width * scale + 2, -e.height * scale + 2};
    if (detail::outside_paint(c, box)) return true;
    const rect *clip = c->clip_depth ? &c->clip_stack[c->clip_depth - 1] : nullptr;
    detail::dl_prepare(c, nullptr, clip);
    if (!outline->uploaded)
    {
        constexpr usize limit = 16 * 1024 * 1024;
        if (outline->data.size() > limit || owner->slug_data.size() > limit - outline->data.size())
            return owner->fail("Slug outline storage budget exhausted");
        outline->location = static_cast<u32>(owner->slug_data.size() / 8);
        owner->slug_data.insert(owner->slug_data.end(), outline->data.begin(), outline->data.end());
        while (owner->slug_data.size() % 8) owner->slug_data.push_back(0);
        outline->uploaded = true;
    }
    const f32 density = c->render_scale;
    sdf_shape shape;
    shape.bounds = {box.x * density, box.y * density, box.w * density, box.h * density};
    shape.radii = {baseline.x * density, baseline.y * density, scale * density, 0};
    std::memcpy(&shape.radii.bl, &outline->location, sizeof(outline->location));
    shape.reserved[0] = 1; // GPU Slug glyph, distinct from rounded SDF shapes
    shape.reserved[1] = c->active_theme.text_gamma;
    const vertex v[] = {{box.x, box.y, 0, 0, tint},
                        {box.right(), box.y, 0, 0, tint},
                        {box.right(), box.bottom(), 0, 0, tint},
                        {box.x, box.bottom(), 0, 0, tint}};
    constexpr i32 indices[] = {0, 1, 2, 0, 2, 3};
    owner->sdf_shapes.push_back(shape);
    owner->sdf_spans.push_back(
        {c->dl->verts.size(), static_cast<u32>(owner->sdf_shapes.size() - 1)});
    pending_shape = static_cast<u32>(owner->sdf_shapes.size() - 1);
    detail::dl_add(c, v, 4, indices, 6);
    return true;
#else
    (void)c;
    (void)font;
    (void)index;
    (void)baseline;
    (void)scale;
    (void)tint;
    return false;
#endif
}
} // namespace vk_detail
bool vulkan_try_rounded_gradient(ui &u, const vulkan_rounded_gradient_opts &opts)
{
    auto *c = u.ctx;
    auto *d = c ? dynamic_cast<vk_detail::device *>(c->device) : nullptr;
    if (!d || !paint_detail::sdf_painter(d->options.painter) || !c->dl || !c->current_window ||
        c->overlay_depth || !opts.area.is_valid() || !std::isfinite(opts.area.x) ||
        !std::isfinite(opts.area.y) || !std::isfinite(opts.area.w) || !std::isfinite(opts.area.h) ||
        !std::isfinite(c->render_scale) || c->render_scale <= 0 ||
        d->sdf_shapes.size() + 2 >
            d->properties.limits.maxStorageBufferRange / sizeof(vk_detail::sdf_shape))
        return false;
    if (opts.area.w <= 0 || opts.area.h <= 0) return true;
    if (detail::outside_paint_feathered(c, opts.area)) return true;
    const auto radii = detail::clamp_radii(opts.area, opts.radii);
    if (!std::isfinite(radii.tl) || !std::isfinite(radii.tr) || !std::isfinite(radii.br) ||
        !std::isfinite(radii.bl))
        return false;
    u.draw_rounded_rect(opts.area, opts.underlay, radii);
    paint_detail::primitive_input input;
    input.operation = paint_detail::opcode::ROUNDED_GRADIENT;
    input.shape = opts.area;
    input.radii = radii;
    input.tint = {255, 255, 255, 255};
    input.scale = c->render_scale;
    input.disabled_alpha = c->disabled_depth > 0 ? clampf(c->disabled_alpha, 0, 1) : 1;
    input.corners[0] = opts.top_left;
    input.corners[1] = opts.top_right;
    input.corners[2] = opts.bottom_right;
    input.corners[3] = opts.bottom_left;
    if (!d->primitive_commands.shape(c, input))
        d->fail("analytic gradient could not record its complete program");
    return true;
}
namespace paint_detail
{
bool vulkan_vector_text(render_device *device)
{
    auto *native = dynamic_cast<vk_detail::device *>(device);
    return native && native->options.vector_text;
}
primitive_sink *vulkan_primitive_sink(render_device *device)
{
    auto *native = dynamic_cast<vk_detail::device *>(device);
    if (!native || native->options.painter == vulkan_painter_mode::REFERENCE) return nullptr;
    return &native->primitive_commands;
}
} // namespace paint_detail
#endif
