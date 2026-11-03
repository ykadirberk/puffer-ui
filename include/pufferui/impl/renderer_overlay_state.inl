// Renderer-owned overlay recording and paint storage.
// Included by pufferui.h inside namespace pui.
// Recorded mesh coordinates preserve parent-to-overlay pixel mapping.
// Four-line slice banner for amalgamation.
// Records what a popup draws while it lives in a host overlay window: draws and
// clips are kept - shifted into the overlay's pixels as they arrive (`ox`, `oy`:
// the overlay's origin in the parent's framebuffer) - and replayed as they are
// onto the overlay's surface at end_frame; texture work (the glyph atlas may
// grow mid-popup) goes straight to the real device. Render-target effects
// (blur) are skipped inside overlays.
struct overlay_recorder : render_device
{
    render_painter *create_painter(context &c) override { return create_mesh_painter(c); }
    render_device *real = nullptr;
    struct cmd
    {
        bool is_clip = false;
        bool clip_on = false;
        rect clip{};
        texture_handle tex = nullptr;
        i32 v0 = 0, vn = 0, i0 = 0, in = 0;
    };
    std::vector<cmd> cmds;
    std::vector<vertex> verts;
    std::vector<i32> idx;
    struct vector_tape
    {
        std::vector<paint_vector> commands;
    };
    vector_tape *vectors = nullptr;
    ~overlay_recorder() override { delete vectors; }
    void prepare_vectors(usize count)
    {
        if (!count) return;
        if (!vectors) vectors = new vector_tape;
        if (count > vectors->commands.capacity())
        {
            vectors->commands.reserve(count);
            cmds.reserve(std::max(cmds.capacity(), count * 4 + 64));
        }
    }
    f32 ox = 0.0f, oy = 0.0f;

    void reset(f32 origin_x, f32 origin_y)
    {
        cmds.clear();
        verts.clear();
        idx.clear();
        if (vectors) vectors->commands.clear();
        ox = origin_x;
        oy = origin_y;
    }
    backend_caps caps() const override { return real ? real->caps() : backend_caps::SCISSOR; }
    texture_handle create_texture(i32 w, i32 h, const u8 *rgba) override
    {
        return real ? real->create_texture(w, h, rgba) : nullptr;
    }
    void update_texture(texture_handle t, i32 x, i32 y, i32 w, i32 h, const u8 *rgba) override
    {
        if (real) real->update_texture(t, x, y, w, h, rgba);
    }
    void destroy_texture(texture_handle t) override
    {
        if (real) real->destroy_texture(t);
    }
    void set_texture_filter(texture_handle t, bool linear) override
    {
        if (real) real->set_texture_filter(t, linear);
    }
    void set_cursor(cursor c) override
    {
        if (real) real->set_cursor(c);
    }
    void set_clip(const rect *clip) override
    {
        cmd c;
        c.is_clip = true;
        c.clip_on = clip != nullptr;
        if (clip) c.clip = rect{clip->x - ox, clip->y - oy, clip->w, clip->h};
        cmds.push_back(c);
    }
    vector_status capture_vector(const paint_vector &paint)
    {
        if (!vectors || vectors->commands.size() == vectors->commands.capacity() ||
            cmds.size() == cmds.capacity())
            return {
                {vector_error_code::LIMIT, 0, "overlay vector recording exceeds prepared storage"}};
        cmd c;
        c.vn = -1;
        c.v0 = static_cast<i32>(vectors->commands.size());
        vectors->commands.push_back(paint);
        cmds.push_back(c);
        return {};
    }
    void draw(texture_handle tex, const vertex *v, i32 vn, const i32 *ix, i32 in) override
    {
        cmd c;
        c.tex = tex;
        c.v0 = static_cast<i32>(verts.size());
        c.vn = vn;
        c.i0 = static_cast<i32>(idx.size());
        c.in = in;
        verts.insert(verts.end(), v, v + vn);
        for (vertex *p = verts.data() + c.v0, *end = p + vn; p != end; ++p)
        {
            p->x -= ox;
            p->y -= oy;
        }
        idx.insert(idx.end(), ix, ix + in);
        cmds.push_back(c);
    }
    // Onto the overlay, as recorded (already in its pixels): no copies.
    void replay(render_device &dev, context &ctx) const
    {
        const bool has_vectors = vectors && !vectors->commands.empty();
        const rect old_screen = ctx.screen, old_clip = ctx.clip_stack[0];
        const i32 old_clip_depth = ctx.clip_depth, old_overlay_depth = ctx.overlay_depth;
        if (has_vectors)
        {
            ctx.screen = {0, 0, ctx.overlay_area.w, ctx.overlay_area.h};
            // overlay_area may be stale after its scope; the caller supplies the
            // live surface rectangle through screen before replay when needed.
            ctx.clip_depth = 0;
            ctx.overlay_depth = 0;
            ctx.painter->invalidate();
        }
        bool clip_on = false;
        rect current_clip{};
        for (const cmd &c : cmds)
        {
            if (c.is_clip)
            {
                if (has_vectors) ctx.painter->flush();
                dev.set_clip(c.clip_on ? &c.clip : nullptr);
                clip_on = c.clip_on;
                current_clip = c.clip;
                if (has_vectors)
                {
                    ctx.clip_depth = c.clip_on ? 1 : 0;
                    if (c.clip_on)
                        ctx.clip_stack[0] = {
                            c.clip.x / ctx.render_scale, c.clip.y / ctx.render_scale,
                            c.clip.w / ctx.render_scale, c.clip.h / ctx.render_scale};
                    ctx.painter->invalidate();
                }
            }
            else if (c.vn == -1)
            {
                paint_vector p = vectors->commands[static_cast<usize>(c.v0)];
                p.options.transform.e -= ox / ctx.render_scale;
                p.options.transform.f -= oy / ctx.render_scale;
                // Capture already validated residency/extent. Preparation and
                // release are forbidden until overlays have been replayed.
                (void)ctx.painter->vector(p);
            }
            else
            {
                if (has_vectors)
                {
                    ctx.painter->flush();
                    ctx.painter->invalidate();
                    dev.set_clip(clip_on ? &current_clip : nullptr);
                }
                dev.draw(c.tex, verts.data() + c.v0, c.vn, idx.data() + c.i0, c.in);
            }
        }
        if (has_vectors)
        {
            ctx.painter->flush();
            ctx.painter->invalidate();
            ctx.screen = old_screen;
            ctx.clip_stack[0] = old_clip;
            ctx.clip_depth = old_clip_depth;
            ctx.overlay_depth = old_overlay_depth;
        }
        dev.set_clip(nullptr);
    }
};

// A popup hosted in an OS overlay window (window_host::open_overlay).
struct overlay_entry
{
    window *parent = nullptr;
    uiid id = 0;
    window_host::overlay_handle h{};
    rect area{};         // local logical, the popup's rect
    rect placed{};       // where the host window currently is (local logical)
    bool used = false;   // drawn this frame (unused overlays close at end_frame)
    bool failed = false; // the host could not open it: the popup draws in-window
    overlay_recorder rec;
    renderer_detail::mesh_painter painter;
    explicit overlay_entry(context &c) : painter(c)
    {
        painter.vector_recorder = &rec;
        painter.record_vector = [](void *recorder, const paint_vector &paint)
        { return static_cast<overlay_recorder *>(recorder)->capture_vector(paint); };
    }
};
