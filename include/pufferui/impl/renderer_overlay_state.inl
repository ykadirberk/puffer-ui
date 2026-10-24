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
    f32 ox = 0.0f, oy = 0.0f;

    void reset(f32 origin_x, f32 origin_y)
    {
        cmds.clear();
        verts.clear();
        idx.clear();
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
    void replay(render_device &dev) const
    {
        for (const cmd &c : cmds)
        {
            if (c.is_clip)
                dev.set_clip(c.clip_on ? &c.clip : nullptr);
            else
                dev.draw(c.tex, verts.data() + c.v0, c.vn, idx.data() + c.i0, c.in);
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
    explicit overlay_entry(context &c) : painter(c) {}
};
