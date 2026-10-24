// Optional HarfBuzz shaping and GPU outline cache.
// Font bytes stay owned by text_store; hb objects are destroyed before them.
// Included inside namespace pui, before the text layout implementation.
// No dependency or runtime branch is added to core/SDL builds.
#if defined(PUFFERUI_ENABLE_HARFBUZZ)
namespace detail
{
inline hb_font_t *harfbuzz_font(context *c, font_handle font)
{
    if (!c->ts || font < 0 || static_cast<usize>(font) >= c->ts->fonts.size()) return nullptr;
    auto &f = c->ts->fonts[static_cast<usize>(font)];
    if (!f.ok || f.buffer.size() > 0xffffffffu) return nullptr;
    if (!f.harfbuzz) f.harfbuzz = new harfbuzz_font_data();
    auto &state = *f.harfbuzz;
    if (!state.shaping)
    {
        auto *blob = hb_blob_create(reinterpret_cast<const char *>(f.buffer.data()),
                                    static_cast<unsigned>(f.buffer.size()), HB_MEMORY_MODE_READONLY,
                                    nullptr, nullptr);
        auto *face = hb_face_create(blob, 0);
        hb_blob_destroy(blob);
        state.shaping = hb_font_create(face);
        const unsigned upem = hb_face_get_upem(face);
        hb_face_destroy(face);
        if (!upem || upem > 32767)
        {
            hb_font_destroy(state.shaping);
            state.shaping = nullptr;
            return nullptr;
        }
        hb_font_set_scale(state.shaping, static_cast<int>(upem), static_cast<int>(upem));
        hb_ot_font_set_funcs(state.shaping);
    }
    return state.shaping;
}
inline font_data::outline *harfbuzz_outline(context *c, font_handle font, u32 index)
{
    auto *native = harfbuzz_font(c, font);
    if (!native) return nullptr;
    auto &f = *c->ts->fonts[static_cast<usize>(font)].harfbuzz;
    if (auto found = f.outlines.find(index); found != f.outlines.end()) return &found->second;
    if (!f.encoder) f.encoder = hb_gpu_draw_create_or_fail();
    if (!f.encoder) return nullptr;
    hb_gpu_draw_clear(f.encoder);
    if (!hb_gpu_draw_glyph_or_fail(f.encoder, native, index)) return nullptr;
    font_data::outline value;
    auto *blob = hb_gpu_draw_encode(f.encoder, &value.extents);
    unsigned bytes = 0;
    const char *data = hb_blob_get_data(blob, &bytes);
    if (bytes)
        value.data.assign(reinterpret_cast<const u8 *>(data),
                          reinterpret_cast<const u8 *>(data) + bytes);
    hb_blob_destroy(blob);
    return &f.outlines.emplace(index, std::move(value)).first->second;
}
inline bool harfbuzz_layout(context *c, font_handle font, f32 size, std::string_view text,
                            text_store::text_layout &result)
{
    if (text.size() > 0x7fffffffu) return false;
    auto *native = harfbuzz_font(c, font);
    if (!native) return false;
    auto &ts = *c->ts;
    if (!ts.shaping_buffer) ts.shaping_buffer = hb_buffer_create();
    hb_buffer_reset(ts.shaping_buffer);
    hb_buffer_add_utf8(ts.shaping_buffer, text.data(), static_cast<int>(text.size()), 0,
                       static_cast<int>(text.size()));
    hb_buffer_guess_segment_properties(ts.shaping_buffer);
    hb_shape(native, ts.shaping_buffer, nullptr, 0);
    if (!hb_buffer_allocation_successful(ts.shaping_buffer)) return false;
    unsigned count = 0;
    const auto *info = hb_buffer_get_glyph_infos(ts.shaping_buffer, &count);
    const auto *pos = hb_buffer_get_glyph_positions(ts.shaping_buffer, nullptr);
    // A missing primary glyph uses the established fallback-font path for this
    // run. Do not manufacture a .notdef outline or silently lose the text.
    for (unsigned i = 0; i < count; ++i)
        if (!info[i].codepoint) return false;
    const auto &f = ts.fonts[static_cast<usize>(font)];
    const f32 scale = stbtt_ScaleForPixelHeight(&f.info, size);
    text_store::text_layout built;
    built.gs.reserve(count);
    for (unsigned i = 0; i < count; ++i)
    {
        auto *outline = harfbuzz_outline(c, font, info[i].codepoint);
        if (!outline) return false;
        text_store::cached_glyph value;
        value.vector = true;
        value.glyph_index = info[i].codepoint;
        value.cluster = info[i].cluster;
        value.offset = {pos[i].x_offset * scale, pos[i].y_offset * scale};
        value.g.advance = pos[i].x_advance * scale;
        value.g.xoff = outline->extents.x_bearing * scale + value.offset.x;
        value.g.yoff = -outline->extents.y_bearing * scale - value.offset.y;
        value.g.w = outline->extents.width * scale;
        value.g.h = -outline->extents.height * scale;
        built.width += value.g.advance;
        if (value.g.advance < 0) built.forward = false;
        if (value.g.w > 0) built.min_xoff = min2(built.min_xoff, value.g.xoff);
        built.gs.push_back(value);
    }
    built.epoch = built.build_epoch = ts.recycled;
    built.built_size = size;
    result = std::move(built);
    return true;
}
} // namespace detail
#endif
