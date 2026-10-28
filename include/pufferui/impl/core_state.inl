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
// capacities, so deep popup/panel nesting never refuses a layer (and
// VIOL_POPUP_OVERFLOW is never raised).
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
    i32 page = -1; // the atlas page holding the bitmap (-1: none, e.g. a space)
    u32 gen = 0;   // that page's generation when packed (stale once it is recycled)
};

#if defined(PUFFERUI_ENABLE_HARFBUZZ)
struct harfbuzz_font_data
{
    retained_pool_resource outline_pool{8192};
    struct outline
    {
        hb_glyph_extents_t extents{};
        std::pmr::vector<u8> data;
        u32 location = 0;
        u32 chunk = 0;
        bool uploaded = false;
        explicit outline(std::pmr::memory_resource *source = std::pmr::get_default_resource())
            : data(source)
        {
        }
    };
    hb_font_t *shaping = nullptr;
    hb_gpu_draw_t *encoder = nullptr;
    std::pmr::unordered_map<u32, outline> outlines{&outline_pool};
    ~harfbuzz_font_data()
    {
        hb_font_destroy(shaping);
        hb_gpu_draw_destroy(encoder);
    }
};
#endif
struct font_data
{
    std::vector<u8> buffer;
    stbtt_fontinfo info{};
    bool ok = false;
    font_data() = default;
    font_data(const font_data &) = delete;
    font_data &operator=(const font_data &) = delete;
    font_data(font_data &&) noexcept = default;
    font_data &operator=(font_data &&) noexcept = default;
#if defined(PUFFERUI_ENABLE_HARFBUZZ)
    using outline = harfbuzz_font_data::outline;
    harfbuzz_font_data *harfbuzz = nullptr; // owned by text_store; stable across font-vector growth
#endif
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
        if (used == 0) return;
        for (slot &s : slots) s.hash = 0;
        used = 0;
    }
};

// Bounded direct-mapped cache. Collisions replace the old entry; values can
// transfer ownership into the cache without a deep copy.
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

    slot &slot_for(u64 h) { return slots[static_cast<usize>(mix(h)) & (N - 1)]; }
    T *find(u64 h)
    {
        slot &s = slot_for(h);
        return s.tag == mix(h) ? &s.value : nullptr;
    }
    template <class V> void put(u64 h, V &&v)
    {
        const u64 m = mix(h);
        slot &s = slots[static_cast<usize>(m) & (N - 1)];
        s.tag = m;
        s.value = std::forward<V>(v);
    }
};

// Pure kerning results for one layout build (font, raw size and fallbacks fixed).
// Separate from layout caching: hits must not alter glyph/atlas or eviction order.
struct kerning_memo
{
    static constexpr u32 PAIRS = 2048;
    u32 a[PAIRS], b[PAIRS];
    f32 kern[PAIRS];
    void clear()
    {
        for (u32 &x : a) x = UTF8_END;
    }
};

// Renderer cache vectors transfer their allocator together with their storage.
// Empty/default vectors keep the established standard-allocation path; vector
// text opts into a context-owned pool. Propagating moves avoid a hidden deep
// copy when a freshly shaped run enters the direct cache or its victim slot.
template <class T> struct renderer_pool_allocator
{
    using value_type = T;
    using propagate_on_container_move_assignment = std::true_type;
    std::pmr::memory_resource *source = nullptr;
    renderer_pool_allocator() = default;
    explicit renderer_pool_allocator(std::pmr::memory_resource *value) : source(value) {}
    template <class U>
    renderer_pool_allocator(const renderer_pool_allocator<U> &other) : source(other.source)
    {
    }
    T *allocate(usize count)
    {
        if (!source) return std::allocator<T>{}.allocate(count);
        if (count > std::numeric_limits<usize>::max() / sizeof(T))
            throw std::bad_array_new_length();
        return static_cast<T *>(source->allocate(count * sizeof(T), alignof(T)));
    }
    void deallocate(T *pointer, usize count)
    {
        if (source)
            source->deallocate(pointer, count * sizeof(T), alignof(T));
        else
            std::allocator<T>{}.deallocate(pointer, count);
    }
    template <class U> bool operator==(const renderer_pool_allocator<U> &other) const noexcept
    {
        return source == other.source;
    }
};

struct text_store
{
#if defined(PUFFERUI_ENABLE_HARFBUZZ)
    // Declared first so every cached run dies before its backing pool. Large
    // unbounded paragraphs pass through upstream; labels reuse size classes.
    retained_pool_resource run_pool{65536};
    hb_buffer_t *shaping_buffer = nullptr;
    bool vector_enabled = false;
    ~text_store()
    {
        hb_buffer_destroy(shaping_buffer);
        for (auto &font : fonts)
        {
            delete font.harfbuzz;
        }
    }
#endif
    std::vector<font_data> fonts;
    std::vector<font_handle> fallbacks;
    text_shaper *shaper = nullptr; // set_text_shaper; nullptr = built-in

    // (font, size, codepoint, subpixel bin) as a struct key: fields never alias
    // (a codepoint can exceed any packing multiplier). `sub` is the glyph's horizontal offset in
    // quarter pixels (0..3): the same glyph is rasterized up to four times so that every quad can
    // be drawn on a whole pixel yet land within 1/8 px of its true position.
    struct glyph_key
    {
        i32 font;
        i32 size_q; // size quantized to quarter pixels
        u32 cp;
        i32 sub;
        i32 density_q;
        i32 gamma_q; // theme::text_gamma, hundredths
#if defined(PUFFERUI_ENABLE_HARFBUZZ)
        bool glyph_index = false;
#endif
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
            h = h * 0x9E3779B97F4A7C15ull + static_cast<u64>(k.density_q);
            h = h * 0x9E3779B97F4A7C15ull + static_cast<u64>(k.gamma_q);
#if defined(PUFFERUI_ENABLE_HARFBUZZ)
            if (k.glyph_index) h ^= 0x4842474c595048ull;
#endif
            h ^= h >> 33;
            h *= 0xff51afd7ed558ccdull;
            h ^= h >> 33;
            return static_cast<usize>(h);
        }
    };
    std::unordered_map<glyph_key, glyph, glyph_key_hash> glyphs;

    // Preserve the original 4096 primary slots and their exact hit/eviction
    // history. A small victim cache avoids rebuilding safe, colliding runs.
    // (font, size, string hash) -> width + the per-glyph
    // run (kern-in + glyph copy). text_width reads the width; ui::text emits
    // from the run — one cache, no decode and no glyph-map lookups on hit.
    struct cached_glyph
    {
#if defined(PUFFERUI_ENABLE_HARFBUZZ)
        u32 glyph_index = 0, cluster = 0;
        vec2 offset{};
        bool vector = false;
#endif
        f32 kern_in = 0.0f; // kerning between the previous glyph and this one
        u32 cp = 0;
        glyph g{};      // subpixel bin 0 (also the advance)
        glyph sub[3]{}; // bins 1..3, filled on first use
        u8 have = 0;    // bit n set: sub[n - 1] is valid
    };
    struct text_layout
    {
        f32 width = 0.0f;
        // For the draw loop's early exit: the smallest left bearing of a glyph
        // with a bitmap (never above 0), and whether the pen only moves right
        // (no advance + kerning step below zero) - then once a glyph's box
        // starts past the paint area, every later one does too.
        f32 min_xoff = 0.0f;
        bool forward = true;
        // text_store::recycled when every glyph copy here (and every sub-bin
        // copy marked in `have`) was last known to be live: while no atlas page
        // has been recycled since, the draw loop needs no per-glyph checks.
        i32 epoch = -1;
        i32 build_epoch = -1; // immutable: width was summed before any later recycling
        std::vector<cached_glyph, renderer_pool_allocator<cached_glyph>> gs;
        text_layout() = default;
        explicit text_layout(std::pmr::memory_resource *source)
            : gs(renderer_pool_allocator<cached_glyph>(source))
        {
        }
        f32 built_size = 0.0f;
        bool builtin = false;
        // Last ellipsis cut, in UTF-8 bytes. Exact size matters: layout keys
        // quantize sizes, but the legacy ellipsis kerning uses the raw size.
        f32 ellipsis_width = 0.0f, ellipsis_size = 0.0f, ellipsis_tail_width = 0.0f;
        usize ellipsis_cut = 0;
        i32 ellipsis_epoch = -1;
        bool ellipsis_valid = false;
    };
    direct_cache<text_layout, 4096> layout_cache;
    direct_cache<text_layout, 64> layout_victims;
    void clear_layouts()
    {
        for (auto &s : layout_cache.slots) s.tag = 0;
        for (auto &s : layout_victims.slots) s.tag = 0;
    }

    f32 coverage_gamma = 0.0f;
    bool coverage_valid = false;
    u8 coverage_curve[256]{};

    // Glyph atlases. One 1024x1024 page holds a full UI set; pages spill when
    // it fills, so non-Latin vocabularies scale (each glyph carries its own
    // page's texture, and batching already flushes on texture change).
    // With a page budget (set_text_atlas_budget) the least recently drawn page
    // is recycled instead of growing: its generation bumps, so cached glyph
    // copies that still point at it are noticed and re-fetched.
    struct atlas_page
    {
        texture_handle tex = nullptr;
        i32 shelf_x = 0, shelf_y = 0, shelf_h = 0;
        u64 last_used = 0; // the frame that last drew (or packed) a glyph of it
        u32 gen = 0;
    };
    std::vector<atlas_page> atlases;
    i32 atlas_w = 0, atlas_h = 0;
    i32 cur_page = 0; // the page new glyphs are packed into
    i32 budget = 16;  // pages kept before recycling (0: unlimited)
    i32 recycled = 0; // pages recycled so far (stats)
};

// The visual lines of a multi-line buffer (ui::text_area): byte ranges without
// the newline; a wrapped paragraph contributes several. Rebuilt only when what
// they were built from (`key`: the buffer's edit generation and size, the wrap
// width, the font) changes - an idle area costs nothing per frame.
struct text_lines
{
    struct line
    {
        usize begin = 0, end = 0;
        i32 para = 1; // the 1-based paragraph it belongs to (the line-number gutter)
    };
    struct source
    {
        u32 gen = 0; // edit_state::gen
        usize size = 0;
        f32 width = -1.0f; // the wrap width (-1: no wrap)
        f32 text_size = 0.0f, scale = 0.0f;
        font_handle font = FONT_INVALID;
        bool operator==(const source &) const = default;
    };
    // One codepoint of a paragraph being wrapped (the wrap's scratch).
    struct metric
    {
        usize at;    // byte offset of the codepoint
        f32 kern_in; // kerning from the previous codepoint of the paragraph
        f32 advance;
    };
    // Advances and kerning pairs memoized for one rebuild (same font and size
    // throughout, so a hit returns exactly what the lookup would): a document's
    // few hundred distinct pairs replace a glyph-map and kern-table lookup per
    // codepoint. Cleared at the start of every rebuild.
    struct metric_memo : kerning_memo
    {
        static constexpr u32 CPS = 512;
        u32 cp[CPS];
        f32 advance[CPS];
        void clear()
        {
            kerning_memo::clear();
            for (u32 &x : cp) x = UTF8_END;
        }
    };
    std::vector<line> lines;
    source key{};
    bool built = false;
    // The paragraph count, for the gutter before the lines are laid out.
    i32 paragraphs = 1;
    source para_key{};
    bool para_counted = false;
};

struct edit_state
{
    std::string buffer;
    usize caret = 0;
    usize anchor = 0;
    f32 scroll_x = 0.0f;  // keeps the caret inside the visible field
    f32 sticky_x = -1.0f; // text_area: the column Up/Down keep (-1: take the caret's)
    text_lines layout;    // text_area: cached visual lines
    u32 gen = 0;          // text_area: bumped whenever the buffer's content changes
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
struct sweep_clock
{
    f64 oldest = 0.0, previous = 0.0, ttl = 0.0;
    bool valid = false;
};

// Stable nodes are allocated in chunks and returned to their context's pool.
// Keep unordered-map hashing, traversal, reference stability and TTL semantics;
// only the node allocator changes. Declare ownership before the map base so
// the map releases every element before its pool is destroyed.
struct state_node_pool
{
    retained_pool_resource resource;
    explicit state_node_pool(std::pmr::memory_resource *upstream) : resource(256, upstream) {}
};
template <class Value>
struct pooled_state_map : private state_node_pool, std::pmr::unordered_map<uiid, Value>
{
    using table = std::pmr::unordered_map<uiid, Value>;
    explicit pooled_state_map(
        std::pmr::memory_resource *upstream = std::pmr::get_default_resource())
        : state_node_pool(upstream), table(&resource)
    {
    }
    pooled_state_map(const pooled_state_map &) = delete;
    pooled_state_map &operator=(const pooled_state_map &) = delete;
};

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
    pooled_state_map<entry> map;
    sweep_clock gc;
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
    pooled_state_map<entry> map;
    sweep_clock gc;
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
    sweep_clock gc;
};

struct a11y_tree
{
    struct node
    {
        uiid id = 0;
        a11y_role role = a11y_role::NONE;
        rect bounds{};
        u32 state = A11Y_NONE;
        std::string name, value;
    };
    std::vector<node> nodes;
    // The open frame's node count (the scratch tree only): nodes past it are
    // slots kept from earlier frames, reused so their strings keep their
    // capacity instead of being reallocated every frame.
    usize count = 0;
};

// Every keyed store lives in one struct: created with the context, freed with
// one `delete`, reached without casts. A new store is one member here.
struct table_scratch
{
    std::vector<i32> shown;
    std::vector<f32> widths, xs;
    table_scratch *nested = nullptr; // owned; one buffer set per active depth
    ~table_scratch() { delete nested; }
};

struct text_area_scratch
{
    struct number_mark
    {
        f32 y;
        i32 n;
    };
    std::vector<number_mark> numbers;
    text_area_scratch *nested = nullptr;
    ~text_area_scratch() { delete nested; }
};

struct overlay_entry;
struct ctx_stores
{
    // Popup overlays; heap-allocated so a recorder's address stays put while
    // it stands in for the device.
    std::vector<overlay_entry *> overlays;
    render_device *overlay_real = nullptr;     // the device while a recorder stands in
    render_painter *overlay_painter = nullptr; // borrowed while its overlay paints
    edit_store edits;
    anim_store anim;
    rect_store rects; // u.animate_rect
    scroll_store scrolls;
    split_store splits;
    defer_store defers;
    flat_map<u8> frame_ids;    // per-frame region/scope ids (duplicate check)
    flat_map<rect> widget_ids; // debug: interacted widget ids -> first rect
    // The open frame's native-caption inputs (published to window::caption at
    // end_frame): drag regions and every interacted rect (the hole candidates).
    struct caption_rect
    {
        uiid id = 0;
        rect r{};
    };
    std::vector<caption_rect> caption_drags;
    std::vector<caption_rect> caption_hits;
    usize caption_candidates = 0; // includes clipped/blocked/non-focusable submissions
    // Finished file dialogs waiting for take_file_dialog (few at a time).
    struct dialog_entry
    {
        uiid id = 0;
        std::string paths; // NUL-separated
        i32 count = 0;
        bool cancelled = false;
    };
    std::vector<dialog_entry> dialogs;
    std::vector<f32> row_tops; // scroll_view::virtual_list(height_of) scratch
    table_scratch tables;
    table_scratch *table_active = nullptr; // borrowed during a table callback
    // virtual_list(height_of) offsets kept per list (virtual_list_opts::heights_version)
    struct kept_rows
    {
        u64 version = 0;
        std::vector<f32> tops; // tops[i]: row i's offset; one entry per measured row + 1
    };
    std::unordered_map<uiid, kept_rows> row_tops_kept;
    std::vector<text_lines::metric> wrap_metrics; // text_area word wrap scratch
    text_lines::metric_memo wrap_memo;            // ... and its per-rebuild memo
    kerning_memo layout_pairs;                    // long built-in layout kerning only
    struct sector_topology
    {
        i32 segments = 0, rings = 0;
        std::vector<i32> indices;
    };
    sector_topology sectors[8]; // bounded: only <= 128 segments are retained
    text_area_scratch text_areas;
    text_area_scratch *text_area_active = nullptr;
    a11y_tree a11y_frame; // the open frame's accessibility nodes
    // ui::sample_pixel requests (read at end_frame) and their results.
    struct pixel_request
    {
        uiid id = 0;
        vec2 at{};
    };
    std::vector<pixel_request> pixel_requests;
    std::unordered_map<uiid, color> pixel_results;
    // Small per-widget state of the components (open flags, a viewed month,
    // a remembered hue): a component owns the meaning of the fields.
    struct comp_state
    {
        bool open = false;
        bool flag = false;
        i32 a = 0, b = 0;
        f32 x = -1.0f, y = -1.0f;
    };
    std::unordered_map<uiid, comp_state> comps;
    std::string dialog_taken; // backs the last dialog_result handed out
};

namespace detail
{
template <class Scratch> struct scratch_scope
{
    Scratch *&active;
    Scratch *previous;
    Scratch &value;
    static Scratch &acquire(Scratch &root, Scratch *current)
    {
        if (!current) return root;
        if (!current->nested) current->nested = new Scratch();
        return *current->nested;
    }
    scratch_scope(Scratch &root, Scratch *&current)
        : active(current), previous(current), value(acquire(root, current))
    {
        active = &value;
    }
    ~scratch_scope() { active = previous; }
    scratch_scope(const scratch_scope &) = delete;
    scratch_scope &operator=(const scratch_scope &) = delete;
};

struct table_scratch_scope : scratch_scope<table_scratch>
{
    explicit table_scratch_scope(ctx_stores &s) : scratch_scope(s.tables, s.table_active) {}
};

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
template <class M> inline void sweep_unused(M &map, f64 now, f64 ttl, sweep_clock *clock = nullptr)
{
    // Use the original subtraction, including its exact floating-point TTL edge.
    // Every subsequently inserted/touched entry has last_used >= this bound.
    // A backwards/non-finite clock or a different TTL always takes the scan path.
    if (clock)
    {
        const bool skip = clock->valid && std::isfinite(now) && now >= clock->previous &&
                          ttl == clock->ttl && !(now - clock->oldest > ttl);
        clock->previous = now;
        if (skip) return;
        clock->oldest = now; // also bounds future inserts into an empty store
        clock->ttl = ttl;
        clock->valid = std::isfinite(now) && std::isfinite(ttl);
    }
    for (auto it = map.begin(); it != map.end();)
    {
        if (now - it->second.last_used > ttl)
            it = map.erase(it);
        else
        {
            if (clock && it->second.last_used < clock->oldest) clock->oldest = it->second.last_used;
            ++it;
        }
    }
}
