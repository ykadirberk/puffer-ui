// pufferui/impl/sdl3.inl - part of the PufferUI implementation (see pufferui.h).
// Included by pufferui.h inside `namespace pui` when PUFFERUI_IMPLEMENTATION is
// defined; never include it directly. The slices concatenate to the original
// implementation block in order (tools/amalgamate.ps1 rebuilds the single header).
// ---- SDL3 device + surface ----
#if defined(PUFFERUI_ENABLE_SDL3)
namespace
{

// A device texture. Streaming textures keep a CPU mirror so every surface's
// renderer can get its own SDL_Texture copy (SDL textures are renderer-scoped);
// the core still sees one shared handle. Targets are slot-local scratch.
struct sdl3_texture
{
    i32 w = 0, h = 0;
    bool target = false;               // render target (no CPU mirror)
    bool streaming = false;            // CPU-mirrored (atlas, images)
    bool linear = false;               // set_texture_filter: pictures sample linearly
    std::vector<u8> pixels;            // streaming: RGBA mirror
    std::vector<SDL_Texture *> copies; // one per device slot
};

struct sdl3_device;

// SDL's defaults that fight an immediate-mode UI, set at DEFAULT priority so an
// app's own SDL_SetHint (or the environment) still wins. Click-through: SDL
// otherwise swallows the first click on an unfocused window (it only
// activates it), so every second window, dialog or tool palette costs an
// extra click before any widget reacts.
inline void sdl3_default_hints()
{
    SDL_SetHintWithPriority(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1", SDL_HINT_DEFAULT);
}

struct sdl3_surface : render_surface
{
    sdl3_device *dev = nullptr;
    i32 slot = -1;
    // end_frame presents; an app that presents again afterwards must not
    // re-present the same frame (with vsync on that waits a second vblank
    // and halves the frame rate). Re-armed by make_current (begin_frame).
    bool presented = false;

    void make_current(render_device &device) override;
    texture_handle scene_target() override;
    void output_size(i32 &w, i32 &h) override;
    void present() override;
};

struct sdl3_device : render_device
{
    struct slot
    {
        SDL_Window *window = nullptr;
        SDL_Renderer *renderer = nullptr;
        bool owns_renderer = false;
        texture_handle scene = nullptr;
        i32 scene_w = 0, scene_h = 0;
        i32 vsync_applied = -1; // what SDL_SetRenderVSync last got (-1: never)
    };

    std::vector<slot> slots;
    std::vector<sdl3_surface *> surfaces;
    std::vector<sdl3_texture *> textures;
    SDL_Renderer *primary_renderer = nullptr;
    bool primary_claimed = false;
    bool vsync_enabled = true;
    i32 current_slot = -1;
    texture_handle current_target_ = nullptr;
    // draw(): the only converted part (u8 -> float colors). The slots are
    // padded to sizeof(vertex) because SDL's software geometry path reads UVs
    // with the color stride (SDL_SW_RenderGeometryRaw) — heterogeneous
    // strides feed it garbage, so all three strides must be equal.
    struct padded_color
    {
        SDL_FColor col{};
        f32 pad_ = 0.0f;
    };
    static_assert(sizeof(padded_color) == sizeof(vertex), "strides must match");
    std::vector<padded_color> colors_scratch;
    SDL_Cursor *cursors[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};

    bool prepare_geometry(const geometry_capacity &capacity) override
    {
        constexpr usize limit = static_cast<usize>(0x7fffffff);
        if (capacity.vertices > limit || capacity.indices > limit ||
            capacity.vertices > colors_scratch.max_size())
            return false;
        // Keep every initialized element: preparation must not erase colors or
        // padding already used by another surface on this shared device.
        if (capacity.vertices > colors_scratch.size())
        {
            if (capacity.vertices > colors_scratch.capacity())
                colors_scratch.reserve(capacity.vertices);
            colors_scratch.resize(capacity.vertices);
        }
        return true;
    }

    ~sdl3_device() override
    {
        for (SDL_Cursor *cu : cursors)
            if (cu) SDL_DestroyCursor(cu);
        for (sdl3_texture *t : textures)
        {
            for (SDL_Texture *c : t->copies)
                if (c) SDL_DestroyTexture(c);
            delete t;
        }
        for (sdl3_surface *s : surfaces) delete s;
        for (slot &sl : slots)
            if (sl.owns_renderer && sl.renderer) SDL_DestroyRenderer(sl.renderer);
    }

    SDL_Renderer *renderer() const
    {
        if (current_slot >= 0 && current_slot < static_cast<i32>(slots.size()))
            return slots[static_cast<usize>(current_slot)].renderer;
        return primary_renderer;
    }

    SDL_Texture *resolve(sdl3_texture *t)
    {
        if (!t || current_slot < 0 || current_slot >= static_cast<i32>(slots.size()))
            return nullptr;
        if (static_cast<i32>(t->copies.size()) < static_cast<i32>(slots.size()))
            t->copies.resize(slots.size(), nullptr);
        SDL_Texture *&tex = t->copies[static_cast<usize>(current_slot)];
        if (tex) return tex;
        SDL_Renderer *ren = renderer();
        if (!ren) return nullptr;
        if (t->target)
        {
            tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, t->w,
                                    t->h);
            if (tex)
            {
                SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
                SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
            }
        }
        else
        {
            tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, t->w,
                                    t->h);
            if (tex)
            {
                SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
                SDL_SetTextureScaleMode(tex,
                                        t->linear ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
                if (!t->pixels.empty()) SDL_UpdateTexture(tex, nullptr, t->pixels.data(), t->w * 4);
            }
        }
        return tex;
    }

    texture_handle make_texture(i32 w, i32 h, bool target, const u8 *rgba)
    {
        if (w <= 0 || h <= 0) return nullptr;
        auto *t = new sdl3_texture();
        t->w = w;
        t->h = h;
        t->target = target;
        t->streaming = !target;
        t->copies.resize(slots.size(), nullptr);
        if (!target)
        {
            t->pixels.assign(static_cast<usize>(w) * static_cast<usize>(h) * 4, 0);
            if (rgba) std::memcpy(t->pixels.data(), rgba, t->pixels.size());
        }
        textures.push_back(t);
        return reinterpret_cast<texture_handle>(t);
    }

    backend_caps caps() const override
    {
        return backend_caps::RENDER_TARGETS | backend_caps::SCISSOR |
               backend_caps::STREAMING_TEXTURES | backend_caps::SHARED_DEVICE |
               backend_caps::READ_PIXELS;
    }

    render_surface *create_surface(void *native_window) override
    {
        SDL_Window *win = static_cast<SDL_Window *>(native_window);
        for (sdl3_surface *s : surfaces)
            if (s->slot >= 0 && slots[static_cast<usize>(s->slot)].window == win && win) return s;

        SDL_Renderer *ren = nullptr;
        bool owns = false;
        if (!primary_claimed && primary_renderer)
        {
            ren = primary_renderer;
            primary_claimed = true;
            if (!win) win = SDL_GetRenderWindow(ren);
        }
        else
        {
            if (!win) return nullptr;
            ren = SDL_CreateRenderer(win, nullptr);
            if (!ren) return nullptr;
            owns = true;
        }

        auto *s = new sdl3_surface();
        s->dev = this;
        s->slot = static_cast<i32>(slots.size());
        slot sl;
        sl.window = win;
        sl.renderer = ren;
        sl.owns_renderer = owns;
        slots.push_back(sl);
        surfaces.push_back(s);
        sync_vsync();
        return s;
    }

    // VSync paces the app's loop, and the loop presents every window in turn:
    // if each window waited for its own vblank, N windows would run at
    // 1/N of the refresh rate. So exactly one window - the first one on
    // screen - waits; the others present without waiting and the compositor
    // shows them at their next vblank (windowed: no tearing). A minimized or
    // hidden pacer hands the job to the next visible window, so the loop never
    // spins uncapped while any window is on screen.
    // A slot outlives its window when the app destroys an extra window (SDL
    // takes the window's renderer with it); SDL validates the handle, so a
    // destroyed window reads as id 0.
    static bool slot_alive(const slot &sl)
    {
        return sl.renderer && sl.window && SDL_GetWindowID(sl.window) != 0;
    }

    i32 pacer_slot() const
    {
        constexpr SDL_WindowFlags off_screen =
            SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED | SDL_WINDOW_OCCLUDED;
        i32 first = -1;
        for (i32 i = 0; i < static_cast<i32>(slots.size()); ++i)
        {
            const slot &sl = slots[static_cast<usize>(i)];
            if (!slot_alive(sl)) continue;
            if (first < 0) first = i;
            if ((SDL_GetWindowFlags(sl.window) & off_screen) == 0) return i;
        }
        return first; // nothing on screen: the first window still paces
    }

    // Applies the pacing; SDL is only called when a window's setting changes.
    void sync_vsync()
    {
        const i32 pacer = vsync_enabled ? pacer_slot() : -1;
        for (i32 i = 0; i < static_cast<i32>(slots.size()); ++i)
        {
            slot &sl = slots[static_cast<usize>(i)];
            const i32 want = i == pacer ? 1 : 0;
            if (!slot_alive(sl) || sl.vsync_applied == want) continue;
            SDL_SetRenderVSync(sl.renderer, want);
            sl.vsync_applied = want;
        }
    }

    // The slot stays (indices into texture copies stay put) but goes dead: its
    // texture copies and its own renderer are released before the app
    // destroys the SDL window.
    void destroy_surface(render_surface *surface) override
    {
        sdl3_surface *s = nullptr;
        for (usize i = 0; i < surfaces.size(); ++i)
        {
            if (surfaces[i] != surface) continue;
            s = surfaces[i];
            surfaces.erase(surfaces.begin() + static_cast<std::ptrdiff_t>(i));
            break;
        }
        if (!s) return;
        const i32 si = s->slot;
        delete s;
        if (si < 0 || si >= static_cast<i32>(slots.size())) return;
        slot &sl = slots[static_cast<usize>(si)];
        if (sl.scene) destroy_texture(sl.scene);
        for (sdl3_texture *t : textures)
        {
            if (si >= static_cast<i32>(t->copies.size())) continue;
            SDL_Texture *&copy = t->copies[static_cast<usize>(si)];
            if (copy) SDL_DestroyTexture(copy);
            copy = nullptr;
        }
        if (sl.owns_renderer && sl.renderer) SDL_DestroyRenderer(sl.renderer);
        sl = slot{};
        if (current_slot == si) current_slot = -1;
        sync_vsync(); // the pacer may have been this window
    }

    void ensure_scene(i32 slot_index, i32 w, i32 h)
    {
        if (slot_index < 0 || slot_index >= static_cast<i32>(slots.size())) return;
        slot &sl = slots[static_cast<usize>(slot_index)];
        if (sl.scene && sl.scene_w == w && sl.scene_h == h) return;
        if (sl.scene)
        {
            destroy_texture(sl.scene);
            sl.scene = nullptr;
        }
        sl.scene = make_texture(w, h, true, nullptr);
        sl.scene_w = w;
        sl.scene_h = h;
    }

    void begin_frame() override {}
    void end_frame() override {}

    void set_vsync(bool enabled) override
    {
        vsync_enabled = enabled;
        sync_vsync();
    }

    void clear(color col) override
    {
        SDL_Renderer *ren = renderer();
        if (!ren) return;
        SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, col.a);
        SDL_RenderClear(ren);
    }

    void set_cursor(cursor c) override
    {
        const int i = static_cast<int>(c);
        if (i < 0 || i > 4) return;
        if (!cursors[i])
        {
            SDL_SystemCursor sc = SDL_SYSTEM_CURSOR_DEFAULT;
            switch (c)
            {
            case cursor::IBEAM:
                sc = SDL_SYSTEM_CURSOR_TEXT;
                break;
            case cursor::HAND:
                sc = SDL_SYSTEM_CURSOR_POINTER;
                break;
            case cursor::HRESIZE:
                sc = SDL_SYSTEM_CURSOR_EW_RESIZE;
                break;
            case cursor::VRESIZE:
                sc = SDL_SYSTEM_CURSOR_NS_RESIZE;
                break;
            default:
                sc = SDL_SYSTEM_CURSOR_DEFAULT;
                break;
            }
            cursors[i] = SDL_CreateSystemCursor(sc);
        }
        if (cursors[i]) SDL_SetCursor(cursors[i]);
    }

    void set_clip(const rect *clip) override
    {
        SDL_Renderer *ren = renderer();
        if (!ren) return;
        if (clip && clip->w > 0.0f && clip->h > 0.0f)
        {
            SDL_Rect r{static_cast<int>(clip->x), static_cast<int>(clip->y),
                       static_cast<int>(clip->w), static_cast<int>(clip->h)};
            SDL_SetRenderClipRect(ren, &r);
        }
        else
        {
            SDL_SetRenderClipRect(ren, nullptr);
        }
    }

    void set_target(texture_handle target) override
    {
        SDL_Renderer *ren = renderer();
        if (!ren) return;
        SDL_SetRenderTarget(ren,
                            target ? resolve(reinterpret_cast<sdl3_texture *>(target)) : nullptr);
        current_target_ = target;
    }
    texture_handle current_target() const override { return current_target_; }

    texture_handle create_texture(i32 w, i32 h, const u8 *rgba) override
    {
        return make_texture(w, h, false, rgba);
    }

    texture_handle create_target(i32 w, i32 h) override
    {
        return make_texture(w, h, true, nullptr);
    }

    void destroy_target(texture_handle target) override { destroy_texture(target); }

    void blit(texture_handle src, const rect *src_rect, texture_handle dst,
              const rect *dst_rect) override
    {
        SDL_Renderer *ren = renderer();
        if (!ren || !src) return;
        SDL_Texture *s = resolve(reinterpret_cast<sdl3_texture *>(src));
        SDL_Texture *d = dst ? resolve(reinterpret_cast<sdl3_texture *>(dst)) : nullptr;
        if (!s) return;
        SDL_SetRenderTarget(ren, d);
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
        SDL_FRect sfr{}, dfr{};
        SDL_FRect *sp = nullptr;
        SDL_FRect *dp = nullptr;
        if (src_rect)
        {
            sfr = SDL_FRect{src_rect->x, src_rect->y, src_rect->w, src_rect->h};
            sp = &sfr;
        }
        if (dst_rect)
        {
            dfr = SDL_FRect{dst_rect->x, dst_rect->y, dst_rect->w, dst_rect->h};
            dp = &dfr;
        }
        // A blit is a copy (porting_a_backend.md). SDL_RenderTexture blends with
        // the TEXTURE's blend mode (the draw blend mode only covers primitives),
        // so switch the source to NONE for the copy: blending let pyramid texels
        // with alpha < 1 mix with the previous frame's contents, and the blur
        // darkened a little more every frame (worst on narrow rects).
        SDL_BlendMode src_mode = SDL_BLENDMODE_BLEND;
        SDL_GetTextureBlendMode(s, &src_mode);
        SDL_SetTextureBlendMode(s, SDL_BLENDMODE_NONE);
        SDL_RenderTexture(ren, s, sp, dp);
        SDL_SetTextureBlendMode(s, src_mode);
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        current_target_ = dst;
    }

    void update_texture(texture_handle handle, i32 x, i32 y, i32 w, i32 h, const u8 *rgba) override
    {
        sdl3_texture *t = reinterpret_cast<sdl3_texture *>(handle);
        if (!t || t->target || !rgba || w <= 0 || h <= 0) return;
        if (x < 0 || y < 0 || x + w > t->w || y + h > t->h) return;
        for (i32 r = 0; r < h; ++r)
        {
            const usize dst =
                (static_cast<usize>(y + r) * static_cast<usize>(t->w) + static_cast<usize>(x)) * 4;
            std::memcpy(t->pixels.data() + dst,
                        rgba + static_cast<usize>(r) * static_cast<usize>(w) * 4,
                        static_cast<usize>(w) * 4);
        }
        SDL_Rect sub{x, y, w, h};
        for (SDL_Texture *c : t->copies)
            if (c) SDL_UpdateTexture(c, &sub, rgba, w * 4);
    }

    bool read_pixels(texture_handle target, i32 x, i32 y, i32 w, i32 h, u8 *rgba) override
    {
        SDL_Renderer *ren = renderer();
        sdl3_texture *t = reinterpret_cast<sdl3_texture *>(target);
        if (!ren || !t || !rgba || w <= 0 || h <= 0) return false;
        if (x < 0 || y < 0 || x + w > t->w || y + h > t->h) return false;
        SDL_Texture *prev = SDL_GetRenderTarget(ren);
        SDL_SetRenderTarget(ren, resolve(t));
        const SDL_Rect r{x, y, w, h};
        SDL_Surface *s = SDL_RenderReadPixels(ren, &r);
        SDL_SetRenderTarget(ren, prev);
        if (!s) return false;
        SDL_Surface *c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(s);
        if (!c) return false;
        for (i32 row = 0; row < h; ++row)
            std::memcpy(rgba + static_cast<usize>(row) * w * 4,
                        static_cast<const u8 *>(c->pixels) + static_cast<usize>(row) * c->pitch,
                        static_cast<usize>(w) * 4);
        SDL_DestroySurface(c);
        return true;
    }

    void set_texture_filter(texture_handle handle, bool linear) override
    {
        sdl3_texture *t = reinterpret_cast<sdl3_texture *>(handle);
        if (!t || t->target) return;
        t->linear = linear;
        for (SDL_Texture *c : t->copies)
            if (c)
                SDL_SetTextureScaleMode(c, linear ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
    }

    void destroy_texture(texture_handle handle) override
    {
        sdl3_texture *t = reinterpret_cast<sdl3_texture *>(handle);
        if (!t) return;
        for (SDL_Texture *c : t->copies)
            if (c) SDL_DestroyTexture(c);
        for (usize i = 0; i < textures.size(); ++i)
        {
            if (textures[i] == t)
            {
                textures.erase(textures.begin() + static_cast<std::ptrdiff_t>(i));
                break;
            }
        }
        delete t;
    }

    void draw(texture_handle handle, const vertex *vertices, i32 vertex_count, const i32 *indices,
              i32 index_count) override
    {
        SDL_Renderer *ren = renderer();
        if (!ren || vertex_count <= 0 || index_count <= 0) return;
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        // Keep the initialized high-water prefix: small draws must not make the
        // next large draw zero-initialize colors that it immediately overwrites.
        if (colors_scratch.size() < static_cast<usize>(vertex_count))
            colors_scratch.resize(static_cast<usize>(vertex_count));
        for (i32 i = 0; i < vertex_count; ++i)
        {
            colors_scratch[static_cast<usize>(i)].col =
                SDL_FColor{vertices[i].c.r / 255.0f, vertices[i].c.g / 255.0f,
                           vertices[i].c.b / 255.0f, vertices[i].c.a / 255.0f};
        }
        // Zero-copy positions and UVs: RenderGeometryRaw strides straight over
        // the core's interleaved vertex buffer. All three strides are
        // sizeof(vertex): SDL's software geometry path reads UVs with the
        // color stride (SDL_SW_RenderGeometryRaw's quad detection), so the
        // strides must be equal — the color slots are padded accordingly.
        constexpr int stride = static_cast<int>(sizeof(vertex));
        SDL_RenderGeometryRaw(
            ren, handle ? resolve(reinterpret_cast<sdl3_texture *>(handle)) : nullptr,
            &vertices[0].x, stride, reinterpret_cast<const SDL_FColor *>(colors_scratch.data()),
            stride, &vertices[0].u, stride, vertex_count, indices, index_count,
            static_cast<int>(sizeof(i32)));
    }
};

void sdl3_surface::make_current(render_device &)
{
    sdl3_device &d = *dev;
    if (slot < 0 || slot >= static_cast<i32>(d.slots.size())) return;
    d.current_slot = slot;
    presented = false;
    sdl3_device::slot &sl = d.slots[static_cast<usize>(slot)];
    if (!sl.renderer) return;
    int w = 0, h = 0;
    SDL_GetCurrentRenderOutputSize(sl.renderer, &w, &h);
    if (w > 0 && h > 0) d.ensure_scene(slot, w, h);
    SDL_SetRenderDrawBlendMode(sl.renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderTarget(sl.renderer,
                        sl.scene ? d.resolve(reinterpret_cast<sdl3_texture *>(sl.scene)) : nullptr);
    d.current_target_ = sl.scene;
}

texture_handle sdl3_surface::scene_target()
{
    if (slot < 0 || slot >= static_cast<i32>(dev->slots.size())) return nullptr;
    return dev->slots[static_cast<usize>(slot)].scene;
}

void sdl3_surface::output_size(i32 &w, i32 &h)
{
    w = 0;
    h = 0;
    if (slot < 0 || slot >= static_cast<i32>(dev->slots.size())) return;
    SDL_Renderer *ren = dev->slots[static_cast<usize>(slot)].renderer;
    if (!ren) return;
    int iw = 0, ih = 0;
    SDL_GetCurrentRenderOutputSize(ren, &iw, &ih);
    w = iw;
    h = ih;
}

void sdl3_surface::present()
{
    sdl3_device &d = *dev;
    if (slot < 0 || slot >= static_cast<i32>(d.slots.size())) return;
    if (presented) return; // this frame is already on screen
    sdl3_device::slot &sl = d.slots[static_cast<usize>(slot)];
    if (!sl.renderer) return;
    presented = true;
    d.sync_vsync(); // the pacer may have changed (minimized, hidden, closed)
    d.current_slot = slot;
    SDL_SetRenderTarget(sl.renderer, nullptr);
    SDL_SetRenderDrawBlendMode(sl.renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderClipRect(sl.renderer, nullptr);
    if (sl.scene)
    {
        SDL_Texture *scene_tex = d.resolve(reinterpret_cast<sdl3_texture *>(sl.scene));
        if (scene_tex) SDL_RenderTexture(sl.renderer, scene_tex, nullptr, nullptr);
    }
    SDL_SetRenderDrawBlendMode(sl.renderer, SDL_BLENDMODE_BLEND);
    SDL_RenderPresent(sl.renderer);
}

} // namespace
#endif

render_device *create_sdl3_device(void *sdl_renderer)
{
#if defined(PUFFERUI_ENABLE_SDL3)
    sdl3_default_hints();
    auto *d = new sdl3_device();
    d->primary_renderer = static_cast<SDL_Renderer *>(sdl_renderer);
    return d;
#else
    (void)sdl_renderer;
    return nullptr;
#endif
}

void destroy_sdl3_device(render_device *device)
{
    delete device;
}

#if defined(PUFFERUI_ENABLE_SDL3)
namespace
{
struct sdl3_clipboard : clipboard
{
    bool get(std::string &out) override
    {
        char *t = SDL_GetClipboardText();
        if (!t) return false;
        out = t;
        SDL_free(t);
        return true;
    }
    void set(std::string_view text) override { SDL_SetClipboardText(std::string(text).c_str()); }
};
} // namespace
#endif

clipboard *sdl3_system_clipboard()
{
#if defined(PUFFERUI_ENABLE_SDL3)
    static sdl3_clipboard cb;
    return &cb;
#else
    return nullptr;
#endif
}

#if defined(PUFFERUI_ENABLE_SDL3)
namespace
{

// The hit-test callback runs on every pointer move over the window, so it
// stays stateless and cheap: geometry only, no layout.
SDL_HitTestResult SDLCALL sdl3_chrome_hit_test(SDL_Window *win, const SDL_Point *area, void *data)
{
    context *c = static_cast<context *>(data);
    window *w = c ? window_at(c, win) : nullptr;
    if (!w) return SDL_HITTEST_NORMAL;
    const f32 lx = static_cast<f32>(area->x) / w->input_scale,
              ly = static_cast<f32>(area->y) / w->input_scale;
    if (!w->maximized)
    {
        // Generous native resize borders: the corner squares claim both axes
        // at once and take precedence over the straight edges. A maximized
        // window keeps no resize bands (its state is fixed), but its caption
        // below stays draggable - the system restores it during the native
        // drag.
        constexpr f32 edge = 8.0f;
        const bool left = lx <= edge, right = lx >= w->area.w - edge;
        const bool top = ly <= edge, bottom = ly >= w->area.h - edge;
        if (top && left) return SDL_HITTEST_RESIZE_TOPLEFT;
        if (top && right) return SDL_HITTEST_RESIZE_TOPRIGHT;
        if (bottom && left) return SDL_HITTEST_RESIZE_BOTTOMLEFT;
        if (bottom && right) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
        if (left) return SDL_HITTEST_RESIZE_LEFT;
        if (right) return SDL_HITTEST_RESIZE_RIGHT;
        // A widget on the titlebar (a caption hole) reaches the top edge, as
        // the system's own caption buttons do.
        bool on_widget = false;
        for (i32 i = 0; i < w->caption.hole_count && !on_widget; ++i)
            on_widget = w->caption.holes[i].contains(lx, ly);
        if (top && !on_widget) return SDL_HITTEST_RESIZE_TOP;
        if (bottom) return SDL_HITTEST_RESIZE_BOTTOM;
    }
    // The system caption is what the last frame declared: its drag regions
    // (the titlebar strip, ui::window_drag_region) minus every widget over
    // them, so buttons on the bar keep their clicks. For a maximized window
    // SDL keeps the WS_MAXIMIZEBOX style, so the system's move loop restores
    // it when the caption is dragged.
    if (w->caption.contains(lx, ly)) return SDL_HITTEST_DRAGGABLE;
    return SDL_HITTEST_NORMAL;
}

// The SDL3 window host: the platform actions the custom titlebar issues.
struct sdl3_window_host : window_host
{
    void move_window(window &w, f32 desktop_x, f32 desktop_y) override
    {
        SDL_Window *win = static_cast<SDL_Window *>(w.handle);
        if (win)
            SDL_SetWindowPosition(win, static_cast<int>(desktop_x), static_cast<int>(desktop_y));
    }
    void resize_window(window &w, f32 width, f32 height) override
    {
        SDL_Window *win = static_cast<SDL_Window *>(w.handle);
        if (win) SDL_SetWindowSize(win, static_cast<int>(width), static_cast<int>(height));
    }
    void minimize_window(window &w) override
    {
        SDL_Window *win = static_cast<SDL_Window *>(w.handle);
        if (win) SDL_MinimizeWindow(win);
    }
    void toggle_maximize(window &w) override
    {
        SDL_Window *win = static_cast<SDL_Window *>(w.handle);
        if (!win) return;
        (void)w;
        // Read the real window state, not `w.maximized`: the library flag is
        // set optimistically by the pump, and a stale flag must never wedge
        // the OS state (restore must stay reachable by clicking the glyph).
        if (SDL_GetWindowFlags(win) & SDL_WINDOW_MAXIMIZED)
            SDL_RestoreWindow(win);
        else
            SDL_MaximizeWindow(win);
    }
    // Native chrome for a borderless window: the titlebar's drag strip maps to
    // the system caption (native dragging = the system's own Aero Snap, its
    // preview, and maximize-on-double-click), and generous edge/corner bands
    // map to the native resize borders. The glyph buttons stay normal client
    // area so the library's clicks reach them.
    void install_system_chrome(window &w) override
    {
        SDL_Window *win = static_cast<SDL_Window *>(w.handle);
        if (!win) return;
        SDL_SetWindowResizable(win, true);
        const char *driver = SDL_GetCurrentVideoDriver();
        const bool headless =
            driver && (std::strcmp(driver, "offscreen") == 0 || std::strcmp(driver, "dummy") == 0);
        if (!SDL_SetWindowHitTest(win, sdl3_chrome_hit_test, w.owner) && !headless)
        {
            // No native hit-test on this platform (some Wayland/X11 setups):
            // a borderless window could not be moved at all, so it gets its OS
            // frame back and the custom titlebar stands down.
            SDL_SetWindowBordered(win, true);
            w.os_frame = true;
        }
    }

    // Popups may use the usable area of the display the window is on - when
    // the video driver has popup windows at all (the offscreen driver does
    // not). SDL has no query for it, so the first call probes once with a
    // hidden 1x1 popup.
    bool popup_bounds(window &w, rect &local) override
    {
        SDL_Window *win = static_cast<SDL_Window *>(w.handle);
        if (!win) return false;
        static const char *probed_driver = nullptr;
        static bool supported = false;
        const char *driver = SDL_GetCurrentVideoDriver();
        if (driver != probed_driver)
        {
            probed_driver = driver;
            SDL_Window *probe =
                SDL_CreatePopupWindow(win, 0, 0, 1, 1, SDL_WINDOW_POPUP_MENU | SDL_WINDOW_HIDDEN);
            supported = probe != nullptr;
            if (probe) SDL_DestroyWindow(probe);
        }
        if (!supported) return false;
        SDL_Rect b{};
        if (!SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(win), &b)) return false;
        const f32 s = w.input_scale;
        local = rect::make((static_cast<f32>(b.x) - w.client.x) / s,
                           (static_cast<f32>(b.y) - w.client.y) / s, static_cast<f32>(b.w) / s,
                           static_cast<f32>(b.h) / s);
        return true;
    }

    // A popup-menu window that never takes focus (keys stay with the parent),
    // transparent around rounded corners, and placed exactly where asked (no
    // SDL constraint shift: input is translated by the same offset).
    bool open_overlay(window &parent, rect local, overlay_handle &out) override
    {
        SDL_Window *pw = static_cast<SDL_Window *>(parent.handle);
        context *c = parent.owner;
        if (!pw || !c || !c->device) return false;
        const f32 s = parent.input_scale;
        const SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetPointerProperty(props, SDL_PROP_WINDOW_CREATE_PARENT_POINTER, pw);
        SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_MENU_BOOLEAN, true);
        SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_FOCUSABLE_BOOLEAN, false);
        SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_TRANSPARENT_BOOLEAN, true);
        SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_HIGH_PIXEL_DENSITY_BOOLEAN, true);
        SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_CONSTRAIN_POPUP_BOOLEAN, false);
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER,
                              static_cast<Sint64>(local.x * s));
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER,
                              static_cast<Sint64>(local.y * s));
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER,
                              static_cast<Sint64>(local.w * s + 0.5f));
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER,
                              static_cast<Sint64>(local.h * s + 0.5f));
        SDL_Window *ow = SDL_CreateWindowWithProperties(props);
        SDL_DestroyProperties(props);
        if (!ow) return false;
        render_surface *surf = c->device->create_surface(ow);
        if (!surf)
        {
            SDL_DestroyWindow(ow);
            return false;
        }
        out.native = ow;
        out.surface = surf;
        return true;
    }

    void move_overlay(window &parent, rect local, overlay_handle &h) override
    {
        SDL_Window *ow = static_cast<SDL_Window *>(h.native);
        if (!ow) return;
        const f32 s = parent.input_scale;
        SDL_SetWindowPosition(ow, static_cast<int>(local.x * s), static_cast<int>(local.y * s));
        SDL_SetWindowSize(ow, static_cast<int>(local.w * s + 0.5f),
                          static_cast<int>(local.h * s + 0.5f));
    }

    void close_overlay(window &parent, overlay_handle &h) override
    {
        context *c = parent.owner;
        if (c && c->device && h.surface) c->device->destroy_surface(h.surface);
        if (h.native) SDL_DestroyWindow(static_cast<SDL_Window *>(h.native));
        h = overlay_handle{};
    }
};

// The core window an SDL window's pointer events belong to: the window itself,
// or - for a popup overlay - its parent, with the overlay's offset (platform
// units) to add so the events land in the parent's local coordinates.
window *sdl3_pointer_window(context *c, SDL_Window *sdl, f32 &dx, f32 &dy)
{
    dx = dy = 0.0f;
    if (!sdl) return nullptr;
    if (window *w = window_at(c, sdl)) return w;
    for (const overlay_entry *e : c->st->overlays)
    {
        if (e->h.native != sdl || !e->parent) continue;
        dx = e->placed.x * e->parent->input_scale;
        dy = e->placed.y * e->parent->input_scale;
        return e->parent;
    }
    return nullptr;
}

window_host *sdl3_host_instance()
{
    static sdl3_window_host host;
    return &host;
}

void sdl3_sync_scale(window &w, SDL_Window *sdl)
{
    const f32 density = SDL_GetWindowPixelDensity(sdl);
    const f32 scale = SDL_GetWindowDisplayScale(sdl);
    if (density > 0.0f) set_window_scale(w, scale, {.input_scale = scale / density});
}

// A finished file dialog travels from SDL's callback (any thread) to the
// routing thread as a user event; the payload owns copies of the paths.
struct sdl3_dialog_payload
{
    context *ctx = nullptr;
    uiid id = 0;
    bool failed = false;
    std::vector<std::string> paths;
    // SDL reads the filters until the callback runs: the payload owns them.
    std::vector<std::string> filter_text;
    std::vector<SDL_DialogFileFilter> filters;
    std::string location;
};
u32 sdl3_dialog_event_type = 0;

void sdl3_finish_dialog(context *c, sdl3_dialog_payload *p)
{
    if (!p) return;
    if (p->ctx == c) // a dialog of another (or a destroyed) context is dropped
    {
        std::vector<const char *> list;
        for (const std::string &s : p->paths) list.push_back(s.c_str());
        const i32 n = p->failed ? -1 : static_cast<i32>(list.size());
        deliver_file_dialog(c, p->id, list.data(), n);
    }
    delete p;
}

void SDLCALL sdl3_dialog_callback(void *userdata, const char *const *files, int)
{
    sdl3_dialog_payload *p = static_cast<sdl3_dialog_payload *>(userdata);
    if (!files)
        p->failed = true;
    else
        for (const char *const *f = files; *f; ++f) p->paths.emplace_back(*f);
    SDL_Event ev{};
    ev.type = sdl3_dialog_event_type;
    ev.user.data1 = p;
    if (!SDL_PushEvent(&ev)) delete p;
}
} // namespace

bool sdl3_route_impl(context *c, SDL_Event *e)
{
    if (!c || !e) return false;
    if (!window_host_of(c))
    {
        sdl3_default_hints(); // apps that route SDL events over their own device
        set_window_host(c, sdl3_host_instance());
    }

    switch (e->type)
    {
    case SDL_EVENT_QUIT:
        return true; // the app should exit
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
    {
        SDL_Window *sdl = SDL_GetWindowFromID(e->window.windowID);
        if (window *w = sdl ? window_at(c, sdl) : nullptr) w->close_requested = true;
        break;
    }
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    {
        SDL_Window *sdl = SDL_GetWindowFromID(e->window.windowID);
        if (window *w = sdl ? window_at(c, sdl) : nullptr) focus_window(c, *w);
        break;
    }
    case SDL_EVENT_WINDOW_FOCUS_LOST:
    {
        // Another app (or another of our windows) took focus: the window's
        // open popup closes, like a native menu.
        SDL_Window *sdl = SDL_GetWindowFromID(e->window.windowID);
        if (window *w = sdl ? window_at(c, sdl) : nullptr) dismiss_popups(*w);
        break;
    }
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_MOVED:
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    {
        SDL_Window *sdl = SDL_GetWindowFromID(e->window.windowID);
        window *w = sdl ? window_at(c, sdl) : nullptr;
        if (w)
        {
            sdl3_sync_scale(*w, sdl);
            int x = 0, y = 0, ww = 0, wh = 0;
            SDL_GetWindowPosition(sdl, &x, &y);
            SDL_GetWindowSize(sdl, &ww, &wh);
            set_window_client(*w, rect::make(static_cast<f32>(x), static_cast<f32>(y),
                                             static_cast<f32>(ww), static_cast<f32>(wh)));
            w->maximized = (SDL_GetWindowFlags(sdl) & SDL_WINDOW_MAXIMIZED) != 0;
        }
        break;
    }
    case SDL_EVENT_WINDOW_MAXIMIZED:
    case SDL_EVENT_WINDOW_RESTORED:
    {
        SDL_Window *sdl = SDL_GetWindowFromID(e->window.windowID);
        if (window *w = sdl ? window_at(c, sdl) : nullptr)
            w->maximized = (SDL_GetWindowFlags(sdl) & SDL_WINDOW_MAXIMIZED) != 0;
        break;
    }
    case SDL_EVENT_MOUSE_MOTION:
    {
        f32 dx = 0.0f, dy = 0.0f;
        if (window *w = sdl3_pointer_window(c, SDL_GetWindowFromID(e->motion.windowID), dx, dy))
            mouse_move(*w, e->motion.x + dx, e->motion.y + dy);
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    {
        f32 dx = 0.0f, dy = 0.0f;
        window *w = sdl3_pointer_window(c, SDL_GetWindowFromID(e->button.windowID), dx, dy);
        if (!w) break;
        mouse_move(*w, e->button.x + dx, e->button.y + dy); // buttons carry coordinates
        const bool down = e->type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        if (e->button.button == SDL_BUTTON_LEFT)
            mouse_button(*w, down);
        else if (e->button.button == SDL_BUTTON_RIGHT)
            mouse_button(*w, pointer_button::RIGHT, down);
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL:
    {
        f32 dx = 0.0f, dy = 0.0f;
        if (window *w = sdl3_pointer_window(c, SDL_GetWindowFromID(e->wheel.windowID), dx, dy))
            mouse_wheel(*w, e->wheel.x, e->wheel.y);
        break;
    }
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
    {
        const bool down = e->type == SDL_EVENT_KEY_DOWN;
        window *w = window_at(c, SDL_GetWindowFromID(e->key.windowID));
        if (!w) w = focused_window(c);
        if (!w) break;
        mods_event(*w, (e->key.mod & SDL_KMOD_SHIFT) != 0, (e->key.mod & SDL_KMOD_CTRL) != 0,
                   (e->key.mod & SDL_KMOD_ALT) != 0, (e->key.mod & SDL_KMOD_GUI) != 0);
        key k = key::ESCAPE;
        bool mapped = true;
        switch (e->key.key)
        {
#define PUI_KEY_CASE(name, native)                                                                 \
    case SDLK_##native:                                                                            \
        k = key::name;                                                                             \
        break;
            PUI_KEYS(PUI_KEY_CASE)
#undef PUI_KEY_CASE
        case SDLK_KP_ENTER:
            k = key::ENTER;
            break;
        default:
            mapped = false;
            break;
        }
        if (mapped)
        {
            if (down && e->key.repeat)
                key_repeat(*w, k);
            else
                key_event(*w, k, down);
        }
        break;
    }
    case SDL_EVENT_TEXT_INPUT:
    {
        SDL_Window *sdl = SDL_GetWindowFromID(e->text.windowID);
        if (window *w = sdl ? window_at(c, sdl) : nullptr) text_input_event(*w, e->text.text);
        break;
    }
    case SDL_EVENT_DROP_BEGIN:
    case SDL_EVENT_DROP_POSITION:
    case SDL_EVENT_DROP_FILE:
    case SDL_EVENT_DROP_TEXT:
    case SDL_EVENT_DROP_COMPLETE:
    {
        SDL_Window *sdl = SDL_GetWindowFromID(e->drop.windowID);
        window *w = sdl ? window_at(c, sdl) : nullptr;
        if (!w) break;
        const drop_kind kind = e->type == SDL_EVENT_DROP_BEGIN      ? drop_kind::BEGIN
                               : e->type == SDL_EVENT_DROP_POSITION ? drop_kind::POSITION
                               : e->type == SDL_EVENT_DROP_FILE     ? drop_kind::FILE
                               : e->type == SDL_EVENT_DROP_TEXT     ? drop_kind::TEXT
                                                                    : drop_kind::COMPLETE;
        drop_event(*w, kind, e->drop.x, e->drop.y, e->drop.data);
        break;
    }
    case SDL_EVENT_TEXT_EDITING:
    {
        SDL_Window *sdl = SDL_GetWindowFromID(e->edit.windowID);
        if (window *w = sdl ? window_at(c, sdl) : nullptr)
            ime_event(*w, e->edit.text, e->edit.start);
        break;
    }
    default:
        if (sdl3_dialog_event_type != 0 && e->type == sdl3_dialog_event_type)
            sdl3_finish_dialog(c, static_cast<sdl3_dialog_payload *>(e->user.data1));
        break;
    }
    return false;
}

bool sdl3_route(context *c, void *sdl_event)
{
    return sdl3_route_impl(c, static_cast<SDL_Event *>(sdl_event));
}

bool sdl3_pump(context *c)
{
    if (!c) return false;
    bool quit = false;
    SDL_Event e;
    while (SDL_PollEvent(&e)) quit = sdl3_route(c, &e) || quit;
    return quit;
}

// ---- one-call bootstrap -----------------------------------------------------
bool sdl3_app_init(sdl3_app &a, const char *title, i32 w, i32 h, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (a.inited) return true;
    if (!title || w <= 0 || h <= 0) return false;

    if (!SDL_Init(SDL_INIT_VIDEO)) return false;

    const SDL_WindowFlags flags = SDL_WINDOW_BORDERLESS | SDL_WINDOW_RESIZABLE |
                                  (a.hidden ? SDL_WINDOW_HIDDEN : static_cast<SDL_WindowFlags>(0));
    a.sdl_window = SDL_CreateWindow(title, w, h, flags | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!a.sdl_window)
    {
        SDL_Quit();
        return false;
    }
    a.sdl_renderer = SDL_CreateRenderer(static_cast<SDL_Window *>(a.sdl_window), a.renderer_name);
    if (!a.sdl_renderer)
    {
        SDL_DestroyWindow(static_cast<SDL_Window *>(a.sdl_window));
        a.sdl_window = nullptr;
        SDL_Quit();
        return false;
    }
    a.device = create_sdl3_device(a.sdl_renderer);
    if (!a.device)
    {
        SDL_DestroyRenderer(static_cast<SDL_Renderer *>(a.sdl_renderer));
        a.sdl_renderer = nullptr;
        SDL_DestroyWindow(static_cast<SDL_Window *>(a.sdl_window));
        a.sdl_window = nullptr;
        SDL_Quit();
        return false;
    }
    a.surface = a.device->create_surface(static_cast<SDL_Window *>(a.sdl_window));
    a.ctx = a.surface ? create_context(a.device, a.surface) : nullptr;
    // The client in desktop coordinates from the start: an app that places
    // another window relative to this one (or anchors desktop-space panels)
    // before the first move event must not see the origin instead.
    int wx = 0, wy = 0;
    SDL_GetWindowPosition(static_cast<SDL_Window *>(a.sdl_window), &wx, &wy);
    a.win = a.ctx ? add_window(a.ctx, a.sdl_window, a.surface,
                               rect::make(static_cast<f32>(wx), static_cast<f32>(wy),
                                          static_cast<f32>(w), static_cast<f32>(h)))
                  : nullptr;
    if (!a.ctx || !a.win)
    {
        if (a.ctx) destroy_context(a.ctx);
        a.ctx = nullptr;
        destroy_sdl3_device(a.device);
        a.device = nullptr;
        a.surface = nullptr;
        SDL_DestroyRenderer(static_cast<SDL_Renderer *>(a.sdl_renderer));
        a.sdl_renderer = nullptr;
        SDL_DestroyWindow(static_cast<SDL_Window *>(a.sdl_window));
        a.sdl_window = nullptr;
        SDL_Quit();
        return false;
    }
    install_window_chrome(a.ctx, *a.win); // native drag/resize/snap
    (void)sdl3_enable_uia(*a.win);        // screen readers (Windows); no cost until queried

    // The base font: the bundled DejaVu set when it can be found (system fonts
    // are the fallback), then a working base theme. Apps override with
    // set_theme afterwards; the handle is handed back for reuse.
    if (a.asset_dir)
    {
        char font_path[512];
        std::snprintf(font_path, sizeof(font_path), "%s/fonts/DejaVuSans.ttf", a.asset_dir);
        a.font = load_font(a.ctx, font_path);
    }
#if defined(PUFFERUI_ASSET_DIR)
    if (a.font == FONT_INVALID)
        a.font = load_font(a.ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
#endif
    if (a.font == FONT_INVALID) a.font = load_font(a.ctx, "assets/fonts/DejaVuSans.ttf");
    if (a.font == FONT_INVALID) a.font = load_font(a.ctx, "C:/Windows/Fonts/segoeui.ttf");
    if (a.font == FONT_INVALID) a.font = load_font(a.ctx, "C:/Windows/Fonts/arial.ttf");
    theme base = default_dark();
    base.font = a.font;
    base.text_size = 15.0f;
    set_theme(a.ctx, base);
    set_clipboard(a.ctx, sdl3_system_clipboard());

    a.width = static_cast<i32>(a.win->area.w);
    a.height = static_cast<i32>(a.win->area.h);
    a.freq = static_cast<f64>(SDL_GetPerformanceFrequency());
    a.last_counter = SDL_GetPerformanceCounter();
    a.inited = true;
    return true;
}

bool sdl3_app_pump(sdl3_app &a)
{
    if (!a.inited) return false;
    if (a.wait_when_idle && a.ctx && !needs_redraw(a.ctx))
    {
        // Idle: sleep until something happens (bounded, so housekeeping —
        // keyed-state sweeps, the caret — stays alive), then drain the burst.
        SDL_Event e;
        if (SDL_WaitEventTimeout(&e, 120))
        {
            if (sdl3_route(a.ctx, &e)) return false;
        }
        while (SDL_PollEvent(&e))
        {
            if (sdl3_route(a.ctx, &e)) return false;
        }
    }
    if (sdl3_pump(a.ctx)) return false; // the app should stop
    if (a.win)
    {
        // keep the client rect in sync with SDL (drag-resize, maximize)
        int x = 0, y = 0, ww = 0, wh = 0;
        SDL_GetWindowPosition(static_cast<SDL_Window *>(a.sdl_window), &x, &y);
        SDL_GetWindowSize(static_cast<SDL_Window *>(a.sdl_window), &ww, &wh);
        if (static_cast<f32>(ww) != a.win->client.w || static_cast<f32>(wh) != a.win->client.h ||
            static_cast<f32>(x) != a.win->client.x || static_cast<f32>(y) != a.win->client.y)
            set_window_client(*a.win, rect::make(static_cast<f32>(x), static_cast<f32>(y),
                                                 static_cast<f32>(ww), static_cast<f32>(wh)));
        if (a.win->close_requested) return false;
        a.width = static_cast<i32>(a.win->area.w);
        a.height = static_cast<i32>(a.win->area.h);
    }
    return true;
}

void sdl3_app_tick(sdl3_app &a)
{
    if (!a.inited) return;
    const u64 counter = SDL_GetPerformanceCounter();
    a.freq = static_cast<f64>(SDL_GetPerformanceFrequency());
    if (a.last_counter != 0)
        a.dt = static_cast<f64>(counter - a.last_counter) / a.freq;
    else
        a.dt = 1.0 / 60.0;
    if (a.dt <= 0.0) a.dt = 1.0 / 60.0;
    a.last_counter = counter;
    a.now += a.dt;
}

window *sdl3_app_open_window(sdl3_app &a, const char *title, i32 w, i32 h,
                             const sdl3_window_opts &opts)
{
    if (!a.inited || !a.ctx || !a.device || !title || w <= 0 || h <= 0) return nullptr;
    const bool hidden = opts.hidden || a.hidden;
    const SDL_WindowFlags flags = SDL_WINDOW_BORDERLESS | SDL_WINDOW_RESIZABLE |
                                  SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                  (hidden ? SDL_WINDOW_HIDDEN : static_cast<SDL_WindowFlags>(0));
    SDL_Window *sdl = SDL_CreateWindow(title, w, h, flags);
    if (!sdl) return nullptr;
    if (opts.x >= 0 && opts.y >= 0) SDL_SetWindowPosition(sdl, opts.x, opts.y);
    render_surface *surface = a.device->create_surface(sdl);
    window *win = nullptr;
    if (surface)
    {
        int x = 0, y = 0;
        SDL_GetWindowPosition(sdl, &x, &y);
        win = add_window(a.ctx, sdl, surface,
                         rect::make(static_cast<f32>(x), static_cast<f32>(y), static_cast<f32>(w),
                                    static_cast<f32>(h)));
    }
    if (!win) // no surface (single-window backend) or refused by add_window
    {
        if (surface) a.device->destroy_surface(surface);
        SDL_DestroyWindow(sdl);
        return nullptr;
    }
    install_window_chrome(a.ctx, *win); // native drag/resize/snap + the window's scale
    if (!hidden) SDL_StartTextInput(sdl);
    (void)sdl3_enable_uia(*win); // screen readers (Windows); no cost until queried
    return win;
}

void sdl3_app_close_window(sdl3_app &a, window &w)
{
    if (!a.inited || !a.ctx || &w == a.win || w.owner != a.ctx) return;
    SDL_Window *sdl = static_cast<SDL_Window *>(w.handle);
    render_surface *surface = w.surface;
    remove_window(a.ctx, w); // refused (and reported) while a frame is open
    if (window_at(a.ctx, sdl)) return;
    if (a.device && surface) a.device->destroy_surface(surface);
    if (sdl) SDL_DestroyWindow(sdl);
}

image_handle sdl3_load_image(context *c, const char *path)
{
    if (!c || !path) return {};
    SDL_Surface *loaded = SDL_LoadSurface(path);
    if (!loaded) return {};
    SDL_Surface *s = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(loaded);
    if (!s) return {};
    // Rows may be padded (pitch); create_image takes tight RGBA8 rows.
    std::vector<u8> px(static_cast<usize>(s->w) * static_cast<usize>(s->h) * 4);
    const usize row = static_cast<usize>(s->w) * 4;
    for (i32 y = 0; y < s->h; ++y)
        std::memcpy(px.data() + static_cast<usize>(y) * row,
                    static_cast<const u8 *>(s->pixels) + static_cast<usize>(y) * s->pitch, row);
    const image_handle img = create_image(c, s->w, s->h, px.data());
    SDL_DestroySurface(s);
    return img;
}

void sdl3_file_dialog(context *c, uiid id, const file_dialog_opts &opts)
{
    if (!c) return;
    if (sdl3_dialog_event_type == 0) sdl3_dialog_event_type = SDL_RegisterEvents(1);
    if (sdl3_dialog_event_type == 0)
    {
        deliver_file_dialog(c, id, nullptr, -1); // no event slot: report a failure
        return;
    }
    auto *p = new sdl3_dialog_payload();
    p->ctx = c;
    p->id = id;
    p->filter_text.reserve(opts.filters.size() * 2); // stable c_str() pointers below
    for (const file_filter &f : opts.filters)
    {
        p->filter_text.emplace_back(f.name ? f.name : "");
        p->filter_text.emplace_back(f.pattern ? f.pattern : "*");
    }
    for (usize i = 0; i < opts.filters.size(); ++i)
        p->filters.push_back({p->filter_text[i * 2].c_str(), p->filter_text[i * 2 + 1].c_str()});
    if (opts.default_location) p->location = opts.default_location;
    const char *loc = opts.default_location ? p->location.c_str() : nullptr;
    SDL_Window *parent = opts.parent ? static_cast<SDL_Window *>(opts.parent->handle) : nullptr;
    const int nf = static_cast<int>(p->filters.size());
    const SDL_DialogFileFilter *fl = nf > 0 ? p->filters.data() : nullptr;
    switch (opts.kind)
    {
    case file_dialog_kind::OPEN:
        SDL_ShowOpenFileDialog(sdl3_dialog_callback, p, parent, fl, nf, loc, opts.allow_many);
        break;
    case file_dialog_kind::SAVE:
        SDL_ShowSaveFileDialog(sdl3_dialog_callback, p, parent, fl, nf, loc);
        break;
    case file_dialog_kind::FOLDER:
        SDL_ShowOpenFolderDialog(sdl3_dialog_callback, p, parent, loc, opts.allow_many);
        break;
    }
}

void sdl3_apply_window_state(window &w, const window_state &s)
{
    SDL_Window *sdl = static_cast<SDL_Window *>(w.handle);
    if (!sdl || s.w <= 0 || s.h <= 0) return;
    // Usable display rects, the primary display first (the clamp's fallback).
    std::vector<rect> displays;
    int n = 0;
    SDL_DisplayID *ids = SDL_GetDisplays(&n);
    const SDL_DisplayID primary = SDL_GetPrimaryDisplay();
    for (int pass = 0; pass < 2 && ids; ++pass)
        for (int i = 0; i < n; ++i)
        {
            if ((ids[i] == primary) != (pass == 0)) continue;
            SDL_Rect b{};
            if (SDL_GetDisplayUsableBounds(ids[i], &b))
                displays.push_back(rect::make(static_cast<f32>(b.x), static_cast<f32>(b.y),
                                              static_cast<f32>(b.w), static_cast<f32>(b.h)));
        }
    SDL_free(ids);
    const rect r = clamp_to_displays(rect::make(static_cast<f32>(s.x), static_cast<f32>(s.y),
                                                static_cast<f32>(s.w), static_cast<f32>(s.h)),
                                     displays);
    if (SDL_GetWindowFlags(sdl) & SDL_WINDOW_MAXIMIZED) SDL_RestoreWindow(sdl);
    SDL_SetWindowSize(sdl, static_cast<int>(r.w), static_cast<int>(r.h));
    SDL_SetWindowPosition(sdl, static_cast<int>(r.x), static_cast<int>(r.y));
    set_window_client(w, r); // the core sees it now; the pump confirms it later
    if (s.maximized) SDL_MaximizeWindow(sdl);
}

void sdl3_app_shutdown(sdl3_app &a)
{
    if (!a.inited) return;
    // Extra windows first (the primary's renderer is the app's, below).
    while (a.ctx && a.ctx->window_count > 0)
    {
        const i32 before = a.ctx->window_count;
        window *last = a.ctx->windows[before - 1];
        if (last == a.win) break;
        sdl3_app_close_window(a, *last);
        if (a.ctx->window_count == before) break; // refused (a frame is open)
    }
    if (a.ctx) destroy_context(a.ctx);
    if (a.device) destroy_sdl3_device(a.device);
    if (a.sdl_renderer) SDL_DestroyRenderer(static_cast<SDL_Renderer *>(a.sdl_renderer));
    if (a.sdl_window) SDL_DestroyWindow(static_cast<SDL_Window *>(a.sdl_window));
    SDL_Quit();
    a.ctx = nullptr;
    a.device = nullptr;
    a.surface = nullptr;
    a.sdl_renderer = nullptr;
    a.sdl_window = nullptr;
    a.win = nullptr;
    a.inited = false;
}
#endif

void install_window_chrome(context *c, window &w)
{
    if (!c) return;
#if defined(PUFFERUI_ENABLE_SDL3)
    // The SDL3 host installs itself lazily on the first routed event, which
    // is AFTER the app created its windows - ensure it exists here so the
    // chrome install cannot be silently skipped.
    if (!c->host) set_window_host(c, sdl3_host_instance());
    if (c->host == sdl3_host_instance() && w.handle)
    {
        sdl3_default_hints();
        sdl3_sync_scale(w, static_cast<SDL_Window *>(w.handle));
    }
#endif
    if (!c->host) return;
    c->host->install_system_chrome(w);
}
