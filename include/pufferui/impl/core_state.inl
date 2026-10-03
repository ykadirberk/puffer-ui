// pufferui/impl/core_state.inl - part of the PufferUI implementation (see pufferui.h).
// Included by pufferui.h inside `namespace pui` when PUFFERUI_IMPLEMENTATION is
// defined; never include it directly. The slices concatenate to the original
// implementation block in order (tools/amalgamate.ps1 rebuilds the single header).

namespace detail
{
inline context **current_slot()
{
    // One context per thread: the slot is genuinely thread-local, so two
    // threads (or an editor with an embedded preview on a worker thread)
    // never cross-report violations or layout clamps.
    thread_local context *slot = nullptr;
    return &slot;
}

// Non-fatal truncation note (see set_report_layout_overflow). Called only when
// the requested slice does not fit the remaining space. column/row carry no
// context pointer, so this uses the current context of the calling thread
// (a cursor is only ever used inside its frame, where the slot is set — and
// the slot is thread-local: one context per thread).
inline void report_slice_clamp(const char *what, f32 requested, f32 granted)
{
    context *c = *current_slot();
    if (!c || !c->report_layout_overflow || requested <= granted + 0.001f) return;
    c->overflow_count += 1;
    char message[160];
    std::snprintf(message, sizeof(message),
                  "%s slice clamped: requested %.0f px > remaining %.0f px", what,
                  static_cast<double>(requested), static_cast<double>(granted));
    report_violation(VIOL_LAYOUT_CLAMPED, "clamped", message, "", 0, false);
}
} // namespace detail

// Deferred-menu label storage (owned copies; see context::defers).
struct defer_store
{
    std::vector<char> buf;          // packed label strings
    std::vector<const char *> ptrs; // one per label, into buf
};

namespace detail
{
inline defer_store *ensure_defers(context *c); // defined after ctx_stores

// Copies a panel dock name into context-owned storage: the drag ghost draws
// across frames, so the caller string need not outlive the gesture.
inline const char *intern_dock_name(context *c, const char *name)
{
    if (!name)
    {
        c->dock_panel_name_buf[0] = '\0';
        return c->dock_panel_name_buf;
    }
    std::snprintf(c->dock_panel_name_buf, sizeof(c->dock_panel_name_buf), "%s", name);
    return c->dock_panel_name_buf;
}
// Growth-on-demand for the overlay stacks: the fixed caps are initial
// capacities, so deep popup/panel nesting never refuses a layer (and the
// VIOL_POPUP_OVERFLOW guard is no longer reachable).
inline void ensure_popup_capacity(context *c, i32 need)
{
    if (need <= c->popup_capacity) return;
    i32 cap = c->popup_capacity > 0 ? c->popup_capacity : MAX_POPUPS;
    while (cap < need) cap *= 2;
    popup_entry *fresh = new popup_entry[cap];
    for (i32 i = 0; i < c->popup_depth; ++i) fresh[i] = c->popups[i];
    popup_entry *fresh_prev = new popup_entry[cap];
    for (i32 i = 0; i < c->prev_popup_depth; ++i) fresh_prev[i] = c->prev_popups[i];
    delete[] c->popups;
    delete[] c->prev_popups;
    c->popups = fresh;
    c->prev_popups = fresh_prev;
    c->popup_capacity = cap;
}

inline void ensure_panel_capacity(context *c, i32 need)
{
    if (need <= c->panel_capacity) return;
    i32 cap = c->panel_capacity > 0 ? c->panel_capacity : MAX_PANELS;
    while (cap < need) cap *= 2;
    panel_entry *fresh = new panel_entry[cap];
    for (i32 i = 0; i < c->panel_depth; ++i) fresh[i] = c->panels[i];
    panel_entry *fresh_prev = new panel_entry[cap];
    for (i32 i = 0; i < c->prev_panel_depth; ++i) fresh_prev[i] = c->prev_panels[i];
    delete[] c->panels;
    delete[] c->prev_panels;
    c->panels = fresh;
    c->prev_panels = fresh_prev;
    c->panel_capacity = cap;
}

inline void ensure_panel_stack_capacity(context *c, i32 need)
{
    if (need <= c->panel_stack_capacity) return;
    i32 cap = c->panel_stack_capacity > 0 ? c->panel_stack_capacity : MAX_PANELS;
    while (cap < need) cap *= 2;
    uiid *fresh = new uiid[cap];
    for (i32 i = 0; i < c->panel_stack_depth; ++i) fresh[i] = c->panel_stack[i];
    delete[] c->panel_stack;
    c->panel_stack = fresh;
    c->panel_stack_capacity = cap;
}
} // namespace detail

// Records a deferred menu's labels into context-owned storage. All strings
// are appended first, then the pointers are built, so no pointer can dangle
// behind a later reallocation.
inline void defer_set_labels(context *c, const char *const *items, i32 count)
{
    defer_store *ds = detail::ensure_defers(c);
    ds->buf.clear();
    ds->ptrs.clear();
    for (i32 i = 0; i < count; ++i)
    {
        const char *src = items[i] ? items[i] : "";
        usize n = 0;
        while (src[n] != '\0') ++n;
        const usize at = ds->buf.size();
        ds->buf.resize(at + n + 1);
        std::memcpy(ds->buf.data() + at, src, n + 1);
    }
    usize off = 0;
    for (i32 i = 0; i < count; ++i)
    {
        ds->ptrs.push_back(ds->buf.data() + off);
        off += std::strlen(ds->buf.data() + off) + 1;
    }
    c->defer_count = count;
}

void set_window_host(context *c, window_host *host)
{
    if (c) c->host = host;
}

window_host *window_host_of(context *c)
{
    return c ? c->host : nullptr;
}

context *current_context()
{
    return *detail::current_slot();
}
void set_current_context(context *c)
{
    *detail::current_slot() = c;
}

// The debug break, portably. SDL3 builds use SDL's breakpoint (every platform
// SDL3 supports); the backend-agnostic core falls back to compiler intrinsics
// (MSVC: __debugbreak; GCC/clang: __builtin_trap, a SIGTRAP a debugger picks
// up).
#if defined(PUFFERUI_ENABLE_SDL3)
#define PUFFERUI_BREAK() SDL_TriggerBreakpoint()
#elif defined(_MSC_VER)
#define PUFFERUI_BREAK() __debugbreak()
#elif defined(__GNUC__)
#define PUFFERUI_BREAK() __builtin_trap()
#else
#define PUFFERUI_BREAK() ((void)0)
#endif

void report_violation(violation_code code, const char *condition, const char *message,
                      const char *file, int line, bool fatal)
{
    if (context *c = current_context())
    {
        ++c->violations;
        c->vlast_code = code;
        std::snprintf(c->vlast, sizeof(c->vlast), "%s", message ? message : "");
        if (c->vhandler && !fatal)
        {
            // A handler captures contract violations; a fatal one is a broken
            // invariant and stops immediately below without consulting it.
            c->vhandler(c->vhandler_user, condition, message, file, line);
            return;
        }
        if (fatal)
        {
            // PUFFERUI_ASSERT is a broken invariant, not a contract nudge:
            // report loudly and stop, in every build, handled or not.
            std::fprintf(stderr, "[pufferui] fatal assertion failed: %s (%s) at %s:%d\n",
                         message ? message : "", condition ? condition : "", file, line);
            std::fflush(stderr);
            PUFFERUI_BREAK();
            std::abort();
        }
#if !defined(NDEBUG)
        // Debug builds stop the debugger at the violating call: a violation
        // nobody handles is a bug, and the fix is fastest exactly there.
        // Tests install a handler (they capture and assert) so they never
        // break here; retail builds keep the cheap detection but never stop.
        // SDL3 builds use SDL's portable breakpoint (every platform SDL3
        // supports); the backend-agnostic core falls back to compiler
        // intrinsics.
        if (c->break_on_violation) PUFFERUI_BREAK();
#endif
    }
    std::fprintf(stderr, "[pufferui] violation: %s (%s) at %s:%d\n", message ? message : "",
                 condition ? condition : "", file, line);
}

void set_violation_handler(context *c, violation_handler handler, void *user)
{
    c->vhandler = handler;
    c->vhandler_user = user;
}

// ---- text store ----
struct glyph
{
    texture_handle tex = nullptr;
    f32 xoff = 0, yoff = 0, advance = 0; // xoff/yoff: whole pixels from the pen / baseline
    f32 w = 0, h = 0;                    // the bitmap's size in whole pixels
    f32 u0 = 0, v0 = 0, u1 = 0, v1 = 0;
};

struct font_data
{
    std::vector<u8> buffer;
    stbtt_fontinfo info{};
    bool ok = false;
};

template <class V> struct flat_map
{
    struct slot
    {
        u64 hash = 0; // 0 = empty (real hashes are mixed to never be 0)
        uiid key = 0;
        V value{};
    };
    std::vector<slot> slots;
    usize used = 0;

    static u64 mix(u64 h)
    {
        h *= 0x9E3779B97F4A7C15ull;
        h ^= h >> 32;
        h *= 0xff51afd7ed558ccdull;
        h ^= h >> 31;
        return h ? h : 1;
    }

    void rehash(usize new_cap)
    {
        std::vector<slot> old;
        old.swap(slots);
        slots.assign(new_cap, slot{});
        for (const slot &s : old)
        {
            if (!s.hash) continue;
            usize i = s.hash & (new_cap - 1);
            while (slots[i].hash) i = (i + 1) & (new_cap - 1);
            slots[i] = s;
        }
    }

    V *find(uiid key)
    {
        if (used == 0 || slots.empty()) return nullptr;
        const u64 h = mix(key);
        const usize mask = slots.size() - 1;
        usize i = static_cast<usize>(h) & mask;
        while (slots[i].hash)
        {
            if (slots[i].hash == h && slots[i].key == key) return &slots[i].value;
            i = (i + 1) & mask;
        }
        return nullptr;
    }

    V &at(uiid key) // find-or-insert
    {
        if (slots.empty() || used * 4 >= slots.size() * 3) // load factor 0.75
        {
            const usize cap = slots.empty() ? 64 : slots.size() * 2;
            rehash(cap);
        }
        const u64 h = mix(key);
        const usize mask = slots.size() - 1;
        usize i = static_cast<usize>(h) & mask;
        while (slots[i].hash)
        {
            if (slots[i].hash == h && slots[i].key == key) return slots[i].value;
            i = (i + 1) & mask;
        }
        slots[i].hash = h;
        slots[i].key = key;
        used += 1;
        return slots[i].value;
    }

    // Wholesale clear (per frame); capacity is kept.
    void clear()
    {
        for (slot &s : slots) s.hash = 0;
        used = 0;
    }
};

// Direct-mapped cache: a fixed array of {tag, value}; lookup is one probe.
// Collision = overwrite + recompute, so only use it for data whose
// recomputation is cheap and side-effect-free (the purge disappears).
template <class T, usize N> struct direct_cache
{
    static_assert((N & (N - 1)) == 0, "N must be a power of two");
    struct slot
    {
        u64 tag = 0;
        T value{};
    };
    slot slots[N];

    static u64 mix(u64 h)
    {
        h *= 0x9E3779B97F4A7C15ull;
        h ^= h >> 32;
        h *= 0xff51afd7ed558ccdull;
        h ^= h >> 31;
        return h ? h : 1;
    }

    T *find(u64 h)
    {
        const u64 m = mix(h);
        slot &s = slots[static_cast<usize>(m) & (N - 1)];
        return (s.tag == m) ? &s.value : nullptr;
    }
    void put(u64 h, const T &v)
    {
        const u64 m = mix(h);
        slot &s = slots[static_cast<usize>(m) & (N - 1)];
        s.tag = m;
        s.value = v;
    }
};

struct text_store
{
    std::vector<font_data> fonts;

    // (font, size, codepoint, subpixel bin) as a struct key: no aliasing between
    // (size, cp) pairs, unlike the old packed u64 (cp can exceed the packing
    // multiplier). `sub` is the glyph's horizontal offset in quarter pixels
    // (0..3): the same glyph is rasterized up to four times so that every quad
    // can be drawn on a whole pixel yet land within 1/8 px of its true position.
    struct glyph_key
    {
        i32 font;
        i32 size_q; // size quantized to quarter pixels
        u32 cp;
        i32 sub;
        bool operator==(const glyph_key &) const = default;
    };
    struct glyph_key_hash
    {
        usize operator()(const glyph_key &k) const
        {
            u64 h = static_cast<u64>(static_cast<u32>(k.font));
            h = h * 0x9E3779B97F4A7C15ull + static_cast<u64>(k.size_q);
            h = h * 0x9E3779B97F4A7C15ull + static_cast<u64>(k.cp);
            h = h * 0x9E3779B97F4A7C15ull + static_cast<u64>(k.sub);
            h ^= h >> 33;
            h *= 0xff51afd7ed558ccdull;
            h ^= h >> 33;
            return static_cast<usize>(h);
        }
    };
    std::unordered_map<glyph_key, glyph, glyph_key_hash> glyphs;

    // Text layout cache (direct-mapped, one probe; collisions rebuild —
    // side-effect-free): (font, size, string hash) -> width + the per-glyph
    // run (kern-in + glyph copy). text_width reads the width; ui::text emits
    // from the run — one cache, no decode and no glyph-map lookups on hit.
    struct cached_glyph
    {
        f32 kern_in = 0.0f; // kerning between the previous glyph and this one
        u32 cp = 0;
        glyph g{};      // subpixel bin 0 (also the advance)
        glyph sub[3]{}; // bins 1..3, filled on first use
        u8 have = 0;    // bit n set: sub[n - 1] is valid
    };
    struct text_layout
    {
        f32 width = 0.0f;
        std::vector<cached_glyph> gs;
    };
    direct_cache<text_layout, 4096> layout_cache;

    // Glyph atlases. One 1024x1024 page holds a full UI set; pages spill when
    // it fills, so non-Latin vocabularies scale (each glyph carries its own
    // page's texture, and batching already flushes on texture change).
    struct atlas_page
    {
        texture_handle tex = nullptr;
        i32 shelf_x = 0, shelf_y = 0, shelf_h = 0;
    };
    std::vector<atlas_page> atlases;
    i32 atlas_w = 0, atlas_h = 0;
};

struct edit_state
{
    std::string buffer;
    usize caret = 0;
    usize anchor = 0;
    f32 scroll_x = 0.0f; // keeps the caret inside the visible field
    bool focused = false;
    bool dragging = false;     // mouse drag-select
    bool pending_drag = false; // pressed inside the selection: may become a move drag
    bool drag_moved = false;   // moved past the drag threshold this press
    f32 press_x = 0.0f;
    f32 press_y = 0.0f;
    i32 click_streak = 0; // double/triple click detection
    f64 last_click_time = -1.0;
    f32 last_click_x = 0.0f, last_click_y = 0.0f;

    // Undo/redo: snapshots of (text, caret, anchor). Consecutive single-character
    // edits coalesce into one step.
    struct snapshot
    {
        std::string text;
        usize caret = 0;
        usize anchor = 0;
    };
    std::vector<snapshot> undo;
    std::vector<snapshot> redo;
    f64 last_edit_time = -1.0;
};

struct edit_store
{
    std::unordered_map<uiid, edit_state> map;
};

struct focus_list
{
    std::vector<uiid> current;
    std::vector<uiid> prev;
};

struct blur_store
{
    texture_handle half = nullptr, quarter = nullptr, eighth = nullptr, sixteenth = nullptr;
    i32 w = 0, h = 0;
};

struct split_store
{
    std::unordered_map<uiid, f32> offsets;
};

// Animation state, keyed by (region-scoped or global) id. Unused keys are
// collected after 5 s, like other keyed UI state.
struct anim_store
{
    struct entry
    {
        f32 current = 0.0f;
        f32 start = 0.0f;
        f32 target = 0.0f;
        f32 velocity = 0.0f;
        f32 duration = 0.0f;
        easing curve = easing::EASE_OUT;
        f64 t0 = 0.0;
        f64 last_used = 0.0;
        i32 kind = 0; // 0 tween, 1 spring, 2 smooth
        bool initialized = false;
        bool settled = true;
    };
    std::unordered_map<uiid, entry> map;
};

// Rect motion (u.animate_rect): four springs per key (x, y, w, h), kept
// relative to the caller's `origin` so a moving parent never animates.
struct rect_store
{
    struct entry
    {
        f32 x[4]{};    // current x, y, w, h (origin-relative)
        f32 v[4]{};    // their velocities, px/s
        rect target{}; // origin-relative
        vec2 origin{}; // the last frame of reference (motion_info reports in it)
        f64 last_used = 0.0;
        u64 stepped_frame = ~0ull; // the frame it was last stepped (double-step guard)
        bool settled = true;
    };
    std::unordered_map<uiid, entry> map;
};

// Scroll offsets + content extents, keyed by scroll view id.
struct scroll_store
{
    struct entry
    {
        f32 offset = 0.0f;
        f32 content_h = 0.0f;
        f32 viewport_h = 0.0f;
        f32 drag_anchor = 0.0f;
        f32 drag_offset = 0.0f;
        f64 last_used = 0.0;
        bool dragging = false;
    };
    std::unordered_map<uiid, entry> map;
};

// Every keyed store lives in one struct: created with the context, freed with
// one `delete`, reached without casts. A new store is one member here.
struct ctx_stores
{
    edit_store edits;
    anim_store anim;
    rect_store rects; // u.animate_rect
    scroll_store scrolls;
    split_store splits;
    defer_store defers;
    flat_map<u8> frame_ids;    // per-frame region/scope ids (duplicate check)
    flat_map<rect> widget_ids; // debug: interacted widget ids -> first rect
};

namespace detail
{
inline defer_store *ensure_defers(context *c)
{
    return &c->st->defers;
}
} // namespace detail
inline scroll_store *ensure_scrolls(context *c)
{
    return &c->st->scrolls;
}

// Keyed state is collected after `ttl` seconds without use (`last_used`).
template <class M> inline void sweep_unused(M &map, f64 now, f64 ttl)
{
    for (auto it = map.begin(); it != map.end();)
    {
        if (now - it->second.last_used > ttl)
            it = map.erase(it);
        else
            ++it;
    }
}
