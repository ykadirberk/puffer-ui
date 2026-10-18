// pufferui/impl/typed_inputs.inl - part of the PufferUI implementation (see pufferui.h).
// Included by pufferui.h inside `namespace pui` when PUFFERUI_IMPLEMENTATION is
// defined; never include it directly. The slices concatenate to the original
// implementation block in order (tools/amalgamate.ps1 rebuilds the single header).
// ---- typed inputs ----
namespace detail
{

inline usize prev_cp(std::string_view s, usize i)
{
    if (i == 0) return 0;
    usize j = i - 1;
    while (j > 0 && (static_cast<unsigned char>(s[j]) & 0xC0) == 0x80) --j;
    return j;
}
inline usize next_cp(std::string_view s, usize i)
{
    if (i >= s.size()) return s.size();
    usize j = i + 1;
    while (j < s.size() && (static_cast<unsigned char>(s[j]) & 0xC0) == 0x80) ++j;
    return j;
}

inline edit_state &edit_for(context *c, uiid id)
{
    return c->st->edits.map[id];
}

inline bool has_selection(const edit_state &st)
{
    return st.anchor != st.caret;
}
inline usize sel_lo(const edit_state &st)
{
    return st.anchor < st.caret ? st.anchor : st.caret;
}
inline usize sel_hi(const edit_state &st)
{
    return st.anchor < st.caret ? st.caret : st.anchor;
}

// Caret index nearest to `local_x` (text space). Mirrors `text()`: advances plus
// the kerning between consecutive codepoints, so a click lands exactly where the
// drawn caret (which measures prefixes with `text_width`) sits.
inline usize index_at_x(ui &u, std::string_view s, f32 local_x)
{
    context *c = u.ctx;
    const f32 size = c->active_theme.text_size;
    const font_handle fh = c->active_theme.font;
    f32 x = 0.0f;
    usize i = 0, best = 0;
    f32 best_d = 1.0e9f;
    u32 prev = 0;
    while (true)
    {
        const f32 d = std::fabs(x - local_x);
        if (d < best_d)
        {
            best_d = d;
            best = i;
        }
        if (i >= s.size()) break;
        const usize j = next_cp(s, i);
        usize k = i;
        const u32 cp = utf8_decode(s, k);
        x += kern_advance(c, fh, size, prev, cp);
        x += u.text_width(std::string_view(s.data() + i, j - i));
        prev = cp;
        i = j;
    }
    return best;
}

// Word policy: ASCII alphanumerics, '_' and every non-ASCII codepoint count as
// word characters, so Turkish/Greek words behave as single words. Punctuation and
// whitespace form their own runs.
inline bool word_cp(u32 cp)
{
    return cp != UTF8_END && (cp == '_' || cp >= 0x80 || (cp >= '0' && cp <= '9') ||
                              (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z'));
}

inline u32 cp_at(const std::string &s, usize i)
{
    if (i >= s.size()) return UTF8_END;
    usize j = i;
    return utf8_decode(s, j);
}

// Next/previous word boundary: the positions Ctrl+Left/Right jump to.
inline usize next_word_boundary(const std::string &s, usize i)
{
    const usize n = s.size();
    if (i >= n) return n;
    const bool word = word_cp(cp_at(s, i));
    usize j = i;
    while (j < n)
    {
        const usize k = next_cp(s, j);
        if (k >= n || word_cp(cp_at(s, k)) != word) return k;
        j = k;
    }
    return n;
}

inline usize prev_word_boundary(const std::string &s, usize i)
{
    if (i == 0) return 0;
    if (i > s.size()) i = s.size();
    const usize p = prev_cp(s, i);
    const bool word = word_cp(cp_at(s, p));
    usize j = p;
    while (j > 0)
    {
        const usize k = prev_cp(s, j);
        if (word_cp(cp_at(s, k)) != word) return j;
        j = k;
    }
    return 0;
}

inline bool space_cp(u32 cp)
{
    return cp == ' ' || cp == '\t';
}

// Ctrl+Left/Right stops: whitespace is skipped so you land at the start/end of
// the next chunk ("foo bar" -> 3, 7 from 0), like browsers.
inline usize next_word_stop(const std::string &s, usize i)
{
    const usize n = s.size();
    usize j = i;
    while (j < n && space_cp(cp_at(s, j))) j = next_cp(s, j);
    return next_word_boundary(s, j);
}

inline usize prev_word_stop(const std::string &s, usize i)
{
    usize j = i;
    while (j > 0)
    {
        const usize p = prev_cp(s, j);
        if (!space_cp(cp_at(s, p))) break;
        j = p;
    }
    return prev_word_boundary(s, j);
}

// The maximal same-class run around `i` (double-click selection). Clicking just
// after a word selects that word, like browsers do.
inline void word_run_at(const std::string &s, usize i, usize &lo, usize &hi)
{
    const usize n = s.size();
    if (i > n) i = n;
    lo = hi = i;
    if (n == 0) return;
    const usize start = (i > 0) ? prev_cp(s, i) : i;
    const bool word = word_cp(cp_at(s, start));
    lo = hi = start;
    while (lo > 0)
    {
        const usize p = prev_cp(s, lo);
        if (word_cp(cp_at(s, p)) != word) break;
        lo = p;
    }
    while (hi < n)
    {
        if (word_cp(cp_at(s, hi)) != word) break;
        hi = next_cp(s, hi);
    }
}

inline void select_all(edit_state &st)
{
    st.anchor = 0;
    st.caret = st.buffer.size();
}

inline void select_word_at(edit_state &st, usize i)
{
    word_run_at(st.buffer, i, st.anchor, st.caret);
}

inline void erase_range(edit_state &st, usize lo, usize hi)
{
    if (hi <= lo) return;
    st.buffer.erase(lo, hi - lo);
    st.caret = lo;
    st.anchor = lo;
}

// Note: `edit_state::undo`/`redo` are only meaningful while the field is focused
// — `edit_buffer` clears them on every focus loss, because an unfocused field
// reloads its buffer from the model each frame (stale snapshots would otherwise
// be restored, and written back, by a later Ctrl+Z).

// Undo/redo: consecutive single-character edits coalesce into one step.
inline void push_undo(edit_state &st, f64 now, bool coalesce)
{
    const bool merge = coalesce && st.last_edit_time >= 0.0 && (now - st.last_edit_time) < 0.4;
    if (!merge)
    {
        st.undo.push_back(edit_state::snapshot{st.buffer, st.caret, st.anchor});
        if (st.undo.size() > 64) st.undo.erase(st.undo.begin());
    }
    st.redo.clear();
    st.last_edit_time = now;
}

inline void break_undo_coalescing(edit_state &st)
{
    st.last_edit_time = -1.0;
}

inline bool undo_edit(edit_state &st)
{
    if (st.undo.empty()) return false;
    st.redo.push_back(edit_state::snapshot{st.buffer, st.caret, st.anchor});
    const edit_state::snapshot s = st.undo.back();
    st.undo.pop_back();
    st.buffer = s.text;
    st.caret = s.caret;
    st.anchor = s.anchor;
    break_undo_coalescing(st);
    return true;
}

inline bool redo_edit(edit_state &st)
{
    if (st.redo.empty()) return false;
    st.undo.push_back(edit_state::snapshot{st.buffer, st.caret, st.anchor});
    if (st.undo.size() > 64) st.undo.erase(st.undo.begin());
    const edit_state::snapshot s = st.redo.back();
    st.redo.pop_back();
    st.buffer = s.text;
    st.caret = s.caret;
    st.anchor = s.anchor;
    break_undo_coalescing(st);
    return true;
}

// Scoped clip push/pop for field bodies (long values must not paint outside).
inline bool clip_push(context *c, rect r)
{
    if (c->clip_depth >= MAX_CLIP_DEPTH) return false;
    const rect parent =
        (c->clip_depth > 0) ? c->clip_stack[c->clip_depth - 1] : rect{0, 0, 1.0e9f, 1.0e9f};
    c->clip_stack[c->clip_depth++] = rect::intersect(r, parent);
    return true;
}

inline void clip_pop(context *c)
{
    if (c->clip_depth > 0) --c->clip_depth;
}

inline void register_focusable(context *c, uiid id)
{
    if (!c->focus_store) c->focus_store = new focus_list();
    focus_list *fs = c->focus_store;
    // edit_buffer runs right after the field's interact (which already joined
    // the ring): adjacent-dedup so a widget never occupies two slots.
    if (fs->current.empty() || fs->current.back() != id) fs->current.push_back(id);
}

inline void tab_move(context *c, uiid id, bool back)
{
    if (!c->focus_store) return;
    focus_list *fs = c->focus_store;
    const i32 n = static_cast<i32>(fs->prev.size());
    if (n <= 0) return;
    i32 idx = -1;
    for (i32 i = 0; i < n; ++i)
        if (fs->prev[static_cast<usize>(i)] == id)
        {
            idx = i;
            break;
        }
    const i32 next = (idx < 0) ? 0 : (back ? (idx - 1 + n) % n : (idx + 1) % n);
    c->focus_request = fs->prev[static_cast<usize>(next)];
    c->focus_request_selects_all = true;
}

inline bool caret_visible(context *c)
{
    return std::fmod(c->now, 1.0) < 0.5;
}

// What an editor's shape decides - a single-line field or a text_area: where a
// pointer lands, what a line is, how the caret moves up and down. One editing
// path (keys, selection, undo, clipboard, IME, drag) serves both.
struct edit_geometry
{
    virtual ~edit_geometry() = default;
    virtual bool multiline() const { return false; }
    virtual usize index_at(f32 mx, f32 my) const = 0; // pointer (frame coords) -> index
    virtual usize line_home(usize i) const
    {
        (void)i;
        return 0;
    }
    virtual usize line_end(usize i) const = 0;
    // `lines` rows up (< 0) or down; `sticky_x` carries the column across moves.
    virtual usize vertical(usize i, i32 lines, f32 &sticky_x) const
    {
        (void)lines;
        (void)sticky_x;
        return i;
    }
    virtual i32 page_lines() const { return 0; }
};

// The single-line field: one line, scrolled horizontally by `scroll_x`.
struct line_geometry : edit_geometry
{
    ui &u;
    rect r;
    const edit_state &st;
    line_geometry(ui &u_, rect r_, const edit_state &st_) : u(u_), r(r_), st(st_) {}
    usize index_at(f32 mx, f32) const override
    {
        const f32 pad = u.th().padding * 0.5f;
        return index_at_x(u, st.buffer, mx - (r.x + pad) + st.scroll_x);
    }
    usize line_end(usize) const override { return st.buffer.size(); }
};

inline bool edit_buffer(ui &u, uiid id, rect r, const interaction &in, edit_state &st,
                        const edit_geometry &geo, bool read_only = false)
{
    context *c = u.ctx;
    if (read_only)
    {
        st.pending_drag = false;
        st.drag_moved = false;
        if (c->text_drag.source_id == id)
        {
            c->text_drag.active = false;
            c->text_drag.text.clear();
        }
    }

    bool changed = false;

    // Keep the context's focused-widget id in sync so `interaction.focused`
    // reflects text-field focus for built-in and custom widgets alike.
    // Focus changes reset the undo history: while unfocused the buffer reloads
    // from the model, so snapshots taken before a blur could otherwise restore
    // stale text (and write it back) on a later Ctrl+Z.
    auto set_focus = [&](bool focused)
    {
        st.focused = focused;
        if (focused)
        {
            c->focus = id;
        }
        else
        {
            if (c->focus == id) c->focus = 0;
            st.undo.clear();
            st.redo.clear();
            break_undo_coalescing(st);
        }
    };

    // A blocked field (under a popup/panel layer) neither joins the tab order nor
    // consumes input, so an open popup traps focus.
    if (in.blocked || in.disabled)
    {
        if (st.focused) set_focus(false);
        return false;
    }

    register_focusable(c, id);

    // Keyboard focus is one state: c->focus (shared with every other widget).
    // A field derives st.focused from it with edge detection — the rising edge
    // consumes the Tab select-all pending flag (Tab focus selects the value)
    // and resets the horizontal scroll; the falling edge clears the undo
    // history (see set_focus above).
    const bool now_focused = (c->focus == id);
    if (now_focused && !st.focused)
    {
        st.focused = true;
        st.scroll_x = 0.0f;
        if (c->focus_request_selects_all)
        {
            select_all(st);
            c->focus_request_selects_all = false;
        }
    }
    else if (!now_focused && st.focused)
    {
        set_focus(false);
    }

    const auto index_here = [&]() { return geo.index_at(c->mouse_x, c->mouse_y); };
    const usize caret_before = st.caret;

    // Press: focus (when needed) and place the caret where you clicked, with
    // double/triple-click selection. Handling this on the press edge is what
    // stops the caret from jumping to the end on the first click.
    if (c->mouse_pressed && in.hovered)
    {
        if (!st.focused)
        {
            set_focus(true);
            st.undo.clear();
            st.redo.clear();
        }
        const usize idx = index_here();
        // Double/triple clicks must land on the same spot, not just soon after.
        const bool near = st.last_click_time >= 0.0 && (c->now - st.last_click_time) < 0.4 &&
                          std::fabs(c->mouse_x - st.last_click_x) <= 5.0f &&
                          std::fabs(c->mouse_y - st.last_click_y) <= 5.0f;
        st.click_streak = near ? st.click_streak + 1 : 1;
        st.last_click_time = c->now;
        st.last_click_x = c->mouse_x;
        st.last_click_y = c->mouse_y;
        st.press_x = c->mouse_x;
        st.press_y = c->mouse_y;
        st.dragging = true;
        st.drag_moved = false;
        st.pending_drag = false;
        if (st.click_streak >= 3)
        {
            if (geo.multiline()) // a line in a text area, everything in a field
            {
                st.anchor = geo.line_home(idx);
                st.caret = geo.line_end(idx);
            }
            else
                select_all(st);
        }
        else if (st.click_streak == 2)
            select_word_at(st, idx);
        else
        {
            const bool inside_sel = has_selection(st) && idx > sel_lo(st) && idx < sel_hi(st);
            if (inside_sel && !c->shift_down && !read_only)
                st.pending_drag = true; // may become a move drag; else collapses on release
            else
            {
                st.caret = idx;
                st.anchor = c->shift_down ? st.anchor : st.caret;
            }
        }
        break_undo_coalescing(st);
    }

    // Drop an active text drag into this field (move; hold Ctrl to copy).
    if (!read_only && c->text_drag.active && c->mouse_released && in.hovered)
    {
        const usize idx = index_here();
        const usize lo = c->text_drag.lo, hi = c->text_drag.hi;
        const bool from_self = c->text_drag.source_id == id;
        const bool copy = c->ctrl_down;

        // The source keeps accepting input while the drag is in flight, so the
        // recorded range may no longer hold the lifted text. A move verifies it
        // first and cancels instead of erasing the wrong characters; a copy only
        // inserts, so it needs no check.
        edit_state &src = edit_for(c, c->text_drag.source_id);
        const bool range_ok = hi >= lo && hi <= src.buffer.size() &&
                              src.buffer.compare(lo, hi - lo, c->text_drag.text) == 0;
        if (copy || range_ok)
        {
            push_undo(st, c->now, false);
            if (from_self && !copy)
            {
                usize at = idx;
                if (at > lo && at <= hi)
                    at = lo; // dropped onto itself: keep the text where it is
                else if (at > hi)
                    at -= (hi - lo);
                st.buffer.erase(lo, hi - lo);
                st.buffer.insert(at, c->text_drag.text);
                st.caret = at + c->text_drag.text.size();
            }
            else
            {
                st.buffer.insert(idx, c->text_drag.text);
                st.caret = idx + c->text_drag.text.size();
                if (!from_self && !copy)
                {
                    // Erase the lifted range from its source field; that field
                    // stays focused until it has flushed, then this one takes over.
                    src.buffer.erase(lo, hi - lo);
                    src.caret = src.anchor = lo;
                    break_undo_coalescing(src);
                    c->focus_request = id;
                    c->focus_request_selects_all = false;
                }
            }
            st.anchor = st.caret;
            changed = true;
        }
        c->text_drag.active = false;
        c->text_drag.text.clear();
    }

    if (!st.focused) return changed;

    if (c->mouse_pressed && !in.hovered)
    {
        set_focus(false);
        return false;
    }
    if (c->key_pressed[static_cast<i32>(key::ENTER)])
    {
        if (!geo.multiline())
        {
            set_focus(false);
            return false;
        }
        c->key_pressed[static_cast<i32>(key::ENTER)] = false; // consumed: a new line
        if (!read_only)
        {
            push_undo(st, c->now, false);
            if (has_selection(st)) erase_range(st, sel_lo(st), sel_hi(st));
            st.buffer.insert(st.caret, 1, '\n');
            st.caret += 1;
            st.anchor = st.caret;
            changed = true;
        }
    }
    if (c->key_pressed[static_cast<i32>(key::ESCAPE)])
    {
        if (c->text_drag.active)
        {
            c->text_drag.active = false; // cancel the drag, keep editing
            c->text_drag.text.clear();
            return changed;
        }
        set_focus(false);
        return false;
    }

    // clipboard + undo/redo
    if (c->ctrl_down && c->key_pressed[static_cast<i32>(key::A)])
    {
        select_all(st);
    }
    if (c->ctrl_down && c->key_pressed[static_cast<i32>(key::C)])
    {
        if (has_selection(st) && c->clip)
            c->clip->set(std::string_view(st.buffer).substr(sel_lo(st), sel_hi(st) - sel_lo(st)));
    }
    if (!read_only && c->ctrl_down && c->key_pressed[static_cast<i32>(key::X)] && has_selection(st))
    {
        if (c->clip)
            c->clip->set(std::string_view(st.buffer).substr(sel_lo(st), sel_hi(st) - sel_lo(st)));
        push_undo(st, c->now, false);
        erase_range(st, sel_lo(st), sel_hi(st));
        changed = true;
    }
    if (!read_only && c->ctrl_down && c->key_pressed[static_cast<i32>(key::V)] && c->clip)
    {
        std::string paste;
        if (c->clip->get(paste))
        {
            push_undo(st, c->now, false);
            if (has_selection(st)) erase_range(st, sel_lo(st), sel_hi(st));
            st.buffer.insert(st.caret, paste);
            st.caret += paste.size();
            st.anchor = st.caret;
            changed = true;
        }
    }
    if (!read_only && c->ctrl_down && c->key_pressed[static_cast<i32>(key::Z)] && !c->shift_down)
    {
        if (undo_edit(st)) changed = true;
    }
    if (!read_only && c->ctrl_down &&
        (c->key_pressed[static_cast<i32>(key::Y)] ||
         (c->shift_down && c->key_pressed[static_cast<i32>(key::Z)])))
    {
        if (redo_edit(st)) changed = true;
    }

    // Mouse: extend the selection, or lift it into a browser-like text drag.
    if (st.dragging && c->mouse_down)
    {
        const f32 ddx = c->mouse_x - st.press_x, ddy = c->mouse_y - st.press_y;
        if (st.pending_drag && (ddx * ddx + ddy * ddy) > 16.0f && has_selection(st))
        {
            c->text_drag.active = true;
            c->text_drag.source_id = id;
            c->text_drag.lo = sel_lo(st);
            c->text_drag.hi = sel_hi(st);
            c->text_drag.text =
                st.buffer.substr(c->text_drag.lo, c->text_drag.hi - c->text_drag.lo);
            st.pending_drag = false;
            st.drag_moved = true;
        }
        if (!st.pending_drag && !st.drag_moved)
        {
            const usize idx = index_here();
            if (st.click_streak >= 3)
            {
                // Triple click: a field keeps "all"; a text area extends by lines.
                if (geo.multiline())
                    st.caret = (idx >= st.anchor) ? geo.line_end(idx) : geo.line_home(idx);
            }
            else if (st.click_streak == 2)
            {
                usize lo = 0, hi = 0;
                word_run_at(st.buffer, idx, lo, hi);
                st.caret = (idx >= st.anchor) ? hi : lo;
            }
            else
            {
                st.caret = idx;
            }
        }
    }
    if (!c->mouse_down)
    {
        if (st.pending_drag)
        {
            // Pressed inside the selection without moving: collapse the caret there.
            st.caret = index_here();
            st.anchor = st.caret;
        }
        st.pending_drag = false;
        st.dragging = false;
        st.drag_moved = false;
    }

    // typing replaces selection
    if (!read_only && c->text_len > 0)
    {
        push_undo(st, c->now, !has_selection(st));
        if (has_selection(st)) erase_range(st, sel_lo(st), sel_hi(st));
        st.buffer.insert(st.caret, c->text_input, static_cast<usize>(c->text_len));
        st.caret += static_cast<usize>(c->text_len);
        st.anchor = st.caret;
        changed = true;
    }

    // backspace / delete (Ctrl deletes a whole word)
    if (!read_only && c->key_pressed[static_cast<i32>(key::BACKSPACE)])
    {
        if (has_selection(st))
        {
            push_undo(st, c->now, false);
            erase_range(st, sel_lo(st), sel_hi(st));
            changed = true;
        }
        else if (st.caret > 0)
        {
            const usize lo =
                c->ctrl_down ? prev_word_stop(st.buffer, st.caret) : prev_cp(st.buffer, st.caret);
            push_undo(st, c->now, !c->ctrl_down);
            erase_range(st, lo, st.caret);
            changed = true;
        }
    }
    if (!read_only && c->key_pressed[static_cast<i32>(key::DEL)])
    {
        if (has_selection(st))
        {
            push_undo(st, c->now, false);
            erase_range(st, sel_lo(st), sel_hi(st));
            changed = true;
        }
        else if (st.caret < st.buffer.size())
        {
            const usize hi =
                c->ctrl_down ? next_word_stop(st.buffer, st.caret) : next_cp(st.buffer, st.caret);
            push_undo(st, c->now, !c->ctrl_down);
            st.buffer.erase(st.caret, hi - st.caret);
            changed = true;
        }
    }

    // navigation (Shift keeps the anchor -> selection; Ctrl moves by word)
    const bool word = c->ctrl_down;
    if (c->key_pressed[static_cast<i32>(key::LEFT)])
    {
        const usize ni =
            (has_selection(st) && !c->shift_down)
                ? sel_lo(st)
                : (word ? prev_word_stop(st.buffer, st.caret) : prev_cp(st.buffer, st.caret));
        st.caret = ni;
        if (!c->shift_down) st.anchor = ni;
        break_undo_coalescing(st);
    }
    if (c->key_pressed[static_cast<i32>(key::RIGHT)])
    {
        const usize ni =
            (has_selection(st) && !c->shift_down)
                ? sel_hi(st)
                : (word ? next_word_stop(st.buffer, st.caret) : next_cp(st.buffer, st.caret));
        st.caret = ni;
        if (!c->shift_down) st.anchor = ni;
        break_undo_coalescing(st);
    }
    // Home/End: the line in a text area (Ctrl: the document), the field otherwise.
    const bool per_line = geo.multiline() && !c->ctrl_down;
    if (c->key_pressed[static_cast<i32>(key::HOME)])
    {
        st.caret = per_line ? geo.line_home(st.caret) : 0;
        if (!c->shift_down) st.anchor = st.caret;
        break_undo_coalescing(st);
    }
    if (c->key_pressed[static_cast<i32>(key::END)])
    {
        st.caret = per_line ? geo.line_end(st.caret) : st.buffer.size();
        if (!c->shift_down) st.anchor = st.caret;
        break_undo_coalescing(st);
    }
    bool moved_vertically = false;
    if (geo.multiline())
    {
        const i32 page = static_cast<i32>(max2(1.0f, static_cast<f32>(geo.page_lines())));
        i32 rows = 0;
        if (c->key_pressed[static_cast<i32>(key::UP)]) rows -= 1;
        if (c->key_pressed[static_cast<i32>(key::DOWN)]) rows += 1;
        if (c->key_pressed[static_cast<i32>(key::PAGE_UP)]) rows -= page;
        if (c->key_pressed[static_cast<i32>(key::PAGE_DOWN)]) rows += page;
        if (rows != 0)
        {
            st.caret = geo.vertical(st.caret, rows, st.sticky_x);
            if (!c->shift_down) st.anchor = st.caret;
            break_undo_coalescing(st);
            moved_vertically = true;
        }
    }
    // Any other caret change forgets the column Up/Down were keeping.
    if (!moved_vertically && st.caret != caret_before) st.sticky_x = -1.0f;

    // Keep the caret inside the visible field (single-line horizontal scroll;
    // a text area reveals the caret itself).
    if (!geo.multiline())
    {
        const f32 pad = u.th().padding * 0.5f;
        const f32 visible = max2(1.0f, r.w - pad * 2.0f);
        const f32 caret_x = u.text_width(std::string_view(st.buffer.data(), st.caret));
        if (caret_x - st.scroll_x > visible - 1.0f) st.scroll_x = caret_x - visible + 1.0f;
        if (caret_x - st.scroll_x < 0.0f) st.scroll_x = caret_x;
        const f32 total = u.text_width(st.buffer);
        st.scroll_x = clampf(st.scroll_x, 0.0f, max2(0.0f, total - visible + 1.0f));
    }

    return changed;
}

// Shared field rendering: background, selection, text, IME preedit and caret —
// scrolled horizontally and clipped to the field so long values never paint
// outside it. `drop_index >= 0` shows the drop position of an active text drag;
// `dim` fades the range that was lifted from this field.
// The field's background and outline (shared by text_field and text_area).
inline void draw_edit_frame(ui &u, rect r, const corner_radii &radii, color border,
                            f32 border_thickness)
{
    const theme &t = u.th();
    if (border_thickness > 0.0f && border.a > 0 && t.widget_bg.a >= 250)
    {
        // opaque background: the classic two-rect outline (one crisp AA edge)
        u.draw_rounded_rect(r, border, radii);
        u.draw_rounded_rect(r.pad(border_thickness), t.widget_bg,
                            detail::inset_radii(radii, border_thickness));
    }
    else
    {
        // translucent (glass) or borderless: the fill first, then the outline as
        // a ring - filling with the border color underneath would show through
        if (t.widget_bg.a > 0) u.draw_rounded_rect(r, t.widget_bg, radii);
        detail::rounded_ring(u, r, border, radii, border_thickness);
    }
}

inline void draw_edit_body(ui &u, rect r, const corner_radii &radii, color border,
                           f32 border_thickness, const edit_state &st, const std::string &shown,
                           bool focused, i32 drop_index = -1, bool dim = false, usize dim_lo = 0,
                           usize dim_hi = 0)
{
    const theme &t = u.th();
    draw_edit_frame(u, r, radii, border, border_thickness);

    const f32 pad = t.padding * 0.5f;
    const rect inner = r.pad(pad, 0.0f);
    const f32 scroll = st.scroll_x;
    const f32 line_h = u.line_height();
    const f32 top = inner.y + max2(0.0f, (inner.h - line_h) * 0.5f);

    const bool clipped = clip_push(u.ctx, inner);
    auto x_at = [&](usize idx)
    { return inner.x - scroll + u.text_width(std::string_view(shown.data(), idx)); };

    const usize lo = sel_lo(st), hi = sel_hi(st);
    if (focused && hi > lo)
    {
        color sc = t.selection;
        if (dim) sc.a = static_cast<u8>(sc.a / 3);
        const f32 x0 = x_at(lo), x1 = x_at(hi);
        u.draw_rect(rect::make(x0, inner.y + 2.0f, max2(0.0f, x1 - x0), max2(0.0f, r.h - 4.0f)),
                    sc);
    }

    if (dim && dim_hi > dim_lo && dim_hi <= shown.size())
    {
        color faded = t.text;
        faded.a = 90;
        u.text(rect::make(inner.x - scroll, top, 1.0e6f, line_h),
               std::string_view(shown.data(), dim_lo), t.text, ALIGN_LEFT);
        u.text(rect::make(x_at(dim_lo), top, 1.0e6f, line_h),
               std::string_view(shown.data() + dim_lo, dim_hi - dim_lo), faded, ALIGN_LEFT);
        u.text(rect::make(x_at(dim_hi), top, 1.0e6f, line_h),
               std::string_view(shown.data() + dim_hi), t.text, ALIGN_LEFT);
    }
    else
    {
        u.text(rect::make(inner.x - scroll, top, 1.0e6f, line_h), shown, t.text, ALIGN_LEFT);
    }

    if (focused && u.ctx->ime_preedit_len > 0)
    {
        const f32 px = x_at(st.caret);
        const f32 pw = u.text_width(
            std::string_view(u.ctx->ime_preedit, static_cast<usize>(u.ctx->ime_preedit_len)));
        u.text(rect::make(px, top, pw + 4.0f, line_h), u.ctx->ime_preedit, t.text, ALIGN_LEFT);
        u.draw_rect(rect::make(px, inner.y + r.h - 3.0f, pw, 1.0f), t.accent);
    }

    if (focused && drop_index < 0 && caret_visible(u.ctx))
        u.draw_rect(rect::make(x_at(st.caret), r.y + 4.0f, 1.0f, max2(0.0f, r.h - 8.0f)), t.caret);
    if (drop_index >= 0)
        u.draw_rect(rect::make(x_at(static_cast<usize>(drop_index)), r.y + 3.0f, 2.0f,
                               max2(0.0f, r.h - 6.0f)),
                    t.accent);

    if (clipped) clip_pop(u.ctx);
}

inline bool parse_float(const std::string &s, f32 &out)
{
    std::string t;
    t.reserve(s.size());
    int seps = 0;
    for (char ch : s)
    {
        if (ch == ',' || ch == '.')
        {
            ++seps;
            t.push_back('.');
        }
        else
            t.push_back(ch);
    }
    if (seps > 1 || t.empty()) return false;

    char *end = nullptr;
    const f32 v = std::strtof(t.c_str(), &end);
    if (end == t.c_str()) return false;
    while (end && (*end == ' ' || *end == '\t')) ++end;
    if (end && *end != '\0') return false;
    out = v;
    return true;
}

} // namespace detail

bool ui::text_field(rect r, std::string &value, uiid id)
{
    return text_field(r, value, id, field_opts{});
}

bool ui::text_field(rect r, std::string &value, uiid id, const field_opts &opts)
{
    auto disabled = this->disabled(!opts.enabled);
    const theme &t = ctx->active_theme;
    const corner_radii radii = opts.radii.set ? opts.radii.value : corner_radii::all(t.radius);
    interaction in = interact(id, r);
    edit_state &st = detail::edit_for(ctx, id);

    const bool was_focused = st.focused;
    if (!st.focused)
    {
        st.buffer = value;
        st.caret = st.buffer.size();
        st.anchor = st.caret;
        st.scroll_x = 0.0f;
    }
    if (opts.read_only || in.disabled)
    {
        st.buffer = value;
        if (st.caret > st.buffer.size()) st.caret = st.buffer.size();
        if (st.anchor > st.buffer.size()) st.anchor = st.buffer.size();
        st.undo.clear();
        st.redo.clear();
    }
    const bool edited = detail::edit_buffer(*this, id, r, in, st,
                                            detail::line_geometry(*this, r, st), opts.read_only);
    bool changed = false;
    // Write back when focused, when focus was just lost, or when the framework
    // edited the buffer (e.g. a text drop into an unfocused field) — otherwise
    // the next reload would discard the edit.
    if (!opts.read_only && !in.disabled && (st.focused || was_focused || edited) &&
        value != st.buffer)
    {
        value = st.buffer;
        changed = true;
    }
    else if (edited)
        changed = true;

    if (in.hovered) ctx->want_cursor = t.text_cursor;

    const bool is_source = ctx->text_drag.active && ctx->text_drag.source_id == id;
    i32 drop_index = -1;
    if (ctx->text_drag.active && in.hovered && !is_source)
        drop_index = static_cast<i32>(detail::index_at_x(
            *this, st.buffer, ctx->mouse_x - (r.x + t.padding * 0.5f) + st.scroll_x));

    const f32 bt = st.focused ? t.focus_border_thickness : t.border_thickness;
    const color bc = st.focused ? t.focus_border : t.border;
    detail::draw_edit_body(*this, r, radii, bc, bt, st, st.focused ? st.buffer : value, st.focused,
                           drop_index, is_source, ctx->text_drag.lo, ctx->text_drag.hi);
    a11y(id, r,
         {.role = a11y_role::TEXT_FIELD,
          .value = st.focused ? st.buffer : value,
          .state = opts.read_only ? A11Y_READ_ONLY : 0u});
    return changed;
}

namespace detail
{
// Widths of [start, j) for growing j, summed glyph by glyph the way text_width
// sums them (kerning into a glyph, then its advance; no kerning into the first),
// so every result is bit-identical to `text_width(s.substr(start, j - start))`
// without laying out each prefix (and without filling the layout cache with
// them). An installed shaper may shape a prefix differently from the whole
// run, so with one the meter measures each prefix instead.
struct prefix_meter
{
    using glyph_metric = text_lines::metric;
    ui &u;
    std::string_view s;
    std::vector<glyph_metric> &m; // the paragraph's codepoints, then a sentinel at `e`
    bool exact = true;            // false: measure prefixes with text_width
    usize start = 0, k0 = 0, k = 0;
    f32 w = 0.0f;

    prefix_meter(ui &u_, std::string_view s_, usize b, usize e, std::vector<glyph_metric> &m_)
        : u(u_), s(s_), m(m_)
    {
        context *c = u.ctx;
        const font_handle fh = c->active_theme.font;
        exact = c->ts && !c->ts->shaper && fh >= 0 && fh < static_cast<i32>(c->ts->fonts.size()) &&
                c->ts->fonts[static_cast<usize>(fh)].ok;
        m.clear();
        if (!exact) return;
        const f32 size = c->active_theme.text_size;
        text_lines::metric_memo &memo = c->st->wrap_memo;
        const auto kern = [&](u32 a, u32 b2)
        {
            if (a == 0) return 0.0f; // what kern_advance returns at a line start
            const u32 slot = (a * 0x9E3779B1u ^ b2 * 0x85EBCA77u) % text_lines::metric_memo::PAIRS;
            if (memo.a[slot] != a || memo.b[slot] != b2)
            {
                memo.a[slot] = a;
                memo.b[slot] = b2;
                memo.kern[slot] = kern_advance(c, fh, size, a, b2);
            }
            return memo.kern[slot];
        };
        const auto advance = [&](u32 cp)
        {
            const u32 slot = cp % text_lines::metric_memo::CPS;
            if (memo.cp[slot] != cp)
            {
                memo.cp[slot] = cp;
                memo.advance[slot] = get_glyph(c, fh, size, cp).advance;
            }
            return memo.advance[slot];
        };
        const std::string_view para = s.substr(0, e); // decoding never reads past the paragraph
        usize i = b;
        u32 prev = 0;
        while (i < e)
        {
            const usize at = i;
            const u32 cp = utf8_decode(para, i);
            if (cp == UTF8_END) break;
            m.push_back({at, kern(prev, cp), advance(cp)});
            prev = cp;
        }
        m.push_back({e, 0.0f, 0.0f});
    }
    void restart(usize from)
    {
        start = from;
        if (!exact) return;
        while (k0 + 1 < m.size() && m[k0].at < from) ++k0; // line starts only move forward
        k = k0;
        w = 0.0f;
    }
    // The width of [start, j); j only grows between restarts.
    f32 to(usize j)
    {
        if (!exact) return j > start ? u.text_width(s.substr(start, j - start)) : 0.0f;
        while (k + 1 < m.size() && m[k].at < j)
        {
            w += k == k0 ? 0.0f : m[k].kern_in; // the first glyph has no kerning into it
            w += m[k].advance;
            ++k;
        }
        return w;
    }
};

// Greedy word wrap of the paragraph [b, e) into lines no wider than `width`;
// trailing spaces stay on their line, a word wider than the line breaks
// between codepoints.
inline void wrap_paragraph(ui &u, std::string_view s, usize b, usize e, f32 width,
                           std::vector<text_lines::line> &out)
{
    if (b == e)
    {
        out.push_back({b, e});
        return;
    }
    prefix_meter meter(u, s, b, e, u.ctx->st->wrap_metrics);
    usize start = b;
    while (start < e)
    {
        meter.restart(start);
        usize fit = start;
        usize i = start;
        while (i < e)
        {
            usize j = i;
            while (j < e && s[j] == ' ') ++j;
            while (j < e && s[j] != ' ') j = next_cp(s, j);
            const f32 w = meter.to(j);
            if (w <= width)
            {
                fit = j;
                i = j;
                continue;
            }
            if (fit == start) // one word wider than the line: cut it by codepoints
            {
                meter.restart(start);
                usize k = next_cp(s, start);
                fit = k;
                (void)meter.to(k);
                while (k < j)
                {
                    const usize nk = next_cp(s, k);
                    if (meter.to(nk) > width) break;
                    fit = nk;
                    k = nk;
                }
            }
            break;
        }
        while (fit < e && s[fit] == ' ') ++fit;
        out.push_back({start, fit});
        start = fit;
    }
}

// What a text area's lines are built from; any change rebuilds them.
inline text_lines::source text_source(ui &u, const edit_state &st, f32 width)
{
    const theme &t = u.th();
    return {st.gen, st.buffer.size(), width, t.text_size, u.ctx->render_scale, t.font};
}

// The visual lines of the area's buffer, rebuilt only when their source changed
// since the cached layout (no per-frame pass over the text).
inline void build_text_lines(ui &u, const edit_state &st, bool wrap, f32 width, text_lines &out)
{
    const std::string_view s = st.buffer;
    const text_lines::source key = text_source(u, st, wrap ? width : -1.0f);
    if (out.built && key == out.key) return;
    out.key = key;
    out.built = true;
    out.lines.clear();
    if (wrap) u.ctx->st->wrap_memo.clear(); // this rebuild's font and size
    usize b = 0;
    i32 para = 1;
    for (;; ++para)
    {
        const usize nl = s.find('\n', b);
        const usize e = nl == std::string_view::npos ? s.size() : nl;
        const usize first = out.lines.size();
        if (!wrap || width <= 1.0f)
            out.lines.push_back({b, e});
        else
            wrap_paragraph(u, s, b, e, width, out.lines);
        for (usize i = first; i < out.lines.size(); ++i) out.lines[i].para = para;
        if (nl == std::string_view::npos) break;
        b = nl + 1;
    }
    out.paragraphs = para;
    out.para_key = {key.gen, key.size};
    out.para_counted = true;
}

// The buffer's paragraph count (the gutter is sized before the lines are laid
// out); counted again only when the buffer changed.
inline i32 paragraph_count(const edit_state &st, text_lines &tl)
{
    const text_lines::source key{st.gen, st.buffer.size()};
    if (!tl.para_counted || !(key == tl.para_key))
    {
        i32 n = 1;
        for (char ch : st.buffer) n += ch == '\n' ? 1 : 0;
        tl.paragraphs = n;
        tl.para_key = key;
        tl.para_counted = true;
    }
    return tl.paragraphs;
}

// A text area's geometry: visual lines stacked from (ox, oy), `line_h` apart.
struct area_geometry : edit_geometry
{
    ui &u;
    const std::string &s;
    const text_lines &tl;
    f32 ox = 0.0f, oy = 0.0f, line_h = 1.0f;
    i32 page = 1;
    area_geometry(ui &u_, const std::string &s_, const text_lines &tl_) : u(u_), s(s_), tl(tl_) {}
    i32 count() const { return static_cast<i32>(tl.lines.size()); }
    // The line holding index i: the last one starting at or before it (an index
    // on a wrap boundary belongs to the next line, where its caret shows).
    i32 line_of(usize i) const
    {
        i32 lo = 0, hi = count() - 1;
        while (lo < hi)
        {
            const i32 mid = (lo + hi + 1) / 2;
            if (tl.lines[static_cast<usize>(mid)].begin <= i)
                lo = mid;
            else
                hi = mid - 1;
        }
        return lo;
    }
    std::string_view text_of(i32 li) const
    {
        const text_lines::line &l = tl.lines[static_cast<usize>(li)];
        return std::string_view(s).substr(l.begin, l.end - l.begin);
    }
    f32 x_of(usize i) const
    {
        const usize b = tl.lines[static_cast<usize>(line_of(i))].begin;
        return u.text_width(std::string_view(s).substr(b, i - b));
    }
    bool multiline() const override { return true; }
    usize index_at(f32 mx, f32 my) const override
    {
        if (count() == 0) return 0;
        const i32 li = static_cast<i32>(
            clampf(std::floor((my - oy) / line_h), 0.0f, static_cast<f32>(count() - 1)));
        return tl.lines[static_cast<usize>(li)].begin + index_at_x(u, text_of(li), mx - ox);
    }
    usize line_home(usize i) const override
    {
        return count() ? tl.lines[static_cast<usize>(line_of(i))].begin : 0;
    }
    usize line_end(usize i) const override
    {
        if (!count()) return 0;
        const i32 li = line_of(i);
        const text_lines::line &l = tl.lines[static_cast<usize>(li)];
        // A wrapped line ends where the next one starts: stop one codepoint
        // short so the caret stays on this line.
        if (li + 1 < count() && tl.lines[static_cast<usize>(li + 1)].begin == l.end &&
            l.end > l.begin)
            return prev_cp(s, l.end);
        return l.end;
    }
    usize vertical(usize i, i32 rows, f32 &sticky_x) const override
    {
        if (!count()) return i;
        const i32 li = line_of(i);
        if (sticky_x < 0.0f) sticky_x = x_of(i);
        const i32 to = li + rows;
        if (to < 0 || to >= count())
        {
            sticky_x = -1.0f; // past the first/last line: the document edge, column forgotten
            return to < 0 ? 0 : s.size();
        }
        return tl.lines[static_cast<usize>(to)].begin + index_at_x(u, text_of(to), sticky_x);
    }
    i32 page_lines() const override { return page; }
};
} // namespace detail

bool ui::text_area(rect r, std::string &value, uiid id, const text_area_opts &opts)
{
    auto disabled = this->disabled(!opts.enabled);
    context *c = ctx;
    const theme &t = c->active_theme;
    const corner_radii radii = opts.radii.set ? opts.radii.value : corner_radii::all(t.radius);
    interaction in = interact(id, r);
    edit_state &st = detail::edit_for(c, id);

    const bool was_focused = st.focused;
    if (!st.focused || opts.read_only || in.disabled)
    {
        if (st.buffer != value) // unfocused (or read-only): the model is the truth
        {
            st.buffer = value;
            st.gen += 1;
        }
        st.caret = std::min(st.caret, st.buffer.size());
        st.anchor = std::min(st.anchor, st.buffer.size());
        if (opts.read_only || in.disabled)
        {
            st.undo.clear();
            st.redo.clear();
        }
    }

    const f32 bt = st.focused ? t.focus_border_thickness : t.border_thickness;
    detail::draw_edit_frame(*this, r, radii, st.focused ? t.focus_border : t.border, bt);
    const f32 pad = t.padding * 0.5f;
    rect inner = r.pad(pad);
    const f32 line_h = line_height();

    // The line-number gutter sits outside the scrolled text.
    rect gutter{};
    if (opts.line_numbers)
    {
        const i32 paragraphs = detail::paragraph_count(st, st.layout);
        i32 digits = 1;
        for (i32 n = paragraphs; n >= 10; n /= 10) ++digits;
        gutter = inner.cut_left(text_width("0") * max2(2.0f, static_cast<f32>(digits)) + pad);
        (void)inner.cut_left(pad * 0.5f);
    }

    struct number_mark
    {
        f32 y;
        i32 n;
    };
    std::vector<number_mark> numbers;
    bool changed = false;
    {
        scroll_view sv(*this, id_child(id, "scroll"_id), inner, scroll_options{.padding = 0.0f});
        rect view = sv.content();
        detail::build_text_lines(*this, st, opts.wrap, view.w - 2.0f, st.layout);
        detail::area_geometry geo(*this, st.buffer, st.layout);
        geo.ox = view.x - st.scroll_x;
        geo.oy = view.y;
        geo.line_h = line_h;
        geo.page = static_cast<i32>(max2(1.0f, std::floor(inner.h / line_h) - 1.0f));

        // A press on the scrollbar scrolls; it neither moves the caret nor blurs.
        const bool on_bar =
            sv.overflows() && sv.bar_track_.pad(-3.0f).contains(c->mouse_x, c->mouse_y);
        const bool pressed = c->mouse_pressed;
        if (on_bar) c->mouse_pressed = false;
        const usize caret_before = st.caret;
        const usize size_before = st.buffer.size();
        const bool edited = detail::edit_buffer(*this, id, r, in, st, geo, opts.read_only);
        c->mouse_pressed = pressed;

        if (!opts.read_only && !in.disabled && (st.focused || was_focused || edited) &&
            value != st.buffer)
        {
            value = st.buffer;
            changed = true;
        }
        else if (edited)
            changed = true;

        if (edited) st.gen += 1;
        detail::build_text_lines(*this, st, opts.wrap, view.w - 2.0f, st.layout); // after edits
        sv.set_content_height(static_cast<f32>(st.layout.lines.size()) * line_h);

        // Keep the caret in view when it moved or the text changed.
        if (st.focused && (st.caret != caret_before || st.buffer.size() != size_before || edited))
        {
            const i32 li = geo.line_of(st.caret);
            sv.ensure_visible(
                rect::make(view.x, view.y + static_cast<f32>(li) * line_h, 1.0f, line_h), 0.0f);
            view = sv.content();
            if (!opts.wrap)
            {
                const f32 cx = geo.x_of(st.caret);
                const f32 visible = max2(1.0f, view.w - 2.0f);
                if (cx - st.scroll_x > visible) st.scroll_x = cx - visible;
                if (cx - st.scroll_x < 0.0f) st.scroll_x = cx;
            }
        }
        if (opts.wrap) st.scroll_x = 0.0f;
        geo.ox = view.x - st.scroll_x;
        geo.oy = view.y;

        // Only the visible lines are drawn.
        const i32 n = geo.count();
        const i32 first = static_cast<i32>(max2(0.0f, std::floor((inner.y - view.y) / line_h)));
        const i32 last = static_cast<i32>(
            min2(static_cast<f32>(n - 1), std::ceil((inner.bottom() - view.y) / line_h)));
        const usize lo = detail::sel_lo(st), hi = detail::sel_hi(st);
        const f32 space_w = text_width(" ");
        for (i32 li = first; li <= last; ++li)
        {
            const text_lines::line &l = st.layout.lines[static_cast<usize>(li)];
            const f32 y = view.y + static_cast<f32>(li) * line_h;
            if (opts.line_numbers && (li == 0 || (l.begin > 0 && st.buffer[l.begin - 1] == '\n')))
                numbers.push_back({y, l.para});
            if (st.focused && hi > lo && hi > l.begin && lo <= l.end)
            {
                const usize a = lo > l.begin ? lo : l.begin;
                const usize b = hi < l.end ? hi : l.end;
                const std::string_view row(st.buffer);
                const f32 x0 = geo.ox + text_width(row.substr(l.begin, a - l.begin));
                f32 x1 = geo.ox + text_width(row.substr(l.begin, b - l.begin));
                if (hi > l.end && l.end < st.buffer.size() && st.buffer[l.end] == '\n')
                    x1 += space_w; // the selected newline
                draw_rect(rect::make(x0, y, max2(0.0f, x1 - x0), line_h), t.selection);
            }
            text(rect::make(geo.ox, y, 1.0e6f, line_h), geo.text_of(li), t.text, ALIGN_LEFT);
        }

        if (st.focused)
        {
            const i32 cl = geo.line_of(st.caret);
            const f32 cx = geo.ox + geo.x_of(st.caret);
            const f32 cy = view.y + static_cast<f32>(cl) * line_h;
            if (c->ime_preedit_len > 0)
            {
                const std::string_view pre(c->ime_preedit, static_cast<usize>(c->ime_preedit_len));
                const f32 pw = text_width(pre);
                text(rect::make(cx, cy, pw + 4.0f, line_h), pre, t.text, ALIGN_LEFT);
                draw_rect(rect::make(cx, cy + line_h - 2.0f, pw, 1.0f), t.accent);
            }
            else if (detail::caret_visible(c) && !c->text_drag.active)
                draw_rect(rect::make(cx, cy + 1.0f, 1.0f, line_h - 2.0f), t.caret);
        }
        const bool is_source = c->text_drag.active && c->text_drag.source_id == id;
        if (c->text_drag.active && in.hovered && !is_source && !opts.read_only)
        {
            const usize di = geo.index_at(c->mouse_x, c->mouse_y);
            const i32 dl = geo.line_of(di);
            draw_rect(rect::make(geo.ox + geo.x_of(di), view.y + static_cast<f32>(dl) * line_h,
                                 2.0f, line_h),
                      t.accent);
        }
        if (in.hovered && !on_bar) c->want_cursor = t.text_cursor;
    }

    if (opts.line_numbers && gutter.w > 0.0f)
    {
        const bool clipped = detail::clip_push(c, gutter);
        char num[16];
        for (const number_mark &m : numbers)
        {
            std::snprintf(num, sizeof(num), "%d", m.n);
            text(rect::make(gutter.x, m.y, gutter.w - pad * 0.5f, line_h), num, t.text_dim,
                 ALIGN_RIGHT);
        }
        if (clipped) detail::clip_pop(c);
    }
    a11y(id, r,
         {.role = a11y_role::TEXT_AREA,
          .value = st.buffer,
          .state = opts.read_only ? A11Y_READ_ONLY : 0u});
    return changed;
}

editor_info ui::editor_info(uiid id) const
{
    pui::editor_info out;
    auto &map = ctx->st->edits.map;
    const auto it = map.find(id);
    if (it == map.end()) return out;
    const edit_state &st = it->second;
    out.focused = st.focused && ctx->focus == id;
    out.caret = st.caret;
    out.anchor = st.anchor;
    out.visual_lines = static_cast<i32>(st.layout.lines.size());
    usize line_start = 0;
    for (usize i = 0; i < st.caret && i < st.buffer.size(); ++i)
        if (st.buffer[i] == '\n')
        {
            ++out.line;
            line_start = i + 1;
        }
    for (usize i = line_start; i < st.caret && i < st.buffer.size();
         i = detail::next_cp(st.buffer, i))
        ++out.column;
    return out;
}

f32 ui::scroll_offset(uiid id) const
{
    auto &map = ctx->st->scrolls.map;
    const auto it = map.find(id);
    return it == map.end() ? 0.0f : it->second.offset;
}

namespace detail
{
template <class T> bool scalar_field(ui &u, rect r, T &value, uiid id, const field_opts &opts)
{
    auto disabled = u.disabled(!opts.enabled);
    const interaction in = u.interact(id, r);
    edit_state &st = edit_for(u.ctx, id);
    if (!st.focused || opts.read_only || in.disabled)
    {
        char buf[128];
        const auto formatted = std::to_chars(buf, buf + sizeof(buf), value);
        st.buffer.assign(buf, formatted.ptr);
        if (!st.focused) st.caret = st.anchor = st.buffer.size();
        if (st.caret > st.buffer.size()) st.caret = st.buffer.size();
        if (st.anchor > st.buffer.size()) st.anchor = st.buffer.size();
        if (opts.read_only || in.disabled)
        {
            st.undo.clear();
            st.redo.clear();
        }
    }
    const bool was_focused = st.focused;
    const bool edited = edit_buffer(u, id, r, in, st, line_geometry(u, r, st), opts.read_only);
    bool changed = false, valid = true;
    if (!opts.read_only && !in.disabled && (st.focused || was_focused || edited))
    {
        T parsed{};
        const auto result =
            std::from_chars(st.buffer.data(), st.buffer.data() + st.buffer.size(), parsed);
        valid = result.ec == std::errc{} && result.ptr == st.buffer.data() + st.buffer.size();
        if constexpr (std::is_floating_point_v<T>) valid = valid && std::isfinite(parsed);
        if (valid && parsed != value)
        {
            value = parsed;
            changed = true;
        }
    }
    const theme &t = u.th();
    if (in.hovered) u.set_cursor(t.text_cursor);
    draw_edit_body(u, r, opts.radii.set ? opts.radii.value : corner_radii::all(t.radius),
                   !valid       ? t.tokens.danger
                   : st.focused ? t.focus_border
                                : t.border,
                   st.focused ? t.focus_border_thickness : t.border_thickness, st, st.buffer,
                   st.focused, -1, false, 0, 0);
    return changed;
}
} // namespace detail

bool ui::int_field(rect r, i64 &value, uiid id, const field_opts &opts)
{
    return detail::scalar_field(*this, r, value, id, opts);
}
bool ui::double_field(rect r, f64 &value, uiid id, const field_opts &opts)
{
    return detail::scalar_field(*this, r, value, id, opts);
}
bool ui::vector_field(rect r, std::span<f32> values, uiid id, const field_opts &opts)
{
    if (values.empty()) return false;
    row fields(r, spacing());
    const f32 width = max2(0.0f, (r.w - spacing() * static_cast<f32>(values.size() - 1)) /
                                     static_cast<f32>(values.size()));
    bool changed = false;
    for (usize i = 0; i < values.size(); ++i)
        changed =
            detail::scalar_field(*this, fields.next(width), values[i], id_child(id, i + 1), opts) ||
            changed;
    return changed;
}

bool ui::number_field(rect r, f32 &value, uiid id, const char *fmt)
{
    const theme &t = ctx->active_theme;
    interaction in = interact(id, r);
    edit_state &st = detail::edit_for(ctx, id);

    if (!st.focused)
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), fmt ? fmt : "%.2f", static_cast<double>(value));
        st.buffer = buf;
        if (t.decimal_separator != '.')
        {
            for (char &ch : st.buffer)
                if (ch == '.') ch = t.decimal_separator;
        }
        st.caret = st.buffer.size();
        st.anchor = st.caret;
    }

    const bool edited =
        detail::edit_buffer(*this, id, r, in, st, detail::line_geometry(*this, r, st));
    bool changed = false;
    bool valid = true;
    // Parse while focused, and also after a framework edit (a text drop) so an
    // unfocused target does not lose the inserted text on its next reload.
    if (st.focused || edited)
    {
        f32 parsed = value;
        valid = detail::parse_float(st.buffer, parsed);
        if (valid && parsed != value)
        {
            value = parsed;
            changed = true;
        }
    }

    const bool strong = st.focused || !valid;
    const f32 bt = strong ? max2(t.border_thickness, t.focus_border_thickness) : t.border_thickness;
    const color bc = !valid ? color{220, 70, 70, 255} : (st.focused ? t.focus_border : t.border);
    if (in.hovered) ctx->want_cursor = t.text_cursor;

    const bool is_source = ctx->text_drag.active && ctx->text_drag.source_id == id;
    i32 drop_index = -1;
    if (ctx->text_drag.active && in.hovered && !is_source)
        drop_index = static_cast<i32>(detail::index_at_x(
            *this, st.buffer, ctx->mouse_x - (r.x + t.padding * 0.5f) + st.scroll_x));

    detail::draw_edit_body(*this, r, corner_radii::all(t.radius), bc, bt, st, st.buffer, st.focused,
                           drop_index, is_source, ctx->text_drag.lo, ctx->text_drag.hi);
    return changed;
}
