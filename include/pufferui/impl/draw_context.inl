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
        if (c->device) c->device->set_clip(want_active ? &want : nullptr);
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

inline void dl_add(context *c, const vertex *verts, i32 vcount, const i32 *idx, i32 icount)
{
    if (!c->dl || vcount <= 0 || icount <= 0) return;
    draw_list &dl = *c->dl;
    i32 base = static_cast<i32>(dl.verts.size());
    dl.verts.insert(dl.verts.end(), verts, verts + vcount);
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
    dl.idx.reserve(dl.idx.size() + static_cast<usize>(icount));
    for (i32 i = 0; i < icount; ++i) dl.idx.push_back(idx[i] + base);
}

} // namespace detail

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
    if (c->window_count > 0) return &c->windows[0];
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
        window &w = c->windows[i];
        delete w.focus_store;
        w.focus_store = nullptr;
        detail::free_blur_store(c, w.blur);
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
        if (!c->windows[i].surface) c->windows[i].surface = surface;
}
void set_theme(context *c, const theme &t)
{
    c->active_theme = t;
}

// ---- windows ----
namespace detail
{
// The per-window root id: a pure function of the slot, so it can be re-derived
// whenever remove_window renumbers the list.
inline uiid window_root(i32 index)
{
    return id_child(0x77696E646F770000ull, static_cast<uiid>(index) + 1);
}

// Grow the heap-owned window list, keeping interior pointers valid.
inline void grow_windows(context *c)
{
    const i32 new_cap = (c->window_capacity > 0) ? c->window_capacity * 2 : MAX_WINDOWS;
    window *fresh = new window[new_cap];
    for (i32 i = 0; i < c->window_count; ++i) fresh[i] = c->windows[i];
    if (c->current_window) c->current_window = fresh + (c->current_window - c->windows);
    if (c->focused_win) c->focused_win = fresh + (c->focused_win - c->windows);
    delete[] c->windows;
    c->windows = fresh;
    c->window_capacity = new_cap;
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
    window &w = c->windows[c->window_count];
    w = window{};
    w.owner = c;
    w.handle = handle;
    w.surface = surface ? surface : c->surface;
    w.index = c->window_count;
    set_window_client(w, client);
    w.root = detail::window_root(w.index);
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
    if (&w < c->windows || &w >= c->windows + c->window_count) return;
    const i32 i = static_cast<i32>(&w - c->windows);
    i32 focus_index = c->focused_win ? static_cast<i32>(c->focused_win - c->windows) : -1;
    delete w.focus_store;
    w.focus_store = nullptr;
    detail::free_blur_store(c, w.blur);
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

    if (focus_index == i)
        focus_index = (i < c->window_count) ? i : c->window_count - 1;
    else if (focus_index > i)
        focus_index -= 1;
    for (i32 j = 0; j < c->window_count; ++j)
    {
        c->windows[j].owner = c;
        c->windows[j].index = j;
        c->windows[j].root = detail::window_root(j); // roots follow the index, so they stay unique
    }
    c->focused_win = (focus_index >= 0 && c->window_count > 0) ? &c->windows[focus_index] : nullptr;
    for (i32 j = 0; j < c->window_count; ++j)
        c->windows[j].focused = (c->focused_win == &c->windows[j]);
}

window *window_at(context *c, void *handle)
{
    for (i32 i = 0; i < c->window_count; ++i)
        if (c->windows[i].handle == handle) return &c->windows[i];
    return nullptr;
}

window *first_window(context *c)
{
    return c->window_count > 0 ? &c->windows[0] : nullptr;
}

void set_window_client(window &w, rect client)
{
    w.client = client;
    w.area = rect::make(0.0f, 0.0f, client.w, client.h);
}

void focus_window(context *c, window &w)
{
    if (&w < c->windows || &w >= c->windows + c->window_count) return;
    c->focused_win = &w;
    for (i32 i = 0; i < c->window_count; ++i) c->windows[i].focused = (&c->windows[i] == &w);
}

window *focused_window(context *c)
{
    return c->focused_win;
}

rect desktop_rect(context *c)
{
    if (c->window_count == 0) return c->screen;
    rect u = c->windows[0].client;
    for (i32 i = 1; i < c->window_count; ++i)
    {
        const rect &r = c->windows[i].client;
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
    return rect::make(c->desktop.x - cl.x, c->desktop.y - cl.y, c->desktop.w, c->desktop.h);
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
    if (!(&w >= c->windows && &w < c->windows + c->window_count))
    {
        PUFFERUI_CHECK(VIOL_WINDOW_NOT_REGISTERED, false, "begin_frame: window is not registered");
        return;
    }

    c->frame += 1;
    c->now = now;
    c->dt = dt;
    c->current_window = &w;
    c->screen = w.area;
    c->desktop = desktop_rect(c);

    // The pointer is one; each window derives its local position from the shared
    // desktop position, so cross-window drags keep working while another window
    // receives the motion events.
    c->mouse_x = c->mouse_global_x - w.client.x;
    c->mouse_y = c->mouse_global_y - w.client.y;
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
    w->client = rect::make(0.0f, 0.0f, screen.w, screen.h);
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
}
} // namespace detail

void end_frame(context *c)
{
    window *w = c->current_window;
    PUFFERUI_CHECK(VIOL_END_WITHOUT_BEGIN, w != nullptr, "end_frame without begin_frame");
    if (!w) return;
    PUFFERUI_CHECK(VIOL_CLIP_UNBALANCED, c->clip_depth == 0, "unbalanced region clip push/pop");
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

    if (!c->mouse_down) c->active = 0;

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
