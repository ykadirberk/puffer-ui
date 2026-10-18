// pufferui/impl/draw_context.inl - part of the PufferUI implementation (see pufferui.h).
// Included by pufferui.h inside `namespace pui` when PUFFERUI_IMPLEMENTATION is
// defined; never include it directly. The slices concatenate to the original
// implementation block in order (tools/amalgamate.ps1 rebuilds the single header).
// ---- draw list (batching by clip + texture) ----
struct draw_list
{
    std::vector<vertex> verts;
    std::vector<i32> idx;
    texture_handle cur_tex = nullptr;
    bool tex_known = false;
    bool clip_active = false;
    rect clip{};
    bool clip_known = false;
    bool solid_uv_remap = false; // the in-flight draw_triangles call is solid
    // Reusable scratch for primitives that build local geometry before the
    // batch copy (draw_sector / draw_polygon): no per-call heap allocation
    // after warm-up.
    std::vector<vertex> scratch_v;
    std::vector<i32> scratch_i;
};

// Open-addressing map with inline values: power-of-two capacity, linear
// probing, no tombstones (wholesale clears or rebuilds only). Keys are uiid
// hashes (u64); the value type must be trivially relocatable enough for the
// growth re-insert. O(1) expected; two cache lines worst case.

namespace detail
{

inline void dl_flush(context *c)
{
    if (!c->dl) return;
    draw_list &dl = *c->dl;
    if (!dl.verts.empty() && c->device)
    {
        c->device->draw(dl.tex_known ? dl.cur_tex : nullptr, dl.verts.data(),
                        static_cast<i32>(dl.verts.size()), dl.idx.data(),
                        static_cast<i32>(dl.idx.size()));
    }
    dl.verts.clear();
    dl.idx.clear();
}

inline void dl_prepare(context *c, texture_handle tex, const rect *clip)
{
    if (!c->dl) return;
    draw_list &dl = *c->dl;

    bool want_active = clip != nullptr && clip->w > 0.0f && clip->h > 0.0f;
    rect want = want_active ? *clip : rect{};
    bool clip_changed = !dl.clip_known || dl.clip_active != want_active ||
                        (want_active && (dl.clip.x != want.x || dl.clip.y != want.y ||
                                         dl.clip.w != want.w || dl.clip.h != want.h));
    if (clip_changed)
    {
        dl_flush(c);
        const f32 s = c->render_scale;
        const rect pixels{want.x * s, want.y * s, want.w * s, want.h * s};
        if (c->device) c->device->set_clip(want_active ? &pixels : nullptr);
        dl.clip_active = want_active;
        dl.clip = want;
        dl.clip_known = true;
    }
    if (!dl.tex_known || dl.cur_tex != tex)
    {
        dl_flush(c);
        dl.cur_tex = tex;
        dl.tex_known = true;
    }
}

// The area a draw can still change: the open window frame's client area
// intersected with the active clip. A zero-size client area paints nothing. False
// when nothing bounds the draw (no frame open, no clip): `screen` is only
// meaningful while a frame is open, it is left stale after end_frame.
inline bool paint_bounds(const context *c, rect &out)
{
    bool bounded = false;
    rect r{};
    if (c->current_window)
    {
        // A popup recording into an overlay paints its own rect, which lies
        // (partly) outside the window by definition.
        r = c->overlay_depth > 0 ? c->overlay_area : c->screen;
        bounded = true;
    }
    if (c->clip_depth > 0)
    {
        const rect &top = c->clip_stack[c->clip_depth - 1];
        r = bounded ? rect::intersect(r, top) : top;
        bounded = true;
    }
    out = r;
    return bounded;
}

// True when a draw confined to `box` (feather included) cannot change a pixel: it
// lies wholly outside the client area or the active clip, or the clip is empty.
// Callers skip the draw (and the geometry they would build) entirely.
inline bool outside_paint(const context *c, const rect &box)
{
    rect pb;
    if (!paint_bounds(c, pb)) return false;
    if (pb.w <= 0.0f || pb.h <= 0.0f) return true;
    return box.right() <= pb.x || box.x >= pb.right() || box.bottom() <= pb.y ||
           box.y >= pb.bottom();
}

// How far past a shape's geometric bounds its antialiasing feather can reach.
inline constexpr f32 FEATHER_REACH = 1.5f;

// outside_paint for a shape whose geometry (feather excluded) fits in `box`.
inline bool outside_paint_feathered(const context *c, const rect &box)
{
    return outside_paint(c, box.pad(-FEATHER_REACH));
}

inline void dl_add(context *c, const vertex *verts, i32 vcount, const i32 *idx, i32 icount)
{
    if (!c->dl || vcount <= 0 || icount <= 0) return;
    draw_list &dl = *c->dl;
    i32 base = static_cast<i32>(dl.verts.size());
    dl.verts.insert(dl.verts.end(), verts, verts + vcount);
    if (c->render_scale != 1.0f)
        for (usize i = dl.verts.size() - static_cast<usize>(vcount); i < dl.verts.size(); ++i)
        {
            dl.verts[i].x *= c->render_scale;
            dl.verts[i].y *= c->render_scale;
        }
    if (c->disabled_depth > 0)
        for (usize i = dl.verts.size() - static_cast<usize>(vcount); i < dl.verts.size(); ++i)
            dl.verts[i].c.a =
                static_cast<u8>(dl.verts[i].c.a * clampf(c->disabled_alpha, 0.0f, 1.0f));
    if (dl.solid_uv_remap)
    {
        // Solid geometry rides in the glyph batch: remap the copied verts' UVs
        // to the atlas's reserved white texel at (0,0) (its center, so any
        // sampler/filtering returns exactly that texel). The copy already
        // happened, so this is arithmetic on the way in — no extra pass cost
        // beyond the uv writes.
        const f32 wx = 0.5f / static_cast<f32>(c->ts->atlas_w);
        const f32 wy = 0.5f / static_cast<f32>(c->ts->atlas_h);
        const usize first = dl.verts.size() - static_cast<usize>(vcount);
        for (usize i = first; i < dl.verts.size(); ++i)
        {
            dl.verts[i].u = wx;
            dl.verts[i].v = wy;
        }
    }
    // Let vector grow geometrically. Reserving exactly size + icount here
    // relocated the entire batch once per primitive while warming up.
    for (i32 i = 0; i < icount; ++i) dl.idx.push_back(idx[i] + base);
}

} // namespace detail

// ---- popup overlays (window_host::open_overlay) ----
namespace detail
{
inline overlay_entry *find_overlay(context *c, const window *w, uiid id)
{
    for (overlay_entry *e : c->st->overlays)
        if (e->parent == w && e->id == id) return e;
    return nullptr;
}

// Where popups may be placed: the display's usable area when overlays are
// available, else the window.
inline rect popup_space(context *c)
{
    rect r{};
    window *w = c->current_window;
    if (!w || !c->host || !c->popup_overlays || c->overlay_depth > 0) return c->screen;
    for (const overlay_entry *e : c->st->overlays)
        if (e->parent == w && e->failed) return c->screen; // the host could not: stay inside
    if (c->host->popup_bounds(*w, r) && r.w > 0.0f && r.h > 0.0f) return r;
    return c->screen;
}

// Starts recording a spilling popup into its overlay; false = draw in-window.
inline bool overlay_begin(context *c, uiid id, rect area)
{
    window *w = c->current_window;
    if (!w || !c->host || !c->popup_overlays || c->overlay_depth > 0 || !c->device || !c->dl)
        return false;
    if (area.w <= 0.0f || area.h <= 0.0f) return false;
    overlay_entry *e = find_overlay(c, w, id);
    if (!e)
    {
        e = new overlay_entry();
        e->parent = w;
        e->id = id;
        e->failed = !c->host->open_overlay(*w, area, e->h) || !e->h.surface;
        e->placed = area;
        c->st->overlays.push_back(e);
    }
    e->used = true;
    if (e->failed) return false; // the host cannot: in-window (retried after it closes)
    if (e->placed.x != area.x || e->placed.y != area.y || e->placed.w != area.w ||
        e->placed.h != area.h)
    {
        c->host->move_overlay(*w, area, e->h);
        e->placed = area;
    }
    e->area = area;
    e->rec.reset(area.x * w->scale, area.y * w->scale); // the overlay's origin, pixels
    e->rec.real = c->device;
    dl_flush(c); // what was drawn so far belongs to the window
    c->st->overlay_real = c->device;
    c->device = &e->rec;
    c->dl->clip_known = false; // re-issue clip/texture state into the recorder
    c->dl->tex_known = false;
    c->overlay_depth = 1;
    c->overlay_area = area;
    return true;
}

inline void overlay_end(context *c)
{
    if (c->overlay_depth == 0) return;
    dl_flush(c);
    c->device = c->st->overlay_real;
    c->st->overlay_real = nullptr;
    c->dl->clip_known = false;
    c->dl->tex_known = false;
    c->overlay_depth = 0;
}

inline void overlay_close(context *c, overlay_entry *e)
{
    if (c->host && e->h.native && e->parent) c->host->close_overlay(*e->parent, e->h);
    delete e;
}

// After `w` presented: replay its overlays onto their surfaces; close the ones
// no popup drew this frame.
inline void overlays_present(context *c, window &w)
{
    std::vector<overlay_entry *> &list = c->st->overlays;
    for (usize i = 0; i < list.size();)
    {
        overlay_entry *e = list[i];
        if (e->parent != &w)
        {
            ++i;
            continue;
        }
        if (!e->used)
        {
            overlay_close(c, e);
            list.erase(list.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        e->used = false;
        if (!e->failed && c->device && e->h.surface)
        {
            render_device &dev = *c->device;
            e->h.surface->make_current(dev);
            dev.clear(color{0, 0, 0, 0}); // transparent around rounded corners
            e->rec.replay(dev);
            e->h.surface->present();
        }
        ++i;
    }
}

// Closes `w`'s overlays (every overlay when `w` is null).
inline void overlays_close(context *c, const window *w)
{
    std::vector<overlay_entry *> &list = c->st->overlays;
    for (usize i = 0; i < list.size();)
    {
        if (w && list[i]->parent != w)
        {
            ++i;
            continue;
        }
        overlay_close(c, list[i]);
        list.erase(list.begin() + static_cast<std::ptrdiff_t>(i));
    }
}
} // namespace detail

void set_accessibility(context *c, bool enabled)
{
    if (c) c->a11y_enabled = enabled;
}

i32 a11y_count(const window &w)
{
    return w.a11y ? static_cast<i32>(w.a11y->nodes.size()) : 0;
}

a11y_node_view a11y_node(const window &w, i32 index)
{
    a11y_node_view v;
    if (!w.a11y || index < 0 || index >= static_cast<i32>(w.a11y->nodes.size())) return v;
    const a11y_tree::node &n = w.a11y->nodes[static_cast<usize>(index)];
    v.id = n.id;
    v.role = n.role;
    v.bounds = n.bounds;
    v.state = n.state;
    v.name = n.name;
    v.value = n.value;
    return v;
}

void a11y_focus(window &w, uiid id)
{
    if (w.owner && w.owner->current_window == &w)
        w.owner->focus_request = id;
    else
        w.focus_request = id;
}

void set_popup_overlays(context *c, bool enabled)
{
    if (!c) return;
    c->popup_overlays = enabled;
    if (!enabled && !c->current_window) detail::overlays_close(c, nullptr);
}

// ---- context ----
context *create_context(render_device *device, render_surface *surface)
{
    context *c = new context();
    c->device = device;
    c->surface = surface;
    c->st = new ctx_stores();
    c->popups = new popup_entry[MAX_POPUPS];
    c->prev_popups = new popup_entry[MAX_POPUPS];
    c->popup_capacity = MAX_POPUPS;
    c->panels = new panel_entry[MAX_PANELS];
    c->prev_panels = new panel_entry[MAX_PANELS];
    c->panel_capacity = MAX_PANELS;
    c->panel_stack = new uiid[MAX_PANELS];
    c->panel_stack_capacity = MAX_PANELS;
    c->dl = new draw_list();
    set_current_context(c);
    return c;
}

namespace detail
{

inline void free_blur_store(context *c, blur_store *&bs)
{
    if (!bs) return;
    if (c->device)
    {
        for (texture_handle t : {bs->half, bs->quarter, bs->eighth, bs->sixteenth})
            if (t) c->device->destroy_target(t);
    }
    delete bs;
    bs = nullptr;
}

inline window *ensure_primary(context *c)
{
    if (c->window_count > 0) return c->windows[0];
    return add_window(c, nullptr, c->surface, rect{0, 0, 0, 0});
}

} // namespace detail

void destroy_context(context *c)
{
    if (!c) return;
    PUFFERUI_CHECK(VIOL_DESTROY_WHILE_OPEN, c->current_window == nullptr,
                   "destroy_context with an open window frame");
    if (current_context() == c) set_current_context(nullptr);
    if (c->ts)
    {
        for (text_store::atlas_page &page : c->ts->atlases)
            if (c->device && page.tex) c->device->destroy_texture(page.tex);
        delete c->ts;
    }
    detail::overlays_close(c, nullptr);
    delete c->st;
    delete[] c->popups;
    delete[] c->prev_popups;
    delete[] c->panels;
    delete[] c->prev_panels;
    delete[] c->panel_stack;
    delete c->focus_store;
    c->focus_store = nullptr;
    for (i32 i = 0; i < c->window_count; ++i)
    {
        window *w = c->windows[i];
        if (w->on_a11y_detach) w->on_a11y_detach(*w);
        delete w->a11y;
        delete w->focus_store;
        detail::free_blur_store(c, w->blur);
        delete w;
    }
    detail::free_blur_store(c, c->blur);
    delete c->dl;
    delete[] c->windows;
    c->windows = nullptr;
    c->window_capacity = 0;
    delete c;
}

void set_device(context *c, render_device *device, render_surface *surface)
{
    c->device = device;
    c->surface = surface;
    for (i32 i = 0; i < c->window_count; ++i)
        if (!c->windows[i]->surface) c->windows[i]->surface = surface;
}
void set_theme(context *c, const theme &t)
{
    c->active_theme = t;
}

// ---- windows ----
namespace detail
{
// The per-window root id: derived from a serial that is never reused, so a
// window keeps its ids (and widget state) when others come and go, and a new
// window never inherits a removed one's state.
inline uiid window_root(u64 serial)
{
    return id_child(0x77696E646F770000ull, static_cast<uiid>(serial));
}

// Grow the list of window pointers (the windows themselves never move).
inline void grow_windows(context *c)
{
    const i32 new_cap = (c->window_capacity > 0) ? c->window_capacity * 2 : MAX_WINDOWS;
    window **fresh = new window *[new_cap];
    for (i32 i = 0; i < c->window_count; ++i) fresh[i] = c->windows[i];
    delete[] c->windows;
    c->windows = fresh;
    c->window_capacity = new_cap;
}

// `w` is one of `c`'s registered windows.
inline bool owns_window(const context *c, const window &w)
{
    return w.owner == c && w.index >= 0 && w.index < c->window_count && c->windows[w.index] == &w;
}
} // namespace detail

window *add_window(context *c, void *handle, render_surface *surface, rect client)
{
    if (c->window_count >= c->window_capacity) detail::grow_windows(c);
    if (c->window_count > 0 && c->device &&
        !has_cap(c->device->caps(), backend_caps::SHARED_DEVICE))
    {
        PUFFERUI_CHECK(VIOL_SHARED_DEVICE, false,
                       "a second window requires backend_caps::SHARED_DEVICE");
        return nullptr;
    }
    window &w = *(c->windows[c->window_count] = new window());
    w.owner = c;
    w.handle = handle;
    w.surface = surface ? surface : c->surface;
    w.index = c->window_count;
    set_window_client(w, client);
    w.root = detail::window_root(++c->window_serial);
    c->window_count += 1;
    if (!c->focused_win) focus_window(c, w);
    return &w;
}

void remove_window(context *c, window &w)
{
    if (c->current_window != nullptr)
    {
        PUFFERUI_CHECK(VIOL_REMOVE_WHILE_OPEN, false, "remove_window while a frame is open");
        return;
    }
    if (!detail::owns_window(c, w)) return;
    const i32 i = w.index;
    const bool was_focused = c->focused_win == &w;
    detail::overlays_close(c, &w);
    if (w.on_a11y_detach) w.on_a11y_detach(w);
    delete w.a11y;
    delete w.focus_store;
    detail::free_blur_store(c, w.blur);
    delete &w;
    for (i32 j = i; j < c->window_count - 1; ++j) c->windows[j] = c->windows[j + 1];
    c->window_count -= 1;

    // Drop this window's last-frame popup/panel entries and remap later indices,
    // so input capture arrays stay in sync with the window list.
    for (i32 k = 0; k < c->popup_depth; ++k)
    {
        if (c->popups[k].win == i)
        {
            for (i32 m = k; m < c->popup_depth - 1; ++m) c->popups[m] = c->popups[m + 1];
            c->popup_depth -= 1;
            --k;
        }
        else if (c->popups[k].win > i)
        {
            c->popups[k].win -= 1;
        }
    }
    for (i32 k = 0; k < c->panel_depth; ++k)
    {
        if (c->panels[k].win == i)
        {
            for (i32 m = k; m < c->panel_depth - 1; ++m) c->panels[m] = c->panels[m + 1];
            c->panel_depth -= 1;
            --k;
        }
        else if (c->panels[k].win > i)
        {
            c->panels[k].win -= 1;
        }
    }

    for (i32 j = 0; j < c->window_count; ++j) c->windows[j]->index = j;
    if (was_focused)
    {
        // Keyboard focus moves to the window that took the removed one's slot.
        c->focused_win = c->window_count > 0
                             ? c->windows[i < c->window_count ? i : c->window_count - 1]
                             : nullptr;
        for (i32 j = 0; j < c->window_count; ++j)
            c->windows[j]->focused = (c->focused_win == c->windows[j]);
    }
}

window *window_at(context *c, void *handle)
{
    for (i32 i = 0; i < c->window_count; ++i)
        if (c->windows[i]->handle == handle) return c->windows[i];
    return nullptr;
}

i32 window_count(const context *c)
{
    return c ? c->window_count : 0;
}

window *window_by_index(context *c, i32 index)
{
    return c && index >= 0 && index < c->window_count ? c->windows[index] : nullptr;
}

#define PUI_STR2(x) #x
#define PUI_STR(x) PUI_STR2(x)
const char *version()
{
    return PUI_STR(PUFFERUI_VERSION_MAJOR) "." PUI_STR(PUFFERUI_VERSION_MINOR) "." PUI_STR(
        PUFFERUI_VERSION_PATCH) "." PUI_STR(PUFFERUI_VERSION_TWEAK);
}
#undef PUI_STR
#undef PUI_STR2

window *first_window(context *c)
{
    return c->window_count > 0 ? c->windows[0] : nullptr;
}

void set_window_client(window &w, rect client)
{
    w.client = client;
    w.area = rect::make(0.0f, 0.0f, client.w / w.input_scale, client.h / w.input_scale);
}

void set_window_scale(window &w, f32 scale, const scale_opts &opts)
{
    if (!std::isfinite(scale) || scale <= 0.0f) return;
    const f32 input = opts.input_scale == 0.0f ? scale : opts.input_scale;
    if (!std::isfinite(input) || input <= 0.0f) return;
    // Change scale between frames so clipping, input and geometry use one scale.
    if (w.owner && w.owner->current_window) return;
    w.scale = scale;
    w.input_scale = input;
    set_window_client(w, w.client);
    if (w.owner) w.owner->redraw_requested = true;
}

void focus_window(context *c, window &w)
{
    if (!detail::owns_window(c, w)) return;
    c->focused_win = &w;
    for (i32 i = 0; i < c->window_count; ++i) c->windows[i]->focused = (c->windows[i] == &w);
}

window *focused_window(context *c)
{
    return c->focused_win;
}

i32 days_in_month(i32 year, i32 month)
{
    static const i32 days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 0;
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return days[month - 1] + (month == 2 && leap ? 1 : 0);
}

i32 weekday(date d)
{
    // Sakamoto's method (0 = Sunday), shifted so Monday is 0.
    static const i32 t[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    i32 y = d.year - (d.month < 3 ? 1 : 0);
    const i32 sunday0 = (y + y / 4 - y / 100 + y / 400 + t[(d.month - 1) % 12] + d.day) % 7;
    return (sunday0 + 6) % 7;
}

date add_days(date d, i32 days)
{
    while (days > 0)
    {
        const i32 left = days_in_month(d.year, d.month) - d.day;
        if (days <= left)
        {
            d.day += days;
            return d;
        }
        days -= left + 1;
        d.day = 1;
        if (++d.month > 12)
        {
            d.month = 1;
            ++d.year;
        }
    }
    while (days < 0)
    {
        if (-days < d.day)
        {
            d.day += days;
            return d;
        }
        days += d.day;
        if (--d.month < 1)
        {
            d.month = 12;
            --d.year;
        }
        d.day = days_in_month(d.year, d.month);
    }
    return d;
}

date add_months(date d, i32 months)
{
    i32 m = d.year * 12 + (d.month - 1) + months;
    d.year = m >= 0 ? m / 12 : -((-m + 11) / 12);
    d.month = m - d.year * 12 + 1;
    const i32 dim = days_in_month(d.year, d.month);
    if (d.day > dim) d.day = dim;
    return d;
}

hsv_color to_hsv(color c)
{
    const f32 r = c.r / 255.0f, g = c.g / 255.0f, b = c.b / 255.0f;
    const f32 mx = max2(r, max2(g, b)), mn = min2(r, min2(g, b));
    const f32 d = mx - mn;
    hsv_color out;
    out.v = mx;
    out.s = mx > 0.0f ? d / mx : 0.0f;
    if (d > 0.0f)
    {
        f32 h = mx == r ? (g - b) / d : mx == g ? 2.0f + (b - r) / d : 4.0f + (r - g) / d;
        h *= 60.0f;
        if (h < 0.0f) h += 360.0f;
        out.h = h;
    }
    return out;
}

color from_hsv(hsv_color c, u8 alpha)
{
    const f32 h = std::fmod(std::fmod(c.h, 360.0f) + 360.0f, 360.0f) / 60.0f;
    const f32 s = clampf(c.s, 0.0f, 1.0f), v = clampf(c.v, 0.0f, 1.0f);
    const i32 i = static_cast<i32>(h) % 6;
    const f32 f = h - std::floor(h);
    const f32 p = v * (1.0f - s), q = v * (1.0f - s * f), t = v * (1.0f - s * (1.0f - f));
    f32 r = v, g = t, b = p;
    switch (i)
    {
    case 1:
        r = q, g = v, b = p;
        break;
    case 2:
        r = p, g = v, b = t;
        break;
    case 3:
        r = p, g = q, b = v;
        break;
    case 4:
        r = t, g = p, b = v;
        break;
    case 5:
        r = v, g = p, b = q;
        break;
    default:
        break;
    }
    const auto to8 = [](f32 x) { return static_cast<u8>(clampf(x, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return color{to8(r), to8(g), to8(b), alpha};
}

window_state get_window_state(const window &w)
{
    window_state s;
    s.x = static_cast<i32>(w.client.x);
    s.y = static_cast<i32>(w.client.y);
    s.w = static_cast<i32>(w.client.w);
    s.h = static_cast<i32>(w.client.h);
    s.maximized = w.maximized;
    return s;
}

std::string save_window_state(const window_state &s)
{
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%d %d %d %d %d", s.x, s.y, s.w, s.h, s.maximized ? 1 : 0);
    return buf;
}

bool load_window_state(std::string_view text, window_state &out)
{
    i64 v[5]{};
    i32 n = 0;
    usize i = 0;
    while (n < 5)
    {
        while (i < text.size() && text[i] == ' ') ++i;
        if (i >= text.size()) break;
        bool neg = false;
        if (text[i] == '-')
        {
            neg = true;
            ++i;
        }
        if (i >= text.size() || text[i] < '0' || text[i] > '9') return false;
        i64 x = 0;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9' && x < 100000000)
            x = x * 10 + (text[i++] - '0');
        v[n++] = neg ? -x : x;
    }
    while (i < text.size() && (text[i] == ' ' || text[i] == '\n' || text[i] == '\r')) ++i;
    if (n != 5 || i != text.size() || v[2] <= 0 || v[3] <= 0 || v[4] < 0 || v[4] > 1) return false;
    out.x = static_cast<i32>(v[0]);
    out.y = static_cast<i32>(v[1]);
    out.w = static_cast<i32>(v[2]);
    out.h = static_cast<i32>(v[3]);
    out.maximized = v[4] == 1;
    return true;
}

rect clamp_to_displays(rect r, std::span<const rect> displays)
{
    if (displays.empty()) return r;
    // Visible enough to grab: a 64x32 piece (the titlebar's worth) on a display.
    for (const rect &d : displays)
    {
        const rect x = rect::intersect(r, d);
        if (x.w >= min2(64.0f, r.w) && x.h >= min2(32.0f, r.h)) return r;
    }
    const rect &d = displays[0];
    const f32 w = min2(r.w, d.w), h = min2(r.h, d.h);
    return rect::make(d.x + (d.w - w) * 0.5f, d.y + (d.h - h) * 0.5f, w, h);
}

void deliver_file_dialog(context *c, uiid id, const char *const *paths, i32 count)
{
    if (!c) return;
    ctx_stores::dialog_entry e;
    e.id = id;
    e.cancelled = count <= 0 || !paths;
    for (i32 i = 0; !e.cancelled && i < count; ++i)
    {
        if (!paths[i]) break;
        e.paths.append(paths[i]);
        e.paths.push_back('\0');
        e.count += 1;
    }
    for (ctx_stores::dialog_entry &old : c->st->dialogs)
        if (old.id == id)
        {
            old = std::move(e); // a newer result for the same id wins
            return;
        }
    c->st->dialogs.push_back(std::move(e));
}

dialog_result take_file_dialog(context *c, uiid id)
{
    dialog_result r;
    if (!c) return r;
    std::vector<ctx_stores::dialog_entry> &list = c->st->dialogs;
    for (usize i = 0; i < list.size(); ++i)
    {
        if (list[i].id != id) continue;
        c->st->dialog_taken = std::move(list[i].paths);
        r.done = true;
        r.cancelled = list[i].cancelled;
        r.count = list[i].count;
        r.paths_ = c->st->dialog_taken.data();
        r.len_ = static_cast<i32>(c->st->dialog_taken.size());
        list.erase(list.begin() + static_cast<std::ptrdiff_t>(i));
        return r;
    }
    return r;
}

rect desktop_rect(context *c)
{
    if (c->window_count == 0) return c->screen;
    rect u = c->windows[0]->client;
    for (i32 i = 1; i < c->window_count; ++i)
    {
        const rect &r = c->windows[i]->client;
        const f32 x0 = min2(u.x, r.x), y0 = min2(u.y, r.y);
        const f32 x1 = max2(u.right(), r.right()), y1 = max2(u.bottom(), r.bottom());
        u = rect::make(x0, y0, x1 - x0, y1 - y0);
    }
    return u;
}

void set_global_mouse(context *c, f32 x, f32 y)
{
    c->mouse_global_x = x;
    c->mouse_global_y = y;
}

namespace detail
{
// The virtual desktop in the current window's local coordinates (used to clamp
// floating panels so they can be dragged across windows).
inline rect desktop_local(context *c)
{
    if (!c->current_window) return c->screen;
    const rect &cl = c->current_window->client;
    const f32 s = c->current_window->input_scale;
    return rect::make((c->desktop.x - cl.x) / s, (c->desktop.y - cl.y) / s, c->desktop.w / s,
                      c->desktop.h / s);
}
} // namespace detail

// Tab focus moves through the ring built during the previous frame; defined
// with the edit helpers below.
namespace detail
{
inline void tab_move(context *c, uiid id, bool back);
}

// ---- frames ----
void begin_frame(context *c, window &w, f64 now, f64 dt)
{
    if (c->current_window != nullptr)
    {
        PUFFERUI_CHECK(VIOL_FRAME_ALREADY_OPEN, false, "begin_frame: another window frame is open");
        return;
    }
    if (!detail::owns_window(c, w))
    {
        PUFFERUI_CHECK(VIOL_WINDOW_NOT_REGISTERED, false, "begin_frame: window is not registered");
        return;
    }

    c->frame += 1;
    c->now = now;
    c->dt = dt;
    c->current_window = &w;
    c->render_scale = w.scale;
    c->screen = w.area;
    c->desktop = desktop_rect(c);

    // The pointer is one; each window derives its local position from the shared
    // desktop position, so cross-window drags keep working while another window
    // receives the motion events.
    c->mouse_x = (c->mouse_global_x - w.client.x) / w.input_scale;
    c->mouse_y = (c->mouse_global_y - w.client.y) / w.input_scale;
    // This frame's input and this window's persistent widget state come in as
    // whole structs; the wheel delta is consumed from the queue.
    static_cast<window_input &>(*c) = w.in;
    w.in.wheel_x = 0.0f;
    w.in.wheel_y = 0.0f;
    c->wheel_target = 0;
    // c->mouse_down is global: set by whichever window received the press.
    static_cast<interaction_state &>(*c) = w;
    c->hot = 0;
    c->redraw_requested = false;

    c->st->widget_ids.clear();
    c->st->frame_ids.clear();
    c->st->caption_drags.clear();
    c->st->caption_hits.clear();
    c->st->a11y_frame.count = 0;
    c->press_claim = 0;
    c->press_claim_rect = rect{};
    c->current_region = 0;
    c->clip_depth = 0;
    c->parent_depth = 0;
    c->seq = 0;

    // popups/panels: this window's previous-frame entries become the input
    // capture set; other windows keep theirs for their own frames.
    {
        const i32 wi = w.index;
        c->prev_popup_depth = 0;
        i32 keep = 0;
        for (i32 i = 0; i < c->popup_depth; ++i)
        {
            if (c->popups[i].win == wi)
            {
                detail::ensure_popup_capacity(c, c->prev_popup_depth + 1);
                c->prev_popups[c->prev_popup_depth++] = c->popups[i];
            }
            else
            {
                c->popups[keep++] = c->popups[i];
            }
        }
        c->popup_depth = keep;

        c->prev_panel_depth = 0;
        keep = 0;
        for (i32 i = 0; i < c->panel_depth; ++i)
        {
            if (c->panels[i].win == wi)
            {
                detail::ensure_panel_capacity(c, c->prev_panel_depth + 1);
                c->prev_panels[c->prev_panel_depth++] = c->panels[i];
            }
            else
            {
                c->panels[keep++] = c->panels[i];
            }
        }
        c->panel_depth = keep;
    }
    c->popup_click_outside = false;
    c->popup_click_id = 0;
    if (c->popup_dismiss && c->prev_popup_depth > 0)
    {
        // The window lost focus to another app: the top popup closes as if
        // clicked outside (native menus do the same).
        c->popup_click_outside = true;
        c->popup_click_id = c->prev_popups[c->prev_popup_depth - 1].id;
    }
    c->in_popup = false;
    c->popup_layer_depth = 0;
    c->panel_stack_depth = 0;

    // per-window focus list and blur scratch migrate into the context for the
    // duration of the frame (they are per-window state).
    if (!w.focus_store) w.focus_store = new focus_list();
    c->focus_store = w.focus_store;
    c->focus_store->prev.swap(c->focus_store->current);
    c->focus_store->current.clear();

    // Tab moves keyboard focus through the ring of widgets interacted last
    // frame — text fields and every other focusable widget alike. Nothing
    // focused: the first entry. Shift+Tab goes back. The select-all pending
    // flag applies only to text fields (consumed on their rising edge in
    // edit_buffer); it is cleared at end_frame when nothing claimed it.
    if (c->key_pressed[static_cast<i32>(key::TAB)])
    {
        // The key edge is left set for the frame (apps and tests may still
        // read it); only this block reacts to it, and the edge clears with
        // the next frame's input copy.
        c->focus_request_selects_all = true;
        detail::tab_move(c, c->focus, c->shift_down); // focus 0 -> ring[0]
    }
    if (c->focus_request != 0)
    {
        c->focus = c->focus_request;
        c->focus_request = 0;
    }
    if (!w.blur) w.blur = new blur_store();
    c->blur = w.blur;

    c->want_cursor = cursor::ARROW;

    // Keyed UI state (animation, scroll) is collected after 5 s of disuse.
    sweep_unused(c->st->anim.map, now, 5.0);
    sweep_unused(c->st->rects.map, now, 5.0);
    sweep_unused(c->st->scrolls.map, now, 5.0);

    c->button_scope_depth = 0;

    if (!c->dl) c->dl = new draw_list();
    c->dl->verts.clear();
    c->dl->idx.clear();
    c->dl->clip_known = false;
    c->dl->clip_active = false;
    c->dl->tex_known = false;
    c->dl->cur_tex = nullptr;

    if (c->device)
    {
        c->device->begin_frame();
        if (w.surface) w.surface->make_current(*c->device);
        c->device->clear(c->active_theme.bg);
    }
}

void begin_frame(context *c, f64 now, f64 dt, rect screen)
{
    window *w = detail::ensure_primary(c);
    if (!w) return;
    if (!w->surface) w->surface = c->surface;
    // Single-window path: the primary window lives at the desktop origin.
    w->area = screen;
    w->client = rect::make(0.0f, 0.0f, screen.w * w->input_scale, screen.h * w->input_scale);
    begin_frame(c, *w, now, dt);
}

// Defined later in the implementation section; the deferred overlays below use
// them (popup surfaces must draw after the base UI).
namespace detail
{
inline i32 menu_items(ui &u, uiid id, rect area, const char *const *labels, i32 count, i32 &hot,
                      const theme &t, bool fresh_open);
inline void popup_retract(context *c, uiid id);
} // namespace detail

namespace detail
{
// End of frame: the edges are consumed (held state, modifiers and IME stay).
inline void clear_input_edges(window_input &in)
{
    in.mouse_pressed = in.mouse_released = in.right_pressed = false;
    in.wheel_x = in.wheel_y = 0.0f;
    std::memset(in.key_pressed, 0, sizeof(in.key_pressed));
    std::memset(in.key_released, 0, sizeof(in.key_released));
    in.text_len = 0;
    in.text_input[0] = '\0';
    in.drop_count = 0;
    in.drop_len = 0;
    in.popup_dismiss = false;
    in.a11y_invoke = 0;
}
} // namespace detail

namespace detail
{
// The frame's drag regions, minus the widgets over them, become the window's
// native caption until the next frame (see window_caption).
inline void publish_caption(context *c, window &w)
{
    window_caption &cap = w.caption;
    cap = window_caption{};
    for (const ctx_stores::caption_rect &d : c->st->caption_drags)
    {
        if (cap.drag_count == window_caption::MAX_DRAG) break;
        cap.drag[cap.drag_count++] = d.r;
    }
    if (cap.drag_count == 0) return;
    for (const ctx_stores::caption_rect &h : c->st->caption_hits)
    {
        bool own = false, over = false;
        for (const ctx_stores::caption_rect &d : c->st->caption_drags)
        {
            own = own || d.id == h.id; // the region's own hover interact
            const rect x = rect::intersect(d.r, h.r);
            over = over || (x.w > 0.0f && x.h > 0.0f);
        }
        if (own || !over) continue;
        if (cap.hole_count < window_caption::MAX_HOLES)
        {
            cap.holes[cap.hole_count++] = h.r;
            continue;
        }
        rect &last = cap.holes[window_caption::MAX_HOLES - 1]; // full: grow the last
        const f32 x0 = min2(last.x, h.r.x), y0 = min2(last.y, h.r.y);
        const f32 x1 = max2(last.right(), h.r.right()), y1 = max2(last.bottom(), h.r.bottom());
        last = rect::make(x0, y0, x1 - x0, y1 - y0);
    }
}
} // namespace detail

void end_frame(context *c)
{
    window *w = c->current_window;
    PUFFERUI_CHECK(VIOL_END_WITHOUT_BEGIN, w != nullptr, "end_frame without begin_frame");
    if (!w) return;
    PUFFERUI_CHECK(VIOL_CLIP_UNBALANCED, c->clip_depth == 0, "unbalanced region clip push/pop");
    PUFFERUI_CHECK(VIOL_SCOPE_UNBALANCED, c->disabled_depth == 0, "disabled scope outlived frame");
    PUFFERUI_CHECK(VIOL_SCOPE_UNBALANCED, c->button_scope_depth == 0,
                   "unbalanced button style scope");

    // Text-drag overlay: while a selection is being dragged, the lifted text
    // follows the pointer. Releasing outside any field cancels the drag.
    if (c->text_drag.active)
    {
        if (!c->mouse_down)
        {
            c->text_drag = text_drag_payload{};
        }
        else if (!c->text_drag.text.empty())
        {
            ui u(c);
            const f32 gw = u.text_width(c->text_drag.text) + 12.0f;
            const f32 gh = u.line_height() + 8.0f;
            const rect ghost{c->mouse_x + 12.0f, c->mouse_y + 10.0f, gw, gh};
            u.draw_rounded_rect(rect::make(ghost.x + 2.0f, ghost.y + 2.0f, ghost.w, ghost.h),
                                color{0, 0, 0, 90}, 4.0f);
            u.draw_rounded_rect(ghost, color{48, 56, 68, 240}, 4.0f);
            u.text(ghost, c->text_drag.text, c->active_theme.text, ALIGN_CENTER);
        }
    }

    // Deferred overlays: the open combo dropdown / context menu and the visible
    // tooltip draw here, after every base widget, so nothing painted later in
    // the frame can cover them (immediate mode paints in call order; popup
    // surfaces must go last, like the text-drag ghost above). The pick is
    // resolved here too: mouse edges are still live, and the entry push/retract
    // behaves exactly as an in-frame popup would.
    if (c->defer_menu_id != 0 && c->defer_count > 0)
    {
        ui u(c);
        popup_scope p = u.popup(c->defer_menu_id, c->defer_menu, POPUP_CLOSE_ON_CLICK_OUTSIDE);
        if (p.close_requested)
        {
            detail::popup_retract(c, c->defer_menu_id); // closing: no ghost entry
        }
        else
        {
            i32 hot = c->combo_hot; // the dropdown and context menus share it
            const i32 got = detail::menu_items(
                u, c->defer_menu_id, c->defer_menu, detail::ensure_defers(c)->ptrs.data(),
                c->defer_count, hot, c->active_theme, c->defer_fresh);
            c->combo_hot = hot;
            if (got >= 0)
            {
                c->defer_result_id = c->defer_menu_id; // the next call reports it
                c->defer_result_pick = got;
                detail::popup_retract(c, c->defer_menu_id); // same-frame close
            }
        }
        c->defer_menu_id = 0;
        c->defer_count = 0;
    }
    if (c->defer_tip.w > 0.0f)
    {
        ui u(c);
        u.draw_rounded_rect(c->defer_tip, color{18, 22, 28, 245}, 6.0f);
        u.text(c->defer_tip, c->defer_tip_text, c->active_theme.text, ALIGN_CENTER);
        c->defer_tip = rect{};
    }

    detail::dl_flush(c);
    // Eyedropper samples: the frame is complete in the scene target now.
    if (!c->st->pixel_requests.empty())
    {
        render_surface *surf = w->surface ? w->surface : c->surface;
        texture_handle scene = surf ? surf->scene_target() : nullptr;
        for (const ctx_stores::pixel_request &r : c->st->pixel_requests)
        {
            u8 px[4]{};
            const i32 x = static_cast<i32>(r.at.x * w->scale),
                      y = static_cast<i32>(r.at.y * w->scale);
            if (c->device && scene && c->device->read_pixels(scene, x, y, 1, 1, px))
                c->st->pixel_results[r.id] = color{px[0], px[1], px[2], px[3]};
        }
        c->st->pixel_requests.clear();
    }
    if (c->device)
    {
        c->device->set_clip(nullptr);
        // Apply the cursor only from the window under the pointer: with several
        // windows on one device, every window's frame would otherwise overwrite
        // the cursor of the window actually being hovered (the last one wins).
        const bool pointer_over =
            c->window_count <= 1 || w->client.contains(c->mouse_global_x, c->mouse_global_y);
        if (pointer_over) c->device->set_cursor(c->want_cursor);
        c->device->end_frame();
    }
    if (w->surface) w->surface->present();
    detail::overlays_present(c, *w); // spilled popups, after their window

    if (!c->mouse_down) c->active = 0;

    detail::publish_caption(c, *w);
    if (c->a11y_enabled)
    {
        if (!w->a11y) w->a11y = new a11y_tree();
        a11y_tree &frame = c->st->a11y_frame;
        frame.nodes.resize(frame.count);  // what this frame recorded
        w->a11y->nodes.swap(frame.nodes); // the previous nodes become next frame's slots
        if (w->on_a11y) w->on_a11y(*w);
    }

    // Save this window's persistent state and its input (held keys, modifiers,
    // IME composition), with the per-frame edges cleared.
    detail::clear_input_edges(*c);
    static_cast<interaction_state &>(*w) = *c;
    w->in = *c;

    // Detach the per-window stores again.
    w->focus_store = c->focus_store;
    c->focus_store = nullptr;
    w->blur = c->blur;
    c->blur = nullptr;
    // Tab select-all is a per-frame pending flag: if it was not consumed by a
    // text field this frame (the Tab target was not a field), drop it so a
    // later field focus never selects all by accident.
    c->focus_request_selects_all = false;

    if (c->dl)
    {
        c->dl->clip_known = false;
        c->dl->clip_active = false;
        c->dl->tex_known = false;
        c->dl->cur_tex = nullptr;
    }

    // Safety net: if no dock space consumed the drop this frame, or the release
    // landed outside every window, don't leave a stale drag hanging.
    if (!c->mouse_down && c->dock_panel != 0)
    {
        c->dock_panel = 0;
        c->dock_source = nullptr;
        c->dock_panel_name = nullptr;
        c->dock_dragging = false;
    }

    c->current_region = 0;
    c->current_window = nullptr;
}
bool needs_redraw(const context *c)
{
    if (!c) return true;
    if (c->text_drag.active || c->redraw_requested) return true;
    for (const auto &kv : c->st->anim.map)
        if (kv.second.initialized && !kv.second.settled) return true;
    for (const auto &kv : c->st->rects.map)
        if (!kv.second.settled) return true;
    for (const auto &kv : c->st->edits.map)
        if (kv.second.focused) return true;
    return false;
}
