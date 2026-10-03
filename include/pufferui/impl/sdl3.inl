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
    std::vector<u8> pixels;            // streaming: RGBA mirror
    std::vector<SDL_Texture *> copies; // one per device slot
};

struct sdl3_device;

struct sdl3_surface : render_surface
{
    sdl3_device *dev = nullptr;
    i32 slot = -1;

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
                SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
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
               backend_caps::STREAMING_TEXTURES | backend_caps::SHARED_DEVICE;
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
            SDL_SetRenderVSync(ren, vsync_enabled ? 1 : 0);
        }
        else
        {
            if (!win) return nullptr;
            ren = SDL_CreateRenderer(win, nullptr);
            if (!ren) return nullptr;
            owns = true;
            SDL_SetRenderVSync(ren, vsync_enabled ? 1 : 0);
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
        return s;
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
        for (slot &sl : slots)
            if (sl.renderer) SDL_SetRenderVSync(sl.renderer, enabled ? 1 : 0);
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
    sdl3_device::slot &sl = d.slots[static_cast<usize>(slot)];
    if (!sl.renderer) return;
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
    const f32 lx = static_cast<f32>(area->x), ly = static_cast<f32>(area->y);
    if (!w->maximized)
    {
        // Generous native resize borders: the corner squares claim both axes
        // at once and take precedence over the straight edges. A maximized
        // window keeps no resize bands (its state is fixed), but its caption
        // below stays draggable - the system restores it during the native
        // drag.
        constexpr f32 edge = 8.0f, corner = 20.0f;
        const bool left = lx <= edge, right = lx >= w->area.w - edge;
        const bool top = ly <= edge, bottom = ly >= w->area.h - edge;
        if (top && left) return SDL_HITTEST_RESIZE_TOPLEFT;
        if (top && right) return SDL_HITTEST_RESIZE_TOPRIGHT;
        if (bottom && left) return SDL_HITTEST_RESIZE_BOTTOMLEFT;
        if (bottom && right) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
        if (left) return SDL_HITTEST_RESIZE_LEFT;
        if (right) return SDL_HITTEST_RESIZE_RIGHT;
        if (top) return SDL_HITTEST_RESIZE_TOP;
        if (bottom) return SDL_HITTEST_RESIZE_BOTTOM;
    }
    // The bar minus the glyph buttons is the system caption. For a maximized
    // window SDL keeps the WS_MAXIMIZEBOX style, so the system's move loop
    // restores it when the caption is dragged.
    const f32 buttons_w = detail::CLOSE_BTN_W + detail::TITLE_BTN_W * 2.0f;
    if (ly <= detail::TITLEBAR_H && lx < w->area.w - buttons_w) return SDL_HITTEST_DRAGGABLE;
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
        SDL_SetWindowHitTest(win, sdl3_chrome_hit_test, w.owner);
    }
};

window_host *sdl3_host_instance()
{
    static sdl3_window_host host;
    return &host;
}
} // namespace

bool sdl3_route_impl(context *c, SDL_Event *e)
{
    if (!c || !e) return false;
    if (!window_host_of(c)) set_window_host(c, sdl3_host_instance());

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
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_MOVED:
    {
        SDL_Window *sdl = SDL_GetWindowFromID(e->window.windowID);
        window *w = sdl ? window_at(c, sdl) : nullptr;
        if (w)
        {
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
        SDL_Window *sdl = SDL_GetWindowFromID(e->motion.windowID);
        if (window *w = sdl ? window_at(c, sdl) : nullptr) mouse_move(*w, e->motion.x, e->motion.y);
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    {
        SDL_Window *sdl = SDL_GetWindowFromID(e->button.windowID);
        window *w = sdl ? window_at(c, sdl) : nullptr;
        if (!w) break;
        mouse_move(*w, e->button.x, e->button.y); // buttons carry coordinates
        const bool down = e->type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        if (e->button.button == SDL_BUTTON_LEFT)
            mouse_button(*w, down);
        else if (e->button.button == SDL_BUTTON_RIGHT)
            mouse_button(*w, pointer_button::RIGHT, down);
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL:
    {
        SDL_Window *sdl = SDL_GetWindowFromID(e->wheel.windowID);
        if (window *w = sdl ? window_at(c, sdl) : nullptr) mouse_wheel(*w, e->wheel.x, e->wheel.y);
        break;
    }
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
    {
        const bool down = e->type == SDL_EVENT_KEY_DOWN;
        window *w = window_at(c, SDL_GetWindowFromID(e->key.windowID));
        if (!w) w = focused_window(c);
        if (!w) break;
        mods_event(*w, (e->key.mod & SDL_KMOD_SHIFT) != 0, (e->key.mod & SDL_KMOD_CTRL) != 0);
        key k = key::ESCAPE;
        bool mapped = true;
        switch (e->key.key)
        {
        case SDLK_ESCAPE:
            k = key::ESCAPE;
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            k = key::ENTER;
            break;
        case SDLK_TAB:
            k = key::TAB;
            break;
        case SDLK_BACKSPACE:
            k = key::BACKSPACE;
            break;
        case SDLK_SPACE:
            k = key::SPACE;
            break;
        case SDLK_LEFT:
            k = key::LEFT;
            break;
        case SDLK_RIGHT:
            k = key::RIGHT;
            break;
        case SDLK_UP:
            k = key::UP;
            break;
        case SDLK_DOWN:
            k = key::DOWN;
            break;
        case SDLK_HOME:
            k = key::HOME;
            break;
        case SDLK_END:
            k = key::END;
            break;
        case SDLK_A:
            k = key::A;
            break;
        case SDLK_C:
            k = key::C;
            break;
        case SDLK_X:
            k = key::X;
            break;
        case SDLK_V:
            k = key::V;
            break;
        case SDLK_Z:
            k = key::Z;
            break;
        case SDLK_Y:
            k = key::Y;
            break;
        case SDLK_DELETE:
            k = key::DEL;
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
    case SDL_EVENT_TEXT_EDITING:
    {
        SDL_Window *sdl = SDL_GetWindowFromID(e->edit.windowID);
        if (window *w = sdl ? window_at(c, sdl) : nullptr)
            ime_event(*w, e->edit.text, e->edit.start);
        break;
    }
    default:
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
    a.sdl_window = SDL_CreateWindow(title, w, h, flags);
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
    a.win = a.ctx ? add_window(a.ctx, a.sdl_window, a.surface,
                               rect::make(0, 0, static_cast<f32>(w), static_cast<f32>(h)))
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

    a.width = w;
    a.height = h;
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

void sdl3_app_shutdown(sdl3_app &a)
{
    if (!a.inited) return;
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
#endif
    if (!c->host) return;
    c->host->install_system_chrome(w);
}
