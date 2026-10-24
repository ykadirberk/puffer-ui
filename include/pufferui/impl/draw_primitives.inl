// pufferui/impl/draw_primitives.inl - part of the PufferUI implementation (see pufferui.h).
// Included by pufferui.h inside `namespace pui` when PUFFERUI_IMPLEMENTATION is
// defined; never include it directly. The slices concatenate to the original
// implementation block in order (tools/amalgamate.ps1 rebuilds the single header).
// ---- draw primitives ----
void ui::draw_triangles(texture_handle tex, const vertex *vertices, i32 vertex_count,
                        const i32 *indices, i32 index_count)
{
    ctx->painter->mesh({tex, vertices, vertex_count, indices, index_count});
}

void ui::draw_rect(rect r, color c)
{
    ctx->painter->rectangle({r, c});
}

void ui::draw_line(f32 x0, f32 y0, f32 x1, f32 y1, color c, f32 thickness)
{
    ctx->painter->line({{x0, y0}, {x1, y1}, c, thickness});
}

void ui::draw_rounded_gradient(const paint_rounded_gradient &p)
{
    ctx->painter->rounded_gradient(p);
}

void ui::draw_rounded_rect(rect r, color c, f32 radius)
{
    draw_rounded_rect(r, c, corner_radii::all(radius));
}

void ui::draw_rounded_rect(rect r, color c, const corner_radii &radii_in)
{
    ctx->painter->rounded_rectangle({r, c, radii_in});
}

void ui::draw_image(const skin_image &img, rect dst)
{
    ctx->painter->image({img, dst});
}

rect fit_image(rect area, f32 img_w, f32 img_h, image_fit fit, rect &uv)
{
    uv = rect{0.0f, 0.0f, 1.0f, 1.0f};
    if (img_w <= 0.0f || img_h <= 0.0f || area.w <= 0.0f || area.h <= 0.0f) return area;
    if (fit == FIT_STRETCH) return area;
    const f32 sx = area.w / img_w, sy = area.h / img_h;
    if (fit == FIT_CONTAIN)
    {
        const f32 s = min2(sx, sy);
        const f32 w = img_w * s, h = img_h * s;
        return rect::make(area.x + (area.w - w) * 0.5f, area.y + (area.h - h) * 0.5f, w, h);
    }
    // FIT_COVER: the area stays; the visible part of the image shrinks.
    const f32 s = max2(sx, sy);
    const f32 vw = area.w / (img_w * s), vh = area.h / (img_h * s); // visible fraction
    uv = rect{(1.0f - vw) * 0.5f, (1.0f - vh) * 0.5f, vw, vh};
    return area;
}

rect ui::image(rect r, const image_handle &img, const image_opts &opts)
{
    return ctx->painter->fitted_image({r, img, opts});
}

image_handle create_image(context *c, i32 w, i32 h, const u8 *rgba)
{
    image_handle img;
    if (!c || !c->device || w <= 0 || h <= 0 || !rgba) return img;
    img.tex = c->device->create_texture(w, h, rgba);
    if (!img.tex) return img;
    c->device->set_texture_filter(img.tex, true);
    img.w = w;
    img.h = h;
    return img;
}

void destroy_image(context *c, image_handle &img)
{
    if (c && c->device && img.tex) c->device->destroy_texture(img.tex);
    img = image_handle{};
}

void ui::draw_nine_slice(const skin_image &img, rect dst)
{
    ctx->painter->nine_slice({img, dst});
}

void ui::draw_polygon(std::span<const vec2> points, color c)
{
    ctx->painter->polygon({points, c});
}

void ui::draw_sector(vec2 center, f32 r_in, f32 r_out, f32 a0, f32 a1, color c)
{
    ctx->painter->sector({center, r_in, r_out, a0, a1, c});
}

void ui::draw_arc(vec2 center, f32 radius, f32 thickness, f32 a0, f32 a1, color c)
{
    ctx->painter->arc({center, radius, thickness, a0, a1, c});
}

void ui::blur(rect r, f32 blur_radius, f32 corner_radius, f32 alpha)
{
    blur(r, blur_radius, corner_radii::all(corner_radius), alpha);
}

void ui::blur(rect r, f32 blur_radius, const corner_radii &radii_in, f32 alpha)
{
    ctx->painter->blur({r, blur_radius, radii_in, alpha});
}
