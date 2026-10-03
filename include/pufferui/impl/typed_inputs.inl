// pufferui/impl/typed_inputs.inl - part of the PufferUI implementation (see pufferui.h).
// Included by pufferui.h inside `namespace pui` when PUFFERUI_IMPLEMENTATION is
// defined; never include it directly. The slices concatenate to the original
// implementation block in order (tools/amalgamate.ps1 rebuilds the single header).
// ---- typed inputs ----
namespace detail
{

inline usize prev_cp(const std::string &s, usize i)
{
    if (i == 0) return 0;
    usize j = i - 1;
    while (j > 0 && (static_cast<unsigned char>(s[j]) & 0xC0) == 0x80) --j;
    return j;
}
inline usize next_cp(const std::string &s, usize i)
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
inline usize index_at_x(ui &u, const std::string &s, f32 local_x)
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

inline bool edit_buffer(ui &u, uiid id, rect r, const interaction &in, edit_state &st)
{
    context *c = u.ctx;

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
    if (in.blocked)
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

    const f32 pad = u.th().padding * 0.5f;
    const f32 local_x = c->mouse_x - (r.x + pad) + st.scroll_x;

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
        const usize idx = index_at_x(u, st.buffer, local_x);
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
            select_all(st);
        else if (st.click_streak == 2)
            select_word_at(st, idx);
        else
        {
            const bool inside_sel = has_selection(st) && idx > sel_lo(st) && idx < sel_hi(st);
            if (inside_sel && !c->shift_down)
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
    if (c->text_drag.active && c->mouse_released && in.hovered)
    {
        const usize idx = index_at_x(u, st.buffer, local_x);
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
        set_focus(false);
        return false;
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
    if (c->ctrl_down && c->key_pressed[static_cast<i32>(key::X)] && has_selection(st))
    {
        if (c->clip)
            c->clip->set(std::string_view(st.buffer).substr(sel_lo(st), sel_hi(st) - sel_lo(st)));
        push_undo(st, c->now, false);
        erase_range(st, sel_lo(st), sel_hi(st));
        changed = true;
    }
    if (c->ctrl_down && c->key_pressed[static_cast<i32>(key::V)] && c->clip)
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
    if (c->ctrl_down && c->key_pressed[static_cast<i32>(key::Z)] && !c->shift_down)
    {
        if (undo_edit(st)) changed = true;
    }
    if (c->ctrl_down && (c->key_pressed[static_cast<i32>(key::Y)] ||
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
            const usize idx = index_at_x(u, st.buffer, local_x);
            if (st.click_streak >= 2)
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
            st.caret = index_at_x(u, st.buffer, local_x);
            st.anchor = st.caret;
        }
        st.pending_drag = false;
        st.dragging = false;
        st.drag_moved = false;
    }

    // typing replaces selection
    if (c->text_len > 0)
    {
        push_undo(st, c->now, !has_selection(st));
        if (has_selection(st)) erase_range(st, sel_lo(st), sel_hi(st));
        st.buffer.insert(st.caret, c->text_input, static_cast<usize>(c->text_len));
        st.caret += static_cast<usize>(c->text_len);
        st.anchor = st.caret;
        changed = true;
    }

    // backspace / delete (Ctrl deletes a whole word)
    if (c->key_pressed[static_cast<i32>(key::BACKSPACE)])
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
    if (c->key_pressed[static_cast<i32>(key::DEL)])
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
    if (c->key_pressed[static_cast<i32>(key::HOME)])
    {
        st.caret = 0;
        if (!c->shift_down) st.anchor = 0;
        break_undo_coalescing(st);
    }
    if (c->key_pressed[static_cast<i32>(key::END)])
    {
        st.caret = st.buffer.size();
        if (!c->shift_down) st.anchor = st.caret;
        break_undo_coalescing(st);
    }

    // Keep the caret inside the visible field (single-line horizontal scroll).
    {
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
inline void draw_edit_body(ui &u, rect r, const corner_radii &radii, color border,
                           f32 border_thickness, const edit_state &st, const std::string &shown,
                           bool focused, i32 drop_index = -1, bool dim = false, usize dim_lo = 0,
                           usize dim_hi = 0)
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
    const bool edited = detail::edit_buffer(*this, id, r, in, st);
    bool changed = false;
    // Write back when focused, when focus was just lost, or when the framework
    // edited the buffer (e.g. a text drop into an unfocused field) — otherwise
    // the next reload would discard the edit.
    if ((st.focused || was_focused || edited) && value != st.buffer)
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

    const bool edited = detail::edit_buffer(*this, id, r, in, st);
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
