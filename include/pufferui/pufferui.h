#pragma once
// ============================================================================
// PufferUI — redesigned core (immediate mode, organized around rect slicing).
//
// Single header, real declaration / implementation split. Define
// PUFFERUI_IMPLEMENTATION in exactly one translation unit.
//
// Implemented: Phase 0 (types, validation, rect algebra, context, region/ID
// stack, interaction, cursors), Phase 1 (draw list + batching, minimal theme,
// SDL3 backend), and text (UTF-8 decode, stb_truetype glyph atlas, textures,
// ui.text / ui.text_width / ui.button).
//
// Optional macros:
//   PUFFERUI_IMPLEMENTATION  - compile the implementation (one TU)
//   PUFFERUI_ENABLE_SDL3     - also compile the SDL3 renderer backend
//
// Naming: types/functions/variables lower_snake_case; macros and
// constants/enumerators UPPER_SNAKE_CASE.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <span>
#include <initializer_list>
#include <type_traits>
#include <utility>
#include <new>

namespace pui
{

// ---------------------------------------------------------------- core types
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;
using f32 = float;
using f64 = double;
using usize = std::size_t;
using uiid = u64;
using texture_handle = void *;
using font_handle = i32;

inline constexpr f32 PI = 3.14159265358979323846f;
inline constexpr font_handle FONT_INVALID = -1;
inline constexpr u32 UTF8_END = 0xFFFFFFFFu;
inline constexpr u32 UTF8_REPLACEMENT = 0xFFFDu;

inline constexpr i32 MAX_ID_DEPTH = 64;
inline constexpr i32 MAX_CLIP_DEPTH = 64;
// MAX_FRAME_IDS was removed (r89): region-id duplicate checking is a per-frame
// hash set, complete at any scale. VIOL_FRAME_IDS_OVERFLOW stays in the enum
// for code stability but is no longer reachable.
inline constexpr i32 MAX_STYLE_SCOPES = 16;
inline constexpr i32 MAX_POPUPS = 8;
inline constexpr i32 MAX_PANELS = 16;
inline constexpr i32 MAX_DOCK_PANELS = 8;
inline constexpr i32 MAX_DOCK_DEPTH = 8;

// ---------------------------------------------------------------- validation

struct context;

context *current_context();
void set_current_context(context *);
// The current-context slot is thread-local: one context per thread. Threads
// that draw must create their own context (or serialize access to a shared
// one around whole frames).

// Every contract guard in the library, enumerated. `violation_last_code`
// hands the last one back (VIOL_NONE when clean); the codes are stable, and
// tests and overlays may rely on them. The guard text stays the human part.
enum violation_code : u32
{
    VIOL_NONE = 0,
    VIOL_INVALID_SLICE,         // a cut/cursor/split produced an invalid slice
    VIOL_INVALID_RECT,          // a draw call got an invalid rect
    VIOL_INVALID_AREA,          // a region/scroll area is invalid
    VIOL_DUP_REGION_ID,         // duplicate region id among siblings
    VIOL_WINDOW_LIMIT,          // add_window beyond MAX_WINDOWS
    VIOL_SHARED_DEVICE,         // a second window without backend_caps::SHARED_DEVICE
    VIOL_REMOVE_WHILE_OPEN,     // remove_window while a frame is open
    VIOL_DESTROY_WHILE_OPEN,    // destroy_context with an open window frame
    VIOL_FRAME_ALREADY_OPEN,    // begin_frame: another window frame is open
    VIOL_WINDOW_NOT_REGISTERED, // begin_frame: window is not registered
    VIOL_END_WITHOUT_BEGIN,     // end_frame without begin_frame
    VIOL_CLIP_UNBALANCED,       // region clip push without pop at end_frame
    VIOL_SCOPE_UNBALANCED,      // a button style scope left open or overflowed
    VIOL_POPUP_OVERFLOW,        // popup stack overflow
    VIOL_COMBO_EMPTY,           // combo needs at least one item
    VIOL_LAYOUT_CLAMPED,        // a requested slice was clamped (off by default)
    VIOL_NO_FONT,               // text drawn while the theme has no font
    VIOL_INPUT_OVERFLOW,        // text input did not fit the per-frame buffer
    VIOL_DUP_WIDGET_ID,         // the same widget id interacted twice in a frame
    VIOL_FRAME_IDS_OVERFLOW,    // over MAX_FRAME_IDS regions; dup checking incomplete
    VIOL_DUP_MOTION_KEY,        // animate_rect called twice for one key in a frame
    VIOL_PANEL_NO_ID,           // a panel without an explicit id (panel_opts::id / dock_panel)
    VIOL_COUNT
};

void report_violation(violation_code code, const char *condition, const char *message,
                      const char *file, int line, bool fatal);

// Per-context violation sink. When set, every check failure is reported to it
// instead of stderr (the count still accumulates). Tests install one to assert
// the exact guard that fired; apps can use it to route violations to a log.
using violation_handler = void (*)(void *user, const char *condition, const char *message,
                                   const char *file, i32 line);
void set_violation_handler(context *c, violation_handler handler, void *user);

// Compile-time format checking for printf-style functions (non-static member
// functions count `this` as argument 1). MSVC checks via SAL elsewhere.
#if defined(__GNUC__)
#define PUFFERUI_PRINTF(fmt_arg, first_arg) __attribute__((format(printf, fmt_arg, first_arg)))
#else
#define PUFFERUI_PRINTF(fmt_arg, first_arg)
#endif

#define PUFFERUI_CHECK(code, cond, message)                                                        \
    do                                                                                             \
    {                                                                                              \
        if (!(cond)) ::pui::report_violation(code, #cond, message, __FILE__, __LINE__, false);     \
    } while (0)
#define PUFFERUI_ASSERT(code, cond, message)                                                       \
    do                                                                                             \
    {                                                                                              \
        if (!(cond)) ::pui::report_violation(code, #cond, message, __FILE__, __LINE__, true);      \
    } while (0)
#define PUFFERUI_VERIFY(cond)                                                                      \
    ((cond) ? true                                                                                 \
            : (::pui::report_violation(VIOL_NONE, #cond, "", __FILE__, __LINE__, false), false))

// ---------------------------------------------------------------- math helpers
inline constexpr f32 min2(f32 a, f32 b)
{
    return a < b ? a : b;
}
inline constexpr f32 max2(f32 a, f32 b)
{
    return a > b ? a : b;
}
inline constexpr f32 clampf(f32 v, f32 lo, f32 hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}
inline constexpr bool is_finite(f32 v)
{
    return v == v && v > -3.0e38f && v < 3.0e38f;
}

// ---------------------------------------------------------------- UTF-8
// Decodes the next codepoint from s starting at index i (advanced past it).
// Returns UTF8_END at end of string, UTF8_REPLACEMENT on malformed input.
inline u32 utf8_decode(std::string_view s, usize &i)
{
    if (i >= s.size()) return UTF8_END;
    u8 c0 = static_cast<u8>(s[i++]);
    if (c0 < 0x80) return c0;

    i32 extra;
    u32 cp;
    if ((c0 & 0xE0) == 0xC0)
    {
        extra = 1;
        cp = c0 & 0x1Fu;
    }
    else if ((c0 & 0xF0) == 0xE0)
    {
        extra = 2;
        cp = c0 & 0x0Fu;
    }
    else if ((c0 & 0xF8) == 0xF0)
    {
        extra = 3;
        cp = c0 & 0x07u;
    }
    else
    {
        return UTF8_REPLACEMENT;
    }

    for (i32 k = 0; k < extra; ++k)
    {
        if (i >= s.size()) return UTF8_REPLACEMENT;
        u8 cc = static_cast<u8>(s[i++]);
        if ((cc & 0xC0) != 0x80) return UTF8_REPLACEMENT;
        cp = (cp << 6) | (cc & 0x3Fu);
    }
    return cp;
}

inline usize utf8_count(std::string_view s)
{
    usize n = 0, i = 0;
    while (i < s.size())
    {
        (void)utf8_decode(s, i);
        ++n;
    }
    return n;
}

// ---------------------------------------------------------------- color
struct vec2
{
    f32 x = 0.0f, y = 0.0f;
};

// Non-owning callable reference (no allocation, no std::function).
template <class Signature> class function_ref;
template <class R, class... Args> class function_ref<R(Args...)>
{
    void *obj_ = nullptr;
    R (*fn_)(void *, Args...) = nullptr;

public:
    function_ref() = default;
    template <class F>
    function_ref(F &&f)
        : obj_(const_cast<void *>(static_cast<const void *>(&f))),
          fn_([](void *o, Args... a) -> R
              { return (*static_cast<std::remove_reference_t<F> *>(o))(std::forward<Args>(a)...); })
    {
    }
    R operator()(Args... a) const { return fn_(obj_, std::forward<Args>(a)...); }
    explicit operator bool() const { return fn_ != nullptr; }
};

struct measure_size
{
    f32 width = 0.0f, height = 0.0f;
};

struct color
{
    u8 r = 255, g = 255, b = 255, a = 255;
    static color white() { return {255, 255, 255, 255}; }
    static color black() { return {0, 0, 0, 255}; }
    static color transparent() { return {0, 0, 0, 0}; }
};

// ---------------------------------------------------------------- rect algebra
struct rect
{
    f32 x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;

    f32 left() const { return x; }
    f32 right() const { return x + w; }
    f32 top() const { return y; }
    f32 bottom() const { return y + h; }
    f32 center_x() const { return x + w * 0.5f; }
    f32 center_y() const { return y + h * 0.5f; }

    bool is_finite() const
    {
        return pui::is_finite(x) && pui::is_finite(y) && pui::is_finite(w) && pui::is_finite(h);
    }
    bool is_valid() const { return is_finite() && w >= 0.0f && h >= 0.0f; }

    static rect make(f32 px, f32 py, f32 pw, f32 ph) { return {px, py, pw, ph}; }

    static rect intersect(const rect &a, const rect &b)
    {
        f32 x0 = max2(a.x, b.x), y0 = max2(a.y, b.y);
        f32 x1 = min2(a.right(), b.right()), y1 = min2(a.bottom(), b.bottom());
        return {x0, y0, max2(0.0f, x1 - x0), max2(0.0f, y1 - y0)};
    }

    // The four mutating cuts. Ref-qualified `&` with the rvalue overload
    // deleted: `r.content().cut_top(h)` (cutting a temporary) is the classic
    // silent-overlap bug, and the compiler now rejects it outright. Chaining
    // on `rect::make(...)` stays legal via a named local.
    [[nodiscard]] rect cut_top(f32 amount) &
    {
        f32 a = clampf(amount, 0.0f, max2(0.0f, h));
        rect out{x, y, w, a};
        y += a;
        h -= a;
        PUFFERUI_CHECK(VIOL_INVALID_SLICE, out.is_valid(), "cut_top produced an invalid slice");
        return out;
    }
    rect cut_top(f32 amount) && = delete;
    [[nodiscard]] rect cut_bottom(f32 amount) &
    {
        f32 a = clampf(amount, 0.0f, max2(0.0f, h));
        rect out{x, y + h - a, w, a};
        h -= a;
        PUFFERUI_CHECK(VIOL_INVALID_SLICE, out.is_valid(), "cut_bottom produced an invalid slice");
        return out;
    }
    rect cut_bottom(f32 amount) && = delete;
    [[nodiscard]] rect cut_left(f32 amount) &
    {
        f32 a = clampf(amount, 0.0f, max2(0.0f, w));
        rect out{x, y, a, h};
        x += a;
        w -= a;
        PUFFERUI_CHECK(VIOL_INVALID_SLICE, out.is_valid(), "cut_left produced an invalid slice");
        return out;
    }
    rect cut_left(f32 amount) && = delete;
    [[nodiscard]] rect cut_right(f32 amount) &
    {
        f32 a = clampf(amount, 0.0f, max2(0.0f, w));
        rect out{x + w - a, y, a, h};
        w -= a;
        PUFFERUI_CHECK(VIOL_INVALID_SLICE, out.is_valid(), "cut_right produced an invalid slice");
        return out;
    }
    rect cut_right(f32 amount) && = delete;

    // Ratio cuts and non-mutating slices (pure rect algebra helpers).
    [[nodiscard]] rect cut_top_ratio(f32 ratio) & { return cut_top(h * clampf(ratio, 0.0f, 1.0f)); }
    rect cut_top_ratio(f32 ratio) && = delete;
    [[nodiscard]] rect cut_bottom_ratio(f32 ratio) &
    {
        return cut_bottom(h * clampf(ratio, 0.0f, 1.0f));
    }
    rect cut_bottom_ratio(f32 ratio) && = delete;
    [[nodiscard]] rect cut_left_ratio(f32 ratio) &
    {
        return cut_left(w * clampf(ratio, 0.0f, 1.0f));
    }
    rect cut_left_ratio(f32 ratio) && = delete;
    [[nodiscard]] rect cut_right_ratio(f32 ratio) &
    {
        return cut_right(w * clampf(ratio, 0.0f, 1.0f));
    }
    rect cut_right_ratio(f32 ratio) && = delete;

    rect top_slice(f32 amount) const
    {
        const f32 a = clampf(amount, 0.0f, h);
        return rect::make(x, y, w, a);
    }
    rect bottom_slice(f32 amount) const
    {
        const f32 a = clampf(amount, 0.0f, h);
        return rect::make(x, bottom() - a, w, a);
    }
    rect left_slice(f32 amount) const
    {
        const f32 a = clampf(amount, 0.0f, w);
        return rect::make(x, y, a, h);
    }
    rect right_slice(f32 amount) const
    {
        const f32 a = clampf(amount, 0.0f, w);
        return rect::make(right() - a, y, a, h);
    }

    // Align this rect inside `in_that` (this rect's size is kept).
    rect align_center(rect in_that) const
    {
        return rect::make(in_that.center_x() - w * 0.5f, in_that.center_y() - h * 0.5f, w, h);
    }
    rect align_h_center(rect in_that) const
    {
        return rect::make(in_that.center_x() - w * 0.5f, y, w, h);
    }
    rect align_v_center(rect in_that) const
    {
        return rect::make(x, in_that.center_y() - h * 0.5f, w, h);
    }
    rect align_left(rect in_that) const { return rect::make(in_that.x, y, w, h); }
    rect align_right(rect in_that) const { return rect::make(in_that.right() - w, y, w, h); }
    rect align_top(rect in_that) const { return rect::make(x, in_that.y, w, h); }
    rect align_bottom(rect in_that) const { return rect::make(x, in_that.bottom() - h, w, h); }

    // Shrink to the given width/height aspect ratio, centered in this rect.
    rect fit_aspect(f32 aspect) const
    {
        if (aspect <= 0.0f || w <= 0.0f || h <= 0.0f) return *this;
        f32 nw = h * aspect, nh = h;
        if (nw > w)
        {
            nw = w;
            nh = w / aspect;
        }
        return rect::make(center_x() - nw * 0.5f, center_y() - nh * 0.5f, nw, nh);
    }

    rect pad(f32 amount) const
    {
        return {x + amount, y + amount, max2(0.0f, w - 2.0f * amount),
                max2(0.0f, h - 2.0f * amount)};
    }
    rect pad(f32 pad_x, f32 pad_y) const
    {
        return {x + pad_x, y + pad_y, max2(0.0f, w - 2.0f * pad_x), max2(0.0f, h - 2.0f * pad_y)};
    }
    rect pad(f32 l, f32 t, f32 r, f32 b) const
    {
        return {x + l, y + t, max2(0.0f, w - l - r), max2(0.0f, h - t - b)};
    }

    bool contains(f32 px, f32 py) const { return px >= x && px <= x + w && py >= y && py <= y + h; }
    bool contains(const rect &other) const
    {
        return other.x >= x && other.y >= y && other.right() <= right() &&
               other.bottom() <= bottom();
    }
};

// ---------------------------------------------------------------- corner radii
// Per-corner radii for rounded shapes: 0 is a square corner, so "round only the
// top" is `corner_radii::top(12)` and corners may even differ:
// `{.tl = 24, .tr = 6, .br = 24, .bl = 6}`. Each radius is limited to half of
// the rectangle's shorter side.
struct corner_radii
{
    f32 tl = 0.0f, tr = 0.0f, br = 0.0f,
        bl = 0.0f; // top-left, top-right, bottom-right, bottom-left

    static constexpr corner_radii all(f32 r) { return {r, r, r, r}; }
    static constexpr corner_radii top(f32 r) { return {r, r, 0.0f, 0.0f}; }
    static constexpr corner_radii bottom(f32 r) { return {0.0f, 0.0f, r, r}; }
    static constexpr corner_radii left(f32 r) { return {r, 0.0f, 0.0f, r}; }
    static constexpr corner_radii right(f32 r) { return {0.0f, r, r, 0.0f}; }
};

// ---------------------------------------------------------------- hashing / ids
inline constexpr u64 FNV_OFFSET = 14695981039346656037ull;
inline constexpr u64 FNV_PRIME = 1099511628211ull;

constexpr u64 hash(std::string_view s)
{
    u64 value = FNV_OFFSET;
    for (unsigned char c : s)
    {
        value ^= c;
        value *= FNV_PRIME;
    }
    return value;
}

constexpr uiid operator""_id(const char *s, usize n)
{
    return hash(std::string_view(s, n));
}

inline uiid id_child(uiid parent, uiid salt)
{
    u64 h = FNV_OFFSET;
    for (i32 i = 0; i < 8; ++i)
    {
        h ^= static_cast<u8>((parent >> (i * 8)) & 0xFF);
        h *= FNV_PRIME;
    }
    for (i32 i = 0; i < 8; ++i)
    {
        h ^= static_cast<u8>((salt >> (i * 8)) & 0xFF);
        h *= FNV_PRIME;
    }
    return h;
}
inline uiid id_child(uiid parent, std::string_view key)
{
    return id_child(parent, hash(key));
}

// ---------------------------------------------------------------- alignment
enum class align : u8
{
    LEFT,
    CENTER,
    RIGHT
};
inline constexpr align ALIGN_LEFT = align::LEFT;
inline constexpr align ALIGN_CENTER = align::CENTER;
inline constexpr align ALIGN_RIGHT = align::RIGHT;

// ---------------------------------------------------------------- keyboard
enum class key : u8
{
    ESCAPE,
    ENTER,
    TAB,
    BACKSPACE,
    SPACE,
    LEFT,
    RIGHT,
    UP,
    DOWN,
    HOME,
    END,
    A,
    C,
    X,
    V,
    Z,
    Y,
    DEL
};
inline constexpr i32 KEY_COUNT = 18;

enum class cursor : u8
{
    ARROW,
    IBEAM,
    HAND,
    HRESIZE,
    VRESIZE
};
inline constexpr cursor CURSOR_ARROW = cursor::ARROW;
inline constexpr cursor CURSOR_IBEAM = cursor::IBEAM;
inline constexpr cursor CURSOR_HAND = cursor::HAND;
inline constexpr cursor CURSOR_HRESIZE = cursor::HRESIZE;
inline constexpr cursor CURSOR_VRESIZE = cursor::VRESIZE;

// Clipboard abstraction; supply one (e.g. SDL-backed) via set_clipboard().
struct clipboard
{
    virtual ~clipboard() = default;
    virtual bool get(std::string &out) = 0;
    virtual void set(std::string_view text) = 0;
};

// ---------------------------------------------------------------- animation
// An animation is a value that moves toward a target across frames. Progress is
// keyed UI state: call the same function every frame and use the returned value.
enum class easing : u8
{
    LINEAR = 0,
    EASE_IN,
    EASE_OUT,
    EASE_IN_OUT,
    EASE_OUT_BACK,
};

inline f32 ease(easing e, f32 t)
{
    t = clampf(t, 0.0f, 1.0f);
    switch (e)
    {
    case easing::LINEAR:
        return t;
    case easing::EASE_IN:
        return t * t;
    case easing::EASE_OUT:
        return 1.0f - (1.0f - t) * (1.0f - t);
    case easing::EASE_IN_OUT:
        return t < 0.5f ? 2.0f * t * t : 1.0f - 2.0f * (1.0f - t) * (1.0f - t);
    case easing::EASE_OUT_BACK:
    {
        const f32 c1 = 1.70158f, c3 = c1 + 1.0f;
        const f32 u = t - 1.0f;
        return 1.0f + c3 * u * u * u + c1 * u * u;
    }
    }
    return t;
}

// tween: time-parametric, settles in a fixed duration.
struct tween
{
    f32 duration = 0.2f;
    easing curve = easing::EASE_OUT;
    // A key seen for the first time (or again after its 5 s collection) starts at
    // the target instead of 0: state that is already there must not animate in.
    bool from_target = false;
};

// spring: physics; settles when it stops moving. Retargets keep velocity.
struct spring
{
    f32 stiffness = 180.0f;
    f32 damping_ratio = 1.0f; // 1 = critical, < 1 bounces
    f32 mass = 1.0f;
};

// Optional implicit transition for built-in style groups (duration 0 = off).
struct transition
{
    f32 duration = 0.0f;
    easing curve = easing::EASE_OUT;
};

// ---------------------------------------------------------------- style cascade
// Partial overrides use opt<T>: unset fields fall through the cascade
// (theme default -> role -> scopes -> instance).
template <class T> struct opt
{
    T value{};
    bool set = false;
};
template <class T> constexpr opt<T> some(T v)
{
    return {v, true};
}

struct button_style
{
    color bg = {40, 46, 56, 255};
    color hover_bg = {54, 62, 74, 255};
    color active_bg = {32, 38, 48, 255};
    color border = {48, 56, 68, 255};
    color text = {235, 240, 245, 255};
    f32 radius = 4.0f;
    opt<corner_radii> radii;     // when set, replaces `radius`: a radius per corner
    f32 border_thickness = 0.0f; // full outline; 0 = flat
    f32 pad_x = 10.0f;
    f32 pad_y = 6.0f;
    struct transition transition{}; // implicit hover/active color animation
};

struct button_override
{
    opt<color> bg, hover_bg, active_bg, border, text;
    opt<f32> radius, border_thickness, pad_x, pad_y;
    opt<corner_radii> radii; // per-corner radii: `.radii = some(corner_radii::right(14))`
    opt<transition> transition;
};

// Options for the text field's look; unset fields use the theme.
struct field_opts
{
    opt<corner_radii> radii;
};

// ---- toggle controls (the pui::comp library) --------------------------------
// Style structs + per-instance overrides for the built-in toggles. The theme
// carries one slot each (`theme::switch_ctrl` / `radio` / `segmented`);
// components resolve theme -> override.
struct switch_style
{
    color track_off = {40, 46, 56, 255};
    color track_on = {86, 156, 255, 255};
    color knob = {235, 240, 245, 255};
    f32 width = 38.0f;
    f32 height = 22.0f;
    f32 knob_pad = 3.0f;
    transition anim{0.12f, easing::EASE_OUT};
    bool animate = true;
};

struct radio_style
{
    color ring = {70, 80, 94, 255};
    color fill = {86, 156, 255, 255};
    f32 size = 16.0f;
    f32 gap = 10.0f;   // dot-to-label gap
    f32 ring_w = 1.5f; // outline thickness
};

struct segmented_style
{
    color bg = {26, 30, 38, 255};
    color selected = {86, 156, 255, 255};
    color text = {236, 240, 246, 255};
    color text_selected = {14, 18, 24, 255};
    color hover = {255, 255, 255, 22}; // laid over a hovered segment (selected or not)
    f32 radius = 6.0f;
    f32 pad = 2.0f;
    transition anim{0.10f, easing::EASE_OUT};
};

struct tabs_style
{
    color text = {140, 150, 165, 255};
    color text_active = {236, 240, 246, 255};
    color underline = {86, 156, 255, 255};
    color hover_bg = {44, 50, 62, 200};
    f32 radius = 6.0f;
    f32 underline_h = 2.0f;
    f32 gap = 4.0f;
    transition anim{0.10f, easing::EASE_OUT};
};

struct accordion_style
{
    color header_bg = {30, 34, 42, 255};
    color header_hover = {42, 48, 58, 255};
    color text = {236, 240, 246, 255};
    color chevron = {140, 150, 165, 255};
    f32 radius = 8.0f;
    f32 header_h = 30.0f;
    transition anim{0.14f, easing::EASE_OUT};
};

struct drawer_style
{
    color bg = {22, 25, 32, 252};
    color border = {44, 50, 61, 255};
    color scrim = {0, 0, 0, 110};
    f32 radius = 0.0f;
    transition anim{0.16f, easing::EASE_OUT};
};

struct toast_style
{
    color bg = {30, 34, 42, 246};
    color info = {86, 156, 255, 255};
    color success = {92, 190, 120, 255};
    color danger = {232, 90, 90, 255};
    color text = {236, 240, 246, 255};
    f32 width = 260.0f;
    f32 height = 44.0f;
    f32 gap = 8.0f;
    f32 radius = 8.0f;
    f32 lifetime = 3.5f;
    f32 fade = 0.25f;
    transition anim{0.18f, easing::EASE_OUT};
};

struct table_style
{
    color header_bg = {26, 30, 38, 255};
    color row_bg = {22, 25, 32, 255};
    color row_alt = {26, 30, 38, 255};
    color row_hover = {40, 46, 56, 255};
    color header_text = {140, 150, 165, 255};
    color text = {236, 240, 246, 255};
    color border = {44, 50, 61, 255};
    f32 row_h = 26.0f;
    f32 header_h = 28.0f;
    f32 radius = 0.0f;
};

struct palette_style
{
    color scrim = {0, 0, 0, 120};
    color bg = {26, 30, 38, 252};
    color border = {48, 56, 68, 255};
    color text = {236, 240, 246, 255};
    color hint = {140, 150, 165, 255};
    color selected = {44, 52, 64, 255};
    color accent = {86, 156, 255, 255};
    f32 width = 420.0f;
    f32 item_h = 30.0f;
    f32 radius = 10.0f;
};

// A titled card section (comp::section): title row, optional caption row, a
// divider, then the body. Colors come from the theme (text, text_dim, border).
struct section_style
{
    f32 title_size = 15.0f;
    f32 title_h = 20.0f;
    f32 caption_h = 16.0f;
    f32 caption_gap = 2.0f;    // below the caption row
    f32 no_caption_gap = 4.0f; // below the title when there is no caption
    f32 divider_gap = 3.0f;    // head -> divider line
    f32 body_gap = 10.0f;      // divider -> body
};

// The options form for buttons: named, order-independent, extensible.
struct button_opts
{
    uiid role = 0;           // theme role id ("primary"_id, "danger"_id, ...)
    button_override style{}; // per-instance style override
};

inline button_style merge_style(button_style s, const button_override &o)
{
    if (o.bg.set) s.bg = o.bg.value;
    if (o.hover_bg.set) s.hover_bg = o.hover_bg.value;
    if (o.active_bg.set) s.active_bg = o.active_bg.value;
    if (o.border.set) s.border = o.border.value;
    if (o.text.set) s.text = o.text.value;
    if (o.radius.set) s.radius = o.radius.value;
    if (o.radii.set) s.radii = o.radii;
    if (o.border_thickness.set) s.border_thickness = o.border_thickness.value;
    if (o.pad_x.set) s.pad_x = o.pad_x.value;
    if (o.pad_y.set) s.pad_y = o.pad_y.value;
    if (o.transition.set) s.transition = o.transition.value;
    return s;
}

// ---------------------------------------------------------------- skins
// A 9-slice (nine-patch) image: an atlas region plus border insets and a center
// mode. Corners and borders stay crisp at any destination size; edges and center
// follow the mode. `uv` is normalized to the atlas; insets are source pixels.
inline constexpr i32 SKIN_CENTER_STRETCH = 0;
inline constexpr i32 SKIN_CENTER_TILE = 1;
inline constexpr i32 SKIN_CENTER_NONE = 2;

struct skin_image
{
    texture_handle tex = nullptr;   // atlas texture (nullptr = unset)
    rect uv{0, 0, 0, 0};            // normalized atlas region (u0, v0, u1-u0, v1-v0)
    f32 src_w = 0.0f, src_h = 0.0f; // source region size in pixels
    f32 inset_l = 0.0f, inset_t = 0.0f, inset_r = 0.0f, inset_b = 0.0f;
    i32 center_mode = SKIN_CENTER_STRETCH;

    bool valid() const { return tex != nullptr && uv.w > 0.0f && uv.h > 0.0f; }
};

// Build an image from a pixel rect in a `atlas_w` x `atlas_h` atlas.
inline skin_image make_skin_image(texture_handle tex, i32 atlas_w, i32 atlas_h, rect px,
                                  f32 inset_l = 0.0f, f32 inset_t = 0.0f, f32 inset_r = 0.0f,
                                  f32 inset_b = 0.0f, i32 center_mode = SKIN_CENTER_STRETCH)
{
    skin_image img;
    img.tex = tex;
    img.src_w = px.w;
    img.src_h = px.h;
    if (atlas_w > 0 && atlas_h > 0)
    {
        const f32 aw = static_cast<f32>(atlas_w), ah = static_cast<f32>(atlas_h);
        img.uv = rect::make(px.x / aw, px.y / ah, px.w / aw, px.h / ah);
    }
    img.inset_l = inset_l;
    img.inset_t = inset_t;
    img.inset_r = inset_r;
    img.inset_b = inset_b;
    img.center_mode = center_mode;
    return img;
}

// ---------------------------------------------------------------- panel & card styles
enum panel_flags : u32
{
    PANEL_NONE = 0,
    PANEL_NO_TITLEBAR = 1 << 0,
    PANEL_NO_CONTROLS = 1 << 1,
    PANEL_NO_DRAG = 1 << 2,
    PANEL_NO_SHADOW = 1 << 3,
};

struct panel_style
{
    color bg = {28, 32, 40, 245};
    color titlebar_bg = {20, 24, 30, 255};
    color titlebar_text = {230, 236, 242, 255};
    color border = {48, 56, 68, 255};
    color close = {220, 70, 70, 255};
    f32 radius = 6.0f;
    f32 border_thickness = 1.0f;
    f32 titlebar_h = 30.0f;
    f32 padding = 10.0f;
    bool shadow = true;
};
struct panel_override
{
    opt<color> bg, titlebar_bg, titlebar_text, border, close;
    opt<f32> radius, border_thickness, titlebar_h, padding;
    opt<bool> shadow;
};

// The options form for panels: named, order-independent, extensible.
struct panel_opts
{
    uiid id = 0; // the panel's identity (drag/press/block state); required unless dock_panel is set
    u32 flags = PANEL_NONE;
    uiid dock_panel = 0;             // dock into a leaf/tab node of the app-owned tree
    const char *dock_name = nullptr; // the tab title when docked
    panel_override style{};          // per-instance style override
};
inline panel_style merge_style(panel_style s, const panel_override &o)
{
    if (o.bg.set) s.bg = o.bg.value;
    if (o.titlebar_bg.set) s.titlebar_bg = o.titlebar_bg.value;
    if (o.titlebar_text.set) s.titlebar_text = o.titlebar_text.value;
    if (o.border.set) s.border = o.border.value;
    if (o.close.set) s.close = o.close.value;
    if (o.radius.set) s.radius = o.radius.value;
    if (o.border_thickness.set) s.border_thickness = o.border_thickness.value;
    if (o.titlebar_h.set) s.titlebar_h = o.titlebar_h.value;
    if (o.padding.set) s.padding = o.padding.value;
    if (o.shadow.set) s.shadow = o.shadow.value;
    return s;
}

struct card_style
{
    color bg = {20, 24, 30, 200};
    color border = {48, 56, 68, 200};
    f32 radius = 4.0f;
    f32 border_thickness = 1.0f;
    f32 padding = 10.0f;
};
struct card_override
{
    opt<color> bg, border;
    opt<f32> radius, border_thickness, padding;
};
inline card_style merge_style(card_style s, const card_override &o)
{
    if (o.bg.set) s.bg = o.bg.value;
    if (o.border.set) s.border = o.border.value;
    if (o.radius.set) s.radius = o.radius.value;
    if (o.border_thickness.set) s.border_thickness = o.border_thickness.value;
    if (o.padding.set) s.padding = o.padding.value;
    return s;
}

// ---------------------------------------------------------------- popups
enum class popup_flags : u32
{
    NONE = 0,
    CLOSE_ON_ESCAPE = 1 << 0,
    CLOSE_ON_CLICK_OUTSIDE = 1 << 1,
    MODAL = 1 << 2,
};
inline constexpr popup_flags operator|(popup_flags a, popup_flags b)
{
    return static_cast<popup_flags>(static_cast<u32>(a) | static_cast<u32>(b));
}
inline constexpr bool has_flag(popup_flags flags, popup_flags bit)
{
    return (static_cast<u32>(flags) & static_cast<u32>(bit)) != 0;
}
inline constexpr popup_flags POPUP_CLOSE_ON_ESCAPE = popup_flags::CLOSE_ON_ESCAPE;
inline constexpr popup_flags POPUP_CLOSE_ON_CLICK_OUTSIDE = popup_flags::CLOSE_ON_CLICK_OUTSIDE;
inline constexpr popup_flags POPUP_MODAL = popup_flags::MODAL;

// ---------------------------------------------------------------- theming
inline constexpr i32 MAX_BUTTON_ROLES = 16;
struct button_role
{
    uiid id = 0;
    button_override ov{};
};

struct scroll_style
{
    f32 thickness = 8.0f;
    f32 margin = 2.0f;
    f32 min_thumb = 24.0f;
    f32 radius = 4.0f;
    color track = {28, 32, 40, 120};
    color thumb = {70, 80, 95, 220};
    color thumb_hover = {95, 108, 126, 235};
    color thumb_active = {60, 180, 255, 235};
};

struct theme
{
    color bg = {18, 20, 26, 255};
    color panel_bg = {28, 32, 40, 255};
    color border = {48, 56, 68, 255};
    color focus_border = {60, 180, 255, 255}; // focused field outline
    color text = {235, 240, 245, 255};
    color text_dim = {140, 150, 160, 255};
    color accent = {60, 180, 255, 255};
    color accent_hover = {90, 200, 255, 255};
    color widget_bg = {40, 46, 56, 255};
    color widget_hover = {54, 62, 74, 255};
    color widget_active = {32, 38, 48, 255};
    f32 radius = 4.0f;
    f32 border_thickness = 0.0f;       // widget outline; 0 = flat
    f32 focus_border_thickness = 1.0f; // focused field outline
    f32 spacing = 6.0f;
    f32 padding = 12.0f;
    // Control metrics (the layout rhythm): interactive rows use `control_h`,
    // dense rows `control_h_small`, glyphs `icon_size`. The size helpers
    // (`u.button_size` / `u.text_size`) read them, so examples and components
    // never hard-code 26–30px magic numbers.
    f32 control_h = 28.0f;
    f32 control_h_small = 22.0f;
    f32 icon_size = 16.0f;
    font_handle font = FONT_INVALID;
    f32 text_size = 15.0f;

    color selection = {60, 120, 200, 140};
    // Semantic tokens (r93): the palette components read for surfaces, text
    // and actions — separated from per-component styles, so retheming means
    // setting tokens once. Component styles default FROM these.
    struct tokens_t
    {
        color surface = {22, 25, 32, 255};           // panels, popups
        color surface_alt = {30, 34, 42, 255};       // controls, headers
        color on_surface = {236, 240, 246, 255};     // primary text
        color on_surface_dim = {140, 150, 165, 255}; // secondary text
        color primary = {86, 156, 255, 255};         // actions, selection
        color primary_hover = {122, 180, 255, 255};
        color danger = {232, 90, 90, 255};
        color success = {92, 190, 120, 255};
        color warning = {230, 170, 70, 255};
        color outline = {44, 50, 61, 255};
    } tokens;
    color caret = {235, 240, 245, 255};
    char decimal_separator = '.';
    cursor button_cursor = CURSOR_HAND;
    cursor text_cursor = CURSOR_IBEAM;

    button_style button;
    switch_style switch_ctrl;  // pui::comp::switch_toggle
    radio_style radio;         // pui::comp::radio_group
    segmented_style segmented; // pui::comp::segmented
    tabs_style tabs;           // pui::comp::tab_bar
    accordion_style accordion; // pui::comp::accordion_scope
    drawer_style drawer;       // pui::comp::drawer_scope
    toast_style toast;         // pui::comp::toast_draw
    table_style table;         // pui::comp::table
    palette_style palette;     // pui::comp::command_palette
    section_style section;     // pui::comp::section
    button_role button_roles[MAX_BUTTON_ROLES]{};
    i32 button_role_count = 0;

    panel_style panel;
    card_style card;
    scroll_style scrollbar;
};

// A friendlier default palette: a layered dark ramp (background < surface <
// elevated), a soft outline, generous radii and an 8px rhythm.
inline theme default_dark()
{
    theme t;
    t.bg = {13, 15, 19, 255};
    t.panel_bg = {22, 25, 32, 252};
    t.border = {42, 48, 58, 255};
    t.focus_border = {104, 168, 255, 255};
    t.text = {236, 240, 246, 255};
    t.text_dim = {140, 150, 165, 255};
    t.accent = {86, 156, 255, 255};
    t.accent_hover = {122, 180, 255, 255};
    t.widget_bg = {30, 34, 42, 255};
    t.widget_hover = {40, 46, 56, 255};
    t.widget_active = {24, 28, 35, 255};
    t.radius = 7.0f;
    t.border_thickness = 0.0f;
    t.focus_border_thickness = 1.5f;
    t.spacing = 8.0f;
    t.padding = 14.0f;
    t.selection = {86, 156, 255, 110};
    // tokens (the semantic layer the values above also feed)
    t.tokens.surface = t.panel_bg;
    t.tokens.surface_alt = t.widget_bg;
    t.tokens.on_surface = t.text;
    t.tokens.on_surface_dim = t.text_dim;
    t.tokens.primary = t.accent;
    t.tokens.primary_hover = t.accent_hover;
    t.tokens.outline = t.border;
    t.button.bg = {32, 37, 46, 255};
    t.button.hover_bg = {43, 49, 60, 255};
    t.button.active_bg = {26, 30, 38, 255};
    t.button.border = {52, 59, 71, 255};
    t.button.border_thickness = 1.0f;
    t.button.radius = 7.0f;
    t.panel.radius = 12.0f;
    t.panel.bg = {22, 25, 32, 250};
    t.panel.titlebar_bg = {28, 32, 40, 255};
    t.panel.border = {44, 50, 61, 255};
    t.panel.padding = 12.0f;
    t.card.radius = 10.0f;
    t.card.padding = 12.0f;
    t.scrollbar.thickness = 7.0f;
    t.scrollbar.thumb = {70, 78, 92, 220};
    t.scrollbar.thumb_hover = {96, 106, 122, 235};
    // toggle controls: the accent ramp
    t.switch_ctrl.track_off = {40, 46, 56, 255};
    t.switch_ctrl.track_on = t.accent;
    t.switch_ctrl.knob = {236, 240, 246, 255};
    t.radio.ring = {70, 80, 94, 255};
    t.radio.fill = t.accent;
    t.segmented.bg = {24, 28, 35, 255};
    t.segmented.selected = t.accent;
    t.segmented.text = {236, 240, 246, 255};
    t.segmented.text_selected = {12, 16, 22, 255};
    t.segmented.hover = {255, 255, 255, 22};
    t.tabs.text = {140, 150, 165, 255};
    t.tabs.text_active = {236, 240, 246, 255};
    t.tabs.underline = t.accent;
    t.accordion.header_bg = {30, 34, 42, 255};
    t.accordion.header_hover = {42, 48, 58, 255};
    t.accordion.text = {236, 240, 246, 255};
    t.drawer.bg = {22, 25, 32, 252};
    t.drawer.border = {44, 50, 61, 255};
    t.toast.info = t.accent;
    t.toast.success = t.tokens.success;
    t.toast.danger = t.tokens.danger;
    t.toast.text = t.text;
    t.table.border = t.border;
    t.table.text = t.text;
    t.palette.accent = t.accent;
    t.palette.text = t.text;
    t.palette.hint = t.text_dim;
    return t;
}

// Interpolates two themes (colors and the layout metrics): the app can ease
// between a dark and a light theme over a few frames. Respects reduced
// motion by simply being driven by the app's own t.
inline color theme_lerp_color(color a, color b, f32 t)
{
    const auto mix = [&](u8 x, u8 y)
    {
        return static_cast<u8>(clampf(
            static_cast<f32>(x) + (static_cast<f32>(y) - static_cast<f32>(x)) * t, 0.0f, 255.0f));
    };
    return color{mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), mix(a.a, b.a)};
}

// Every color and metric of every slot is interpolated (the macros keep one
// field to one line; discrete members - transitions, flags, fonts - stay `a`'s).
#define PUI_LC(m) r.m = theme_lerp_color(a.m, b.m, t)
#define PUI_LF(m) r.m = a.m + (b.m - a.m) * t
inline theme theme_lerp(const theme &a, const theme &b, f32 t)
{
    theme r = a;
    PUI_LC(bg), PUI_LC(panel_bg), PUI_LC(border), PUI_LC(focus_border), PUI_LC(text);
    PUI_LC(text_dim), PUI_LC(accent), PUI_LC(accent_hover), PUI_LC(widget_bg);
    PUI_LC(widget_hover), PUI_LC(widget_active), PUI_LC(selection), PUI_LC(caret);
    PUI_LC(tokens.surface), PUI_LC(tokens.surface_alt), PUI_LC(tokens.on_surface);
    PUI_LC(tokens.on_surface_dim), PUI_LC(tokens.primary), PUI_LC(tokens.primary_hover);
    PUI_LC(tokens.danger), PUI_LC(tokens.success), PUI_LC(tokens.warning);
    PUI_LC(tokens.outline);
    PUI_LF(radius), PUI_LF(border_thickness), PUI_LF(focus_border_thickness), PUI_LF(spacing);
    PUI_LF(padding), PUI_LF(control_h), PUI_LF(control_h_small), PUI_LF(icon_size);

    PUI_LC(button.bg), PUI_LC(button.hover_bg), PUI_LC(button.active_bg), PUI_LC(button.border);
    PUI_LC(button.text), PUI_LF(button.radius), PUI_LF(button.border_thickness);
    PUI_LF(button.pad_x), PUI_LF(button.pad_y);
    for (i32 i = 0; i < r.button_role_count && i < b.button_role_count; ++i)
    {
        // roles are matched by id; a role present in both themes interpolates its set fields
        if (a.button_roles[i].id != b.button_roles[i].id) continue;
        button_override &o = r.button_roles[i].ov;
        const button_override &x = a.button_roles[i].ov, &y = b.button_roles[i].ov;
        if (x.bg.set && y.bg.set) o.bg.value = theme_lerp_color(x.bg.value, y.bg.value, t);
        if (x.hover_bg.set && y.hover_bg.set)
            o.hover_bg.value = theme_lerp_color(x.hover_bg.value, y.hover_bg.value, t);
        if (x.active_bg.set && y.active_bg.set)
            o.active_bg.value = theme_lerp_color(x.active_bg.value, y.active_bg.value, t);
        if (x.border.set && y.border.set)
            o.border.value = theme_lerp_color(x.border.value, y.border.value, t);
        if (x.text.set && y.text.set)
            o.text.value = theme_lerp_color(x.text.value, y.text.value, t);
    }

    PUI_LC(panel.bg), PUI_LC(panel.titlebar_bg), PUI_LC(panel.titlebar_text), PUI_LC(panel.border);
    PUI_LC(panel.close), PUI_LF(panel.radius), PUI_LF(panel.border_thickness);
    PUI_LF(panel.titlebar_h), PUI_LF(panel.padding);
    PUI_LC(card.bg), PUI_LC(card.border), PUI_LF(card.radius), PUI_LF(card.border_thickness);
    PUI_LF(card.padding);
    PUI_LF(scrollbar.thickness), PUI_LF(scrollbar.margin), PUI_LF(scrollbar.min_thumb);
    PUI_LF(scrollbar.radius), PUI_LC(scrollbar.track), PUI_LC(scrollbar.thumb);
    PUI_LC(scrollbar.thumb_hover), PUI_LC(scrollbar.thumb_active);

    PUI_LC(switch_ctrl.track_off), PUI_LC(switch_ctrl.track_on), PUI_LC(switch_ctrl.knob);
    PUI_LF(switch_ctrl.width), PUI_LF(switch_ctrl.height), PUI_LF(switch_ctrl.knob_pad);
    PUI_LC(radio.ring), PUI_LC(radio.fill), PUI_LF(radio.size), PUI_LF(radio.gap);
    PUI_LF(radio.ring_w);
    PUI_LC(segmented.bg), PUI_LC(segmented.selected), PUI_LC(segmented.text);
    PUI_LC(segmented.text_selected), PUI_LC(segmented.hover), PUI_LF(segmented.radius);
    PUI_LF(segmented.pad);
    PUI_LC(tabs.text), PUI_LC(tabs.text_active), PUI_LC(tabs.underline), PUI_LC(tabs.hover_bg);
    PUI_LF(tabs.radius), PUI_LF(tabs.underline_h), PUI_LF(tabs.gap);
    PUI_LC(accordion.header_bg), PUI_LC(accordion.header_hover), PUI_LC(accordion.text);
    PUI_LC(accordion.chevron), PUI_LF(accordion.radius), PUI_LF(accordion.header_h);
    PUI_LC(drawer.bg), PUI_LC(drawer.border), PUI_LC(drawer.scrim), PUI_LF(drawer.radius);
    PUI_LC(toast.bg), PUI_LC(toast.info), PUI_LC(toast.success), PUI_LC(toast.danger);
    PUI_LC(toast.text), PUI_LF(toast.width), PUI_LF(toast.height), PUI_LF(toast.gap);
    PUI_LF(toast.radius), PUI_LF(toast.lifetime), PUI_LF(toast.fade);
    PUI_LC(table.header_bg), PUI_LC(table.row_bg), PUI_LC(table.row_alt), PUI_LC(table.row_hover);
    PUI_LC(table.header_text), PUI_LC(table.text), PUI_LC(table.border), PUI_LF(table.row_h);
    PUI_LF(table.header_h), PUI_LF(table.radius);
    PUI_LC(palette.scrim), PUI_LC(palette.bg), PUI_LC(palette.border), PUI_LC(palette.text);
    PUI_LC(palette.hint), PUI_LC(palette.selected), PUI_LC(palette.accent), PUI_LF(palette.width);
    PUI_LF(palette.item_h), PUI_LF(palette.radius);
    PUI_LF(section.title_size), PUI_LF(section.title_h), PUI_LF(section.caption_h);
    PUI_LF(section.caption_gap), PUI_LF(section.no_caption_gap), PUI_LF(section.divider_gap);
    PUI_LF(section.body_gap);
    return r;
}
#undef PUI_LC
#undef PUI_LF

inline void set_button_role(theme &t, uiid id, button_override ov)
{
    for (i32 i = 0; i < t.button_role_count; ++i)
    {
        if (t.button_roles[i].id == id)
        {
            t.button_roles[i].ov = ov;
            return;
        }
    }
    if (t.button_role_count < MAX_BUTTON_ROLES)
        t.button_roles[t.button_role_count++] = button_role{id, ov};
}

// ---------------------------------------------------------------- rendering
struct vertex
{
    f32 x = 0, y = 0, u = 0, v = 0;
    color c{};
};

enum class backend_caps : u32
{
    NONE = 0,
    RENDER_TARGETS = 1 << 0, // offscreen targets + filtered blit (blur)
    SCISSOR = 1 << 1,
    STREAMING_TEXTURES = 1 << 2,
    SHARED_DEVICE = 1 << 3, // one device may serve multiple windows
};
inline constexpr backend_caps operator|(backend_caps a, backend_caps b)
{
    return static_cast<backend_caps>(static_cast<u32>(a) | static_cast<u32>(b));
}
inline constexpr bool has_cap(backend_caps caps, backend_caps bit)
{
    return (static_cast<u32>(caps) & static_cast<u32>(bit)) != 0;
}

struct render_surface;

// One device shared by all windows. Owns textures/targets; the core keeps the
// draw list and only submits geometry here.
struct render_device
{
    virtual ~render_device() = default;
    virtual backend_caps caps() const { return backend_caps::SCISSOR; }

    virtual void begin_frame() {}
    virtual void end_frame() {}

    virtual render_surface *create_surface(void *native_window = nullptr)
    {
        (void)native_window;
        return nullptr;
    }

    // Presentation vsync (best effort; backends without control ignore it).
    virtual void set_vsync(bool enabled) { (void)enabled; }

    virtual texture_handle create_texture(i32 w, i32 h, const u8 *rgba) = 0;
    virtual void update_texture(texture_handle tex, i32 x, i32 y, i32 w, i32 h, const u8 *rgba) = 0;
    virtual void destroy_texture(texture_handle tex) = 0;

    virtual texture_handle create_target(i32 w, i32 h)
    {
        (void)w;
        (void)h;
        return nullptr;
    }
    virtual void destroy_target(texture_handle tex) { destroy_texture(tex); }
    virtual void blit(texture_handle src, const rect *src_rect, texture_handle dst,
                      const rect *dst_rect)
    {
        (void)src;
        (void)src_rect;
        (void)dst;
        (void)dst_rect;
    }

    virtual void set_target(texture_handle target) { (void)target; }
    virtual texture_handle current_target() const { return nullptr; }
    virtual void set_clip(const rect *clip) { (void)clip; }
    virtual void set_cursor(cursor c) { (void)c; }
    virtual void clear(color c) { (void)c; }

    virtual void draw(texture_handle tex, const vertex *vertices, i32 vertex_count,
                      const i32 *indices, i32 index_count) = 0;
};

// Per-window backbuffer on the shared device.
struct render_surface
{
    virtual ~render_surface() = default;
    virtual void make_current(render_device &device) { (void)device; }
    virtual texture_handle default_target() { return nullptr; }
    virtual texture_handle scene_target() { return nullptr; }
    virtual void output_size(i32 &w, i32 &h)
    {
        w = 0;
        h = 0;
    }
    virtual void present() {}
    virtual void resize() {}
};

struct null_surface : render_surface
{
    texture_handle scene = reinterpret_cast<texture_handle>(static_cast<usize>(3));
    i32 w = 0, h = 0;
    texture_handle scene_target() override { return scene; }
    void output_size(i32 &ow, i32 &oh) override
    {
        ow = w;
        oh = h;
    }
};

// Headless device used by tests and to prove contract completeness. Reports
// SHARED_DEVICE and hands out one surface per `create_surface` call; set
// `shared = false` to model a single-window backend in gating tests.
struct null_device : render_device
{
    null_surface my_surface;
    static constexpr i32 MAX_EXTRA_SURFACES = 7;
    null_surface extra[MAX_EXTRA_SURFACES];
    i32 surfaces_created = 0;
    bool shared = true;

    i32 draw_calls = 0;
    i32 vertices = 0;
    i32 textures = 0;
    i32 targets = 0;
    texture_handle last_texture = nullptr;
    i32 cursor_sets = 0;
    cursor last_cursor = cursor::ARROW;
    bool vsync = true;

    backend_caps caps() const override
    {
        backend_caps b =
            backend_caps::RENDER_TARGETS | backend_caps::SCISSOR | backend_caps::STREAMING_TEXTURES;
        if (shared) b = b | backend_caps::SHARED_DEVICE;
        return b;
    }
    void begin_frame() override
    {
        draw_calls = 0;
        vertices = 0;
    }
    render_surface *create_surface(void * = nullptr) override
    {
        if (surfaces_created == 0)
        {
            ++surfaces_created;
            return &my_surface;
        }
        if (!shared || surfaces_created > MAX_EXTRA_SURFACES) return nullptr;
        null_surface &s = extra[surfaces_created - 1];
        s.scene = reinterpret_cast<texture_handle>(static_cast<usize>(3 + surfaces_created));
        ++surfaces_created;
        return &s;
    }
    texture_handle create_texture(i32, i32, const u8 *) override
    {
        ++textures;
        return reinterpret_cast<texture_handle>(static_cast<usize>(0x200 + textures));
    }
    texture_handle create_target(i32, i32) override
    {
        ++targets;
        return reinterpret_cast<texture_handle>(static_cast<usize>(0x100 + targets));
    }
    void update_texture(texture_handle, i32, i32, i32, i32, const u8 *) override {}
    void destroy_texture(texture_handle) override {}
    void set_cursor(cursor c) override
    {
        ++cursor_sets;
        last_cursor = c;
    }
    void set_vsync(bool enabled) override { vsync = enabled; }
    void draw(texture_handle tex, const vertex *, i32 vertex_count, const i32 *, i32) override
    {
        ++draw_calls;
        vertices += vertex_count;
        last_texture = tex;
    }
};

// SDL3 device + surface. Returns nullptr when PufferUI was built without
// PUFFERUI_ENABLE_SDL3. Owned by the caller.
render_device *create_sdl3_device(void *sdl_renderer);
void destroy_sdl3_device(render_device *device);

// One-call SDL event routing (the SDL3 glue above): feeds every event to the
// right window (mouse, left+right buttons, wheel, keys with mods, text input,
// IME preedit, focus, move/resize) and keeps window clients in sync with SDL.
// A window-close request sets `window::close_requested` for the app to act on.
// Also installs the SDL3 window_host once, so custom titlebars work.
//   sdl3_route - route one already-polled SDL_Event (SDL_AppEvent style).
//   sdl3_pump  - poll SDL and route everything; returns true on SDL_EVENT_QUIT
//                (the app should exit). The event is passed as void* because
//                the declaration section does not include SDL.
bool sdl3_route(context *c, void *sdl_event);
bool sdl3_pump(context *c);
// The system clipboard (SDL3's) as a `clipboard` for set_clipboard(); stateless,
// owned by the library. `sdl3_app_init` already installs it. nullptr without SDL3.
clipboard *sdl3_system_clipboard();

// ---- one-call bootstrap (SDL3 builds) ----
// Owns the SDL-side objects a single-window app needs and hands them over as
// plain pointers. The declaration stays SDL-free: the SDL handles are void*
// here (the SDL3 glue casts them back).
//
//   sdl3_app_init    - SDL_Init + window (borderless + resizable) + renderer +
//                      device + surface + context + window with the native
//                      chrome installed. Returns false (and cleans up) on any
//                      failure.
//   sdl3_app_pump    - poll SDL, route every event, keep the window client in
//                      sync. Returns false when the app should stop
//                      (SDL_EVENT_QUIT or the window's close_requested).
//   sdl3_app_tick    - advance the app clock: fills `now` / `dt` for
//                      begin_frame.
//   sdl3_app_shutdown- destroy context + device + renderer + window; SDL_Quit.
//
// Fonts and the theme stay app-side (load_font + set_theme); see the README's
// smallest program for the complete app.
struct render_device;
struct render_surface;
struct context;
struct window;
struct sdl3_app
{
    void *sdl_window = nullptr;      // SDL_Window*
    void *sdl_renderer = nullptr;    // SDL_Renderer*
    render_device *device = nullptr; // owned; freed by sdl3_app_shutdown
    render_surface *surface = nullptr;
    context *ctx = nullptr; // owned
    window *win = nullptr;
    font_handle font = FONT_INVALID; // the base font the bootstrap loaded

    // options (set before sdl3_app_init)
    bool hidden = false;                 // create the window hidden (offscreen tests)
    const char *renderer_name = nullptr; // "software" pins the software renderer
    const char *asset_dir = nullptr;     // font root; defaults to PUFFERUI_ASSET_DIR
    // Opt-in idle sleep: when true and the library has nothing animating, the
    // pump waits for events instead of spinning at vsync (tools/editors on
    // laptops). Apps that animate OUTSIDE the library (their own clocks,
    // video, ...) must keep this false or drive their own wake-ups.
    bool wait_when_idle = false;

    // the app's clock
    f64 freq = 0.0;
    u64 last_counter = 0;
    f64 now = 0.0, dt = 1.0 / 60.0;
    i32 width = 0, height = 0;
    bool inited = false;
};
bool sdl3_app_init(sdl3_app &a, const char *title, i32 w, i32 h, int argc, char **argv);
bool sdl3_app_pump(sdl3_app &a);
void sdl3_app_tick(sdl3_app &a);
void sdl3_app_shutdown(sdl3_app &a);

// ---------------------------------------------------------------- context
struct draw_list;
struct text_store;
struct dock_node;

struct popup_entry
{
    uiid id = 0;
    rect area{};
    i32 win = 0;
};
struct panel_entry
{
    uiid id = 0;
    rect area{};
    i32 win = 0;
};

// ---------------------------------------------------------------- windows
// Multi-window: one shared device, one surface per window. Windows are framed
// one after another; a window's widgets only interact with its own input.
// `client` is the window's client rect in DESKTOP coordinates (all windows share
// one desktop space); `area` is the local client rect { 0, 0, w, h }.
inline constexpr i32 MAX_WINDOWS = 8;

struct ctx_stores; // keyed UI state (edit/anim/scroll/split/defer + id sets)
struct focus_list; // the keyboard focus ring (per window)
struct blur_store; // blur scratch targets (per window)

// Input queued by the platform layer between frames. The button *held* states
// and the pointer position are global (one pointer); press/release edges and text
// land in the window they were routed to.
struct window_input
{
    bool mouse_pressed = false;
    bool mouse_released = false;
    bool right_pressed = false;
    f32 wheel_x = 0.0f;
    f32 wheel_y = 0.0f;
    bool key_down[KEY_COUNT]{};
    bool key_pressed[KEY_COUNT]{};
    bool key_released[KEY_COUNT]{};
    char text_input[512]{};
    i32 text_len = 0;
    char ime_preedit[128]{};
    i32 ime_preedit_len = 0;
    i32 ime_cursor = 0;
    bool shift_down = false;
    bool ctrl_down = false;
};

// Per-window interaction state that survives between that window's frames. The
// context carries the open window's copy during a frame (it derives from this
// struct): begin_frame copies the window's in, end_frame copies it back, so a
// new field is added here once.
struct interaction_state
{
    uiid active = 0;
    uiid focus = 0;
    uiid focus_request = 0;
    uiid dragging_panel = 0;
    f32 drag_dx = 0.0f, drag_dy = 0.0f;
    uiid last_click_id = 0;
    f64 last_click_time = -1.0;
};

struct context;

struct window : interaction_state
{
    context *owner = nullptr;
    void *handle = nullptr; // native window (platform-specific)
    render_surface *surface = nullptr;
    rect client{0, 0, 0, 0}; // client rect, desktop coordinates
    rect area{0, 0, 0, 0};   // local client rect
    uiid root = 0;           // per-window root region id (unique)
    i32 index = 0;
    bool focused = false;
    bool close_requested = false;      // the titlebar's close button or the platform asked
    bool maximized = false;            // reflected by the host / event pump
    blur_store *blur = nullptr;        // impl-side per-window blur scratch
    focus_list *focus_store = nullptr; // impl-side per-window focus ring
    window_input in{};
};

// Platform actions a custom titlebar needs from the native window. Backends
// that own windows implement it (the SDL3 glue does); with no host installed
// the titlebar widget still draws and reports clicks, but moving, resizing,
// minimizing and maximizing do nothing.
struct window_host
{
    virtual ~window_host() = default;
    // Move the window's client top-left to the desktop point (screen pixels on
    // the platform's global space, the same space as `window::client`).
    virtual void move_window(window &w, f32 desktop_x, f32 desktop_y) = 0;
    virtual void resize_window(window &w, f32 width, f32 height) = 0;
    virtual void minimize_window(window &w) { (void)w; }
    // Maximize when restored, restore when maximized.
    virtual void toggle_maximize(window &w) { (void)w; }
    // Install the platform's native window chrome for a borderless window:
    // native caption dragging (the system's own Aero Snap and its preview),
    // and native edge/corner resizing. Backends that can map the library's
    // chrome geometry onto platform hit-testing override it; a no-op host
    // leaves the window with no native drag/resize.
    virtual void install_system_chrome(window &w) { (void)w; }
};

// What the titlebar widget did this frame (besides platform actions through
// the host; a close click sets window::close_requested rather than returning).
struct titlebar_result
{
    bool close_clicked = false;
    bool minimize_clicked = false;
    bool maximize_clicked = false;
};

// Platform "global mouse" queries may return a different coordinate space than
// window clients (SDL3's SDL_GetGlobalMouseState returns unscaled screen
// pixels while its event coordinates are content-scaled); the titlebar uses
// the pointer tracked through `mouse_move(window&, ...)` instead.
void set_window_host(context *c, window_host *host);
window_host *window_host_of(context *c);

// An in-app text drag (browser-like): a selection is lifted from its source field
// and dropped into another (or the same) field. Move by default; hold Ctrl while
// dropping to copy. The pointer is global, so this belongs to the context.
struct text_drag_payload
{
    bool active = false;
    uiid source_id = 0;   // text field id that owns the range
    usize lo = 0, hi = 0; // byte range in the source buffer
    std::string text;     // the lifted text
};

// The context derives from `window_input` (this frame's input: edges, held keys,
// text, IME, modifiers) and `widget_state`: during a frame they hold the open
// window's copies, so `c->key_pressed[...]` / `c->focus` read as plain fields.
struct context : interaction_state, window_input
{
    render_device *device = nullptr;
    render_surface *surface = nullptr;
    draw_list *dl = nullptr;
    text_store *ts = nullptr;
    ctx_stores *st = nullptr; // keyed stores, created with the context
    bool reduced_motion = false;
    blur_store *blur = nullptr; // the open window's blur scratch

    // dock drag state (panel dragged by its tab)
    uiid dock_panel = 0;
    dock_node *dock_source = nullptr;
    const char *dock_panel_name = nullptr; // points into the owned buffer below
    char dock_panel_name_buf[128]{};       // owned: the drag ghost outlives the caller
    bool dock_dragging = false;
    f32 dock_press_x = 0.0f, dock_press_y = 0.0f;

    theme active_theme = default_dark();

    uiid parent_stack[MAX_ID_DEPTH]{};
    i32 parent_depth = 0;
    uiid current_region = 0;
    u64 seq = 0;

    rect clip_stack[MAX_CLIP_DEPTH]{};
    i32 clip_depth = 0;

    uiid hot = 0;
    bool redraw_requested = false; // ui::request_redraw(), valid until the next begin_frame
    bool focus_request_selects_all = false; // Tab focus selects the field's value

    clipboard *clip = nullptr;

    focus_list *focus_store = nullptr; // the open window's focus ring
    cursor want_cursor = cursor::ARROW;
    window_host *host = nullptr; // platform actions for custom titlebars

    // Layout-overflow reporting (see set_report_layout_overflow). Off by default.
    bool report_layout_overflow = false;
    i32 overflow_count = 0;

    f32 mouse_x = 0.0f, mouse_y = 0.0f;
    bool mouse_down = false; // global: one pointer
    bool right_down = false;
    uiid wheel_target = 0; // innermost hovered scroll view this frame

    text_drag_payload text_drag; // active in-app text drag (browser-like)

    u64 frame = 0;
    f64 now = 0.0, dt = 0.0;
    rect screen{0, 0, 0, 0};
    rect desktop{0, 0, 0, 0}; // union of all window clients (desktop coords)

    window *windows = nullptr; // heap-owned, grown on demand (start: MAX_WINDOWS)
    i32 window_capacity = 0;
    i32 window_count = 0;
    window *current_window = nullptr; // window whose frame is open (null between)
    window *focused_win = nullptr;    // receives keyboard input
    f32 mouse_global_x = 0.0f, mouse_global_y = 0.0f;

    button_override button_scopes[MAX_STYLE_SCOPES]{};
    i32 button_scope_depth = 0;

    // Growth-on-demand stacks (same pattern as the window list): the old
    // fixed caps (MAX_POPUPS/MAX_PANELS) are the INITIAL capacity; the arrays
    // double when full, so deep nesting never silently refuses a layer.
    popup_entry *popups = nullptr;
    i32 popup_depth = 0;
    i32 popup_capacity = 0;
    popup_entry *prev_popups = nullptr;
    i32 prev_popup_depth = 0;

    // combo/context_menu state (keyed by uiid, per window via the popup table).
    i32 combo_hot = -1;   // item index the dropdown/menu highlights (kbd + mouse)
    uiid tip_last_id = 0; // tooltip queue: widget the last tooltip belonged to
    f64 tip_last_shown = -1.0;
    uiid tip_stuck_id = 0; // tooltip queue: widget currently waiting out the delay
    f64 tip_wait_start = -1.0;
    f32 context_menu_x = 0.0f, context_menu_y = 0.0f; // where the menu was opened

    // Deferred overlays (see combo / context_menu / tooltip): the open menu and
    // the visible tooltip are drawn once at the end of the frame, so widgets
    // painted after their anchor can never cover them. One menu surface and
    // one tooltip at a time. The menu's labels are COPIED into context-owned
    // storage (reached via the `defers` handle; the caller's array may be a
    // stack local that dies before end_frame); the pick reports through
    // `defer_result_id`/`defer_result_pick`, which the widget's next call
    // consumes and applies to its model.
    uiid defer_menu_id = 0;
    rect defer_menu{};
    i32 defer_count = 0;
    bool defer_fresh = false;
    uiid defer_result_id = 0; // pick result awaiting the next widget call
    i32 defer_result_pick = -1;
    rect defer_tip{};
    char defer_tip_text[240]{};
    bool popup_click_outside = false;
    uiid popup_click_id = 0;
    bool in_popup = false; // currently drawing popup content
    i32 popup_layer_depth = 0;

    panel_entry *panels = nullptr;
    i32 panel_depth = 0;
    i32 panel_capacity = 0;
    panel_entry *prev_panels = nullptr;
    i32 prev_panel_depth = 0;
    uiid *panel_stack = nullptr;
    i32 panel_stack_depth = 0;
    i32 panel_stack_capacity = 0;

    i32 violations = 0;
    violation_handler vhandler = nullptr;
    void *vhandler_user = nullptr;
    char vlast[240]{}; // the last violation's message ("" when none)
    violation_code vlast_code = VIOL_NONE;
    bool break_on_violation = true; // debug builds stop the debugger (opt out per app)

    // Topmost-wins interaction: this frame's press claim and its rect (the
    // last submitted enabled rect containing the press position).
    uiid press_claim = 0;
    rect press_claim_rect{};
};

// ---------------------------------------------------------------- region (RAII)
struct ui;

struct region
{
    ui *u_ = nullptr;
    uiid id_ = 0;
    uiid parent_ = 0;
    rect area_{};
    rect content_{};
    rect clip_{};
    bool pushed_clip_ = false;

    region(ui &u, uiid key, rect area, bool push_clip = true);
    ~region();

    region(const region &) = delete;
    region &operator=(const region &) = delete;
    region(region &&) = delete;
    region &operator=(region &&) = delete;

    uiid id() const { return id_; }
    rect area() const { return area_; }
    [[nodiscard]] rect content() const { return content_; }
    rect clip() const { return clip_; }

    // A badge-sized rect pinned to one of this region's content corners, inset by
    // `margin` — the "label pinned to a corner, clear of other corners" helper.
    // `n` in 0..3, clockwise from the top-left corner (0 TL, 1 TR, 2 BR, 3 BL).
    rect corner(i32 n, f32 w, f32 h, f32 margin = 8.0f) const;
};

// Scoped button restyle: applies to buttons drawn within its lifetime.
struct style_scope
{
    ui *u_ = nullptr;
    bool pushed_ = false;

    style_scope(ui &u, button_override ov);
    ~style_scope();

    style_scope(const style_scope &) = delete;
    style_scope &operator=(const style_scope &) = delete;
    style_scope(style_scope &&) = delete;
    style_scope &operator=(style_scope &&) = delete;
};

// Scoped identity (RAII): pushes an id scope for loops and reusable
// components — widgets inside derive their ids with `u.local("part")`, so two
// instances of the same component never collide. Pure identity: no area, no
// clip. Duplicate scope keys are reported like duplicate regions.
struct id_scope
{
    ui *u_ = nullptr;
    uiid parent_ = 0;

    id_scope(ui &u, uiid key);
    ~id_scope();

    id_scope(const id_scope &) = delete;
    id_scope &operator=(const id_scope &) = delete;
    id_scope(id_scope &&) = delete;
    id_scope &operator=(id_scope &&) = delete;
};

// Scroll view flags
inline constexpr u32 SCROLL_NONE = 0;
inline constexpr u32 SCROLL_ALWAYS_RESERVE_BAR = 1 << 0; // stable gutter, no layout shift
inline constexpr u32 SCROLL_OVERLAY = 1 << 1;            // floating bar, no gutter
inline constexpr u32 SCROLL_NO_CLIP = 1 << 2;            // skip the viewport scissor

struct scroll_options
{
    f32 padding = 4.0f;
    u32 flags = SCROLL_NONE;
};

struct popup_scope;
struct panel_scope;
struct text_scope;
struct scroll_view;

// ---------------------------------------------------------------- docking
// App-owned dock tree. The framework reads it, resizes splits, switches tabs,
// and reports structural moves through `dock_action`; it never moves panels
// between nodes itself.
inline constexpr i32 DOCK_SPLIT_H = 0;
inline constexpr i32 DOCK_SPLIT_V = 1;
inline constexpr i32 DOCK_LEAF = 2;
inline constexpr i32 DOCK_TABS = 3;

inline constexpr i32 DOCK_ZONE_CENTER = 0;
inline constexpr i32 DOCK_ZONE_LEFT = 1;
inline constexpr i32 DOCK_ZONE_RIGHT = 2;
inline constexpr i32 DOCK_ZONE_TOP = 3;
inline constexpr i32 DOCK_ZONE_BOTTOM = 4;

struct dock_node
{
    i32 kind = DOCK_LEAF;
    f32 ratio = 0.5f;       // split fraction (top/left share)
    dock_node *a = nullptr; // split children
    dock_node *b = nullptr;
    uiid panels[MAX_DOCK_PANELS]{}; // leaf/tabs
    const char *panel_names[MAX_DOCK_PANELS]{};
    i32 panel_count = 0;
    i32 active = 0;
};

struct dock_action
{
    bool active = false;         // a drop happened this frame
    uiid panel = 0;              // which panel moved
    dock_node *target = nullptr; // nullptr == undock (floating)
    i32 zone = DOCK_ZONE_CENTER;
};

// ---------------------------------------------------------------- interaction
enum class widget_state : u8
{
    NORMAL,
    HOVERED,
    ACTIVE,
    FOCUSED,
    DISABLED,
    INVALID,
    SELECTED
};

struct interaction
{
    bool hovered = false;
    bool pressed = false;
    bool activated = false; // press edge: became active this frame
    bool held = false;
    bool clicked = false;
    bool double_clicked = false;
    bool right_clicked = false; // right-press edge while hovered
    bool focused = false;
    bool disabled = false;
    bool captured = false;
    bool blocked = false; // a popup/panel layer swallowed this widget's input
};

// Which pointer button an input event belongs to.
enum class pointer_button : u8
{
    LEFT = 0,
    RIGHT = 1,
    MIDDLE = 2
};

// ---------------------------------------------------------------- rect motion
// Options for u.animate_rect: a rect that comes from layout springs from where
// it was drawn to where the cutting puts it this frame.
struct rect_motion
{
    spring spec{240.0f, 0.8f}; // the same spring struct u.animate takes
    // Frame of reference: motion is measured relative to this point, so a
    // parent that moves (a scrolled content corner, an animating card) carries
    // its children without making them lag; only real layout changes animate.
    vec2 origin{};
    opt<rect> from;    // first sight: start here instead of at the target (entrances)
    bool snap = false; // jump to the target this frame (a dragged item follows the pointer)
};

// What a key's rect motion is doing (u.motion_info), in the frame of reference
// of its last animate_rect call.
struct rect_motion_info
{
    rect current{};
    rect target{};
    vec2 velocity{};    // px/s of the rect's center (squash and stretch, tilt)
    bool known = false; // false: no entry (never animated, or collected)
    bool settled = true;
};

// ---------------------------------------------------------------- ui
struct ui
{
    context *ctx = nullptr;
    explicit ui(context *c) : ctx(c) {}

    region region(rect area, uiid key, bool push_clip = true);
    // Scoped identity for loops and reusable components: widgets inside
    // derive with `local("part")`. RAII — destroy before end_frame.
    id_scope scope(uiid key) { return id_scope(*this, key); }

    uiid local(std::string_view key) const { return id_child(ctx->current_region, key); }
    uiid auto_id() { return id_child(0x5055494155544F00ull, ++ctx->seq); }

    // Layout metrics from the theme — the control rhythm (no magic numbers).
    f32 control_h() const { return ctx->active_theme.control_h; }
    f32 control_h_small() const { return ctx->active_theme.control_h_small; }
    f32 spacing() const { return ctx->active_theme.spacing; }
    f32 padding() const { return ctx->active_theme.padding; }
    // A button's natural size: label width + the theme's horizontal padding,
    // at the control height. `row::next(u.button_size("OK").x)` sizes rows.
    // (Non-const: first call may rasterize glyphs.)
    vec2 button_size(std::string_view label);
    // A single-line text's natural size (its drawn extent).
    vec2 text_size(std::string_view s);
    uiid hot_id() const { return ctx->hot; } // widget hovered this frame (tooltips)

    const theme &th() const { return ctx->active_theme; }

    bool key_pressed(key k) const { return ctx->key_pressed[static_cast<i32>(k)]; }
    bool key_down(key k) const { return ctx->key_down[static_cast<i32>(k)]; }
    // Mark a pressed key as handled so nothing later in the frame reacts to it.
    void consume_key(key k) { ctx->key_pressed[static_cast<i32>(k)] = false; }

    // Pointer state for a rect. `focusable = false` keeps the widget out of
    // the Tab ring (a click-capturing scrim, a passive hit area) while it
    // still interacts normally.
    interaction interact(uiid id, rect area, bool enabled = true, bool focusable = true);

    // text
    f32 text_width(std::string_view s);
    void text(rect r, std::string_view s, color c, align a = ALIGN_LEFT);
    // printf-style text: `u.textf(r, th.text, ALIGN_LEFT, "clicks: %d", n);`
    // (one formatted line, truncated at 511 bytes).
    void textf(rect r, color c, align a, const char *fmt, ...) PUFFERUI_PRINTF(5, 6);

    // measurement + wrapping
    measure_size measure_text(std::string_view s, f32 available_width);
    measure_size measure(function_ref<measure_size(f32)> fn, f32 available_width);
    void text_wrapped(rect r, std::string_view s, color c);
    // The height `text_wrapped` would draw into a `width`-wide rect: word wrap
    // with newline handling, measured without drawing. Use it to size containers
    // around wrapped paragraphs.
    f32 text_wrapped_height(f32 width, std::string_view s);
    // Truncates with a trailing U+2026 ellipsis when the line does not fit.
    void text_ellipsis(rect r, std::string_view s, color c, align a = ALIGN_LEFT);
    // Scoped font/size override for text drawn within its lifetime.
    text_scope text_style(f32 size, font_handle font = FONT_INVALID);
    // True when the active font has a real glyph for `cp` (not the replacement).
    bool has_glyph(u32 cp);
    // One line if it fits, otherwise wrapped within the rect. Returns the height
    // used (a single line, or the wrapped block's height).
    f32 text_fit(rect r, std::string_view s, color c, align a = ALIGN_LEFT);
    f32 line_height() const { return ctx->active_theme.text_size * 1.2f; }

    // True when `r` can paint: it intersects the active clip (always true
    // with no clip active). Widgets and custom drawing can skip work early.
    bool is_visible(rect r) const;

    // built-in widget (uses only the public API above)
    button_style resolve_button_style(uiid role_id = 0, const button_override &ov = {}) const;
    bool button(rect r, std::string_view label, uiid id, uiid role_id = 0, button_override ov = {});
    // Options-struct form: `u.button(r, "Save", "save"_id, {.role = "primary"_id})`.
    // The positional form stays as a thin wrapper.
    bool button(rect r, std::string_view label, uiid id, const button_opts &opts);
    void card(rect r, card_override ov = {});

    // common widgets
    bool checkbox(rect r, std::string_view label, bool &value, uiid id);
    bool slider_float(rect r, std::string_view label, f32 &value, f32 min_value, f32 max_value,
                      uiid id, const char *fmt = "%.1f");
    void progress_bar(rect r, f32 fraction, color fill, color bg);

    // Scrollable viewport (RAII): clips, offsets content, handles wheel + the
    // draggable scrollbar. State is keyed by `id`; set the content height while
    // the scope is alive (`set_content_height`).
    scroll_view scroll(rect viewport, uiid id, scroll_options ov = {});

    panel_scope panel(std::string_view title, rect &bounds, u32 flags = PANEL_NONE,
                      uiid dock_panel = 0, const char *dock_name = nullptr, panel_override ov = {});
    // Options form: `u.panel("Find", bounds, {.id = "find"_id})`. The positional form
    // takes its identity from `dock_panel`; a panel with neither reports
    // VIOL_PANEL_NO_ID (the title is never an identity).
    panel_scope panel(std::string_view title, rect &bounds, const panel_opts &opts);

    // splitters + docking
    std::pair<rect, rect> split_horizontal_interactive(uiid id, rect bounds, f32 *position,
                                                       f32 min_left = 60.0f, f32 max_left = 1.0e9f,
                                                       f32 thickness = 6.0f, f32 gap = 2.0f);
    std::pair<rect, rect> split_vertical_interactive(uiid id, rect bounds, f32 *position,
                                                     f32 min_top = 40.0f, f32 max_top = 1.0e9f,
                                                     f32 thickness = 6.0f, f32 gap = 2.0f);
    dock_action dock_space(uiid id, rect area, dock_node &root,
                           function_ref<void(uiid, rect, bool)> draw_panel);

    // typed inputs (write-back). text_field edits std::string directly;
    // number_field formats/parses a float using the codec behind the scenes.
    bool text_field(rect r, std::string &value, uiid id);
    // With options: `.radii = some(corner_radii::left(14))` rounds only the left
    // side, so a field can sit flush against a button.
    bool text_field(rect r, std::string &value, uiid id, const field_opts &opts);
    bool number_field(rect r, f32 &value, uiid id, const char *fmt = "%.2f");

    // overlay layer (draw after base UI; input capture uses the previous frame)
    popup_scope popup(uiid id, rect area, popup_flags flags = popup_flags::NONE);

    // Custom window chrome (for borderless windows): draws the titlebar with
    // minimize / maximize / close glyph buttons on the right. Native dragging
    // and resizing (the system's own Aero Snap, its preview, and
    // maximize-on-double-click) come from the platform through
    // `install_window_chrome`; this widget only paints and handles the
    // buttons. `bounds` shrinks by the bar's height (lay out below it). A
    // close click sets `window::close_requested` for the app to act on.
    titlebar_result titlebar(window &w, rect &bounds, std::string_view title);

    // combo: a closed button showing `items[selected]`; clicking opens a dropdown
    // popup listing every item. Returns true when `selected` changed this frame.
    // The dropdown is 6.2 * line height tall per item plus padding, scrolls when
    // taller than `popup_height` (0 = size to content, up to `max_popup_height`).
    bool combo(rect r, std::string_view label, const char *const *items, i32 item_count,
               i32 &selected, uiid id, f32 max_popup_height = 220.0f);

    // tooltip: draws a small dark tooltip under `anchor` while the widget `id`
    // is hovered. First tooltip waits ~0.5 s; moving between widgets within
    // ~1.2 s opens instantly (the emilkowal delay pattern). The paint defers to
    // the end of the frame (nothing drawn later can cover it); returns the
    // placement rect, or a zero rect when nothing was placed this frame.
    rect tooltip(rect anchor, uiid id, std::string_view tip);

    // context menu: one call owns the whole pattern — opens on right press while
    // `anchor` is hovered, draws `item_labels` as a menu positioned at the
    // pointer (clamped to the window), closes on pick / outside press / escape.
    // Returns the picked item index, or -1 while nothing was picked. The app
    // keeps no open flag: the menu state lives under `id` (see context_menu_open).
    i32 context_menu(uiid id, rect anchor, const char *const *item_labels, i32 item_count);

    // primitives
    void draw_triangles(texture_handle tex, const vertex *vertices, i32 vertex_count,
                        const i32 *indices, i32 index_count);
    void draw_rect(rect r, color c);
    void draw_line(f32 x0, f32 y0, f32 x1, f32 y1, color c, f32 thickness = 1.0f);
    void draw_rounded_rect(rect r, color c, f32 radius = 0.0f);
    // The same with a radius per corner (0 = square): `corner_radii::top(12)`.
    void draw_rounded_rect(rect r, color c, const corner_radii &radii);

    // images: one stretched quad, or a 9-slice patch (skins, custom widgets)
    void draw_image(const skin_image &img, rect dst);
    void draw_nine_slice(const skin_image &img, rect dst);

    // ---- animation (keys are region-scoped unless suffixed `_global`) ----
    // Call every frame; returns the current value moving toward `target`.
    f32 animate(uiid key, f32 target, const tween &spec);
    f32 animate(uiid key, f32 target, const spring &spec);
    f32 animate_global(uiid key, f32 target, const tween &spec);
    f32 animate_global(uiid key, f32 target, const spring &spec);
    // Frame-rate-independent exponential approach for continuously moving targets.
    f32 smooth(uiid key, f32 target, f32 half_life = 0.1f);
    // 0 on the first frame the key is seen, animating to 1 once (mount animation).
    f32 appear(uiid key, f32 duration = 0.2f, easing curve = easing::EASE_OUT);
    color animate_color(uiid key, color target, const tween &spec);
    // Animated layout: call once per frame with the rect the layout produced;
    // returns where to draw (and interact) this frame. Key it by the item's
    // identity (`u.local("pos")` inside `u.scope(item.id)`), never its index.
    // A new key starts at the target (or `from`); keys are collected after
    // 5 s of disuse; reduced motion snaps. Calling it twice for one key in a
    // frame is a reported violation (the second call does not step).
    rect animate_rect(uiid key, rect target, const rect_motion &m = {});
    rect_motion_info motion_info(uiid key) const;
    bool animations_active() const;
    // Ambient motion that is not a keyed animation (a drifting background, a
    // spinner you draw yourself): call this every frame it is moving, so the
    // idle-sleep gate (`needs_redraw`) keeps frames coming. A frame that does not
    // call it lets an otherwise quiet app go to sleep.
    void request_redraw() { ctx->redraw_requested = true; }
    void set_reduced_motion(bool on) { ctx->reduced_motion = on; }

    // custom shapes (used by custom widgets, e.g. radial menus)
    void set_cursor(cursor c) { ctx->want_cursor = c; }
    void blur(rect r, f32 blur_radius = 12.0f, f32 corner_radius = 0.0f, f32 alpha = 1.0f);
    // The same with a radius per corner, so a blurred panel can have square edges
    // where it docks to something: `u.blur(r, 20, corner_radii::bottom(16))`.
    void blur(rect r, f32 blur_radius, const corner_radii &radii, f32 alpha = 1.0f);
    void draw_polygon(std::span<const vec2> points, color c);
    void draw_sector(vec2 center, f32 r_in, f32 r_out, f32 a0, f32 a1, color c);
    void draw_arc(vec2 center, f32 radius, f32 thickness, f32 a0, f32 a1, color c);
};

// ---------------------------------------------------------------- components
// The built-in component library (`pui::comp`): reusable, themeable widgets
// built ONLY on the public API, following the component convention — explicit
// ids (never label-derived), props/result structs, theme-driven styles, no
// file-scope statics, keyboard operable, cursor feedback included. The
// implementations live in the implementation section.
namespace comp
{
struct switch_props
{
    uiid id = 0; // required: identity is explicit
    bool enabled = true;
    const switch_style *style = nullptr; // null = the theme slot; else copy-edit-pass
};
struct switch_result
{
    bool changed = false;
    interaction in{};
    explicit operator bool() const { return changed; }
};
// The track width + gap + label width: `row.next(comp::switch_size(u, "Wi-Fi").x)`.
vec2 switch_size(ui &u, std::string_view label);
// An animated toggle: click, or Space/Enter when focused. Writes `value`.
switch_result switch_toggle(ui &u, rect area, std::string_view label, bool &value,
                            const switch_props &p = {});

struct radio_props
{
    uiid id = 0; // required
    bool enabled = true;
    f32 row_h = 0.0f;                   // 0 = the theme's control height
    const radio_style *style = nullptr; // null = the theme slot; else copy-edit-pass
};
struct radio_result
{
    bool changed = false;
    i32 selected = -1;
    interaction in{};
    explicit operator bool() const { return changed; }
};
// A vertical radio group, one row per label. Every row joins the Tab ring;
// Space/Enter selects the focused row.
radio_result radio_group(ui &u, rect area, std::span<const char *const> labels, i32 &selected,
                         const radio_props &p = {});

struct segmented_props
{
    uiid id = 0; // required
    bool enabled = true;
    // Radii of the control's OUTER corners (default: all `style.radius`). The first
    // segment's left corners and the last one's right corners follow them, inset by
    // the padding so they stay concentric; corners between segments stay small. Use it
    // when the control sits in a rounded card: `.radii = some(corner_radii::bottom(18))`.
    opt<corner_radii> radii;
    const segmented_style *style = nullptr; // null = the theme slot; else copy-edit-pass
};
struct segmented_result
{
    bool changed = false;
    i32 selected = -1;
    interaction in{};
    explicit operator bool() const { return changed; }
};
// A horizontal segmented control, one segment per label: the selected
// segment is filled; clicks and Space/Enter select.
segmented_result segmented(ui &u, rect area, std::span<const char *const> labels, i32 &selected,
                           const segmented_props &p = {});

struct tabs_props
{
    uiid id = 0; // required
    bool enabled = true;
    const tabs_style *style = nullptr; // null = the theme slot; else copy-edit-pass
};
struct tabs_result
{
    bool changed = false;
    i32 active = -1;
    interaction in{};
    explicit operator bool() const { return changed; }
};
// A horizontal tab bar (the app draws the content below): the active tab is
// highlighted with an underline; clicks and Space/Enter activate.
tabs_result tab_bar(ui &u, rect area, std::span<const char *const> labels, i32 &active,
                    const tabs_props &p = {});

struct accordion_props
{
    uiid id = 0; // required
    bool enabled = true;
    f32 header_h = 0.0f;                    // 0 = the theme's
    f32 content_h = 0.0f;                   // the content's natural height (drives the animation)
    const accordion_style *style = nullptr; // null = the theme slot; else copy-edit-pass
};
// A collapsible section: the header toggles `open`, the content area animates
// its height and clips. Draw the content into `content()` while the scope is
// alive. `toggled()` reports the header interaction.
struct accordion_scope
{
    ui *u_ = nullptr;
    uiid id_ = 0;
    bool open_ = false;
    bool toggled_ = false;
    bool pushed_clip_ = false;
    rect content_{};

    accordion_scope(ui &u, rect area, std::string_view title, bool &open,
                    const accordion_props &p = {});
    ~accordion_scope();

    accordion_scope(const accordion_scope &) = delete;
    accordion_scope &operator=(const accordion_scope &) = delete;
    accordion_scope(accordion_scope &&) = delete;
    accordion_scope &operator=(accordion_scope &&) = delete;

    [[nodiscard]] rect content() const { return content_; }
    explicit operator bool() const { return open_; }
    bool toggled() const { return toggled_; }
};

enum class drawer_edge : u8
{
    LEFT,
    RIGHT
};

struct drawer_props
{
    uiid id = 0; // required
    bool enabled = true;
    f32 width = 280.0f;
    drawer_edge edge = drawer_edge::LEFT;
    bool scrim = true; // dim + click-to-close behind the drawer
    bool close_on_scrim_click = true;
    const drawer_style *style = nullptr; // null = the theme slot; else copy-edit-pass
};
// A sliding side drawer over `host`: dims (optionally), clips and slides its
// content in from an edge. The scrim captures clicks (and closes when
// `close_on_scrim_click`); the content widgets interact above it.
struct drawer_scope
{
    ui *u_ = nullptr;
    uiid id_ = 0;
    bool open_ = false;
    bool toggled_ = false;
    bool pushed_clip_ = false;
    rect content_{};

    drawer_scope(ui &u, rect host, bool &open, const drawer_props &p = {});
    ~drawer_scope();

    drawer_scope(const drawer_scope &) = delete;
    drawer_scope &operator=(const drawer_scope &) = delete;
    drawer_scope(drawer_scope &&) = delete;
    drawer_scope &operator=(drawer_scope &&) = delete;

    [[nodiscard]] rect content() const { return content_; }
    explicit operator bool() const { return open_; }
    bool toggled() const { return toggled_; }
};

// ---- toast host -------------------------------------------------------------
// An app-owned queue of transient notifications. `push` from anywhere (any
// frame); `toast_draw` renders the stack, ages it out and is a no-op while
// empty. Kind: 0 info, 1 success, 2 danger.
struct toast_item
{
    char text[160]{};
    u32 kind = 0;
    u64 serial = 0;
    f64 born = 0.0; // stamped on the first draw
    bool stamped = false;
    bool used = false;
};
struct toast_host
{
    static constexpr i32 MAX_TOASTS = 8;
    toast_item items[MAX_TOASTS]{};
    u64 next_serial = 1;

    void push(const char *text, u32 kind = 0);
    i32 alive() const;
};

struct toast_props
{
    uiid id = 0;
    bool enabled = true;
    const toast_style *style = nullptr; // null = the theme slot; else copy-edit-pass
};
// Stacks the live toasts downward from `anchor`'s top-right corner.
void toast_draw(ui &u, rect anchor, toast_host &host, const toast_props &p = {});

// ---- table ------------------------------------------------------------------
struct table_props
{
    uiid id = 0;
    bool enabled = true;
    f32 row_h = 0.0f;                   // 0 = the theme's
    f32 header_h = 0.0f;                // 0 = the theme's
    const table_style *style = nullptr; // null = the theme slot; else copy-edit-pass
};
struct table_result
{
    i32 clicked_row = -1;
    i32 clicked_col = -1;
    i32 focused_row = -1;
    explicit operator bool() const { return clicked_row >= 0; }
};
// A uniform-column table over `area` (headers pinned, body scrolls with
// virtual_list: only visible rows are submitted). `cell_draw` paints one
// cell — the component owns the chrome, hit-testing and scrolling.
table_result table(ui &u, rect area, std::span<const char *const> headers, i32 row_count,
                   function_ref<void(ui &, rect, i32 row, i32 col)> cell_draw,
                   const table_props &p = {});

// ---- command palette --------------------------------------------------------
struct palette_command
{
    const char *name = "";
    const char *hint = "";
};
struct palette_state
{
    std::string query;
    i32 active = 0;
    i32 first =
        0; // first visible entry: moves only to keep a keyboard highlight in view, or by wheel
    f32 pointer_x = -1.0f, pointer_y = -1.0f; // where the pointer was last frame: hover only
                                              // moves the highlight when the pointer moved
};
struct palette_props
{
    uiid id = 0;
    const palette_style *style = nullptr; // null = the theme slot; else copy-edit-pass
};
struct palette_result
{
    i32 chosen = -1; // the ORIGINAL command index, or -1
    i32 active = -1; // the highlighted entry this frame (filtered order)
    i32 shown = 0;   // how many entries passed the filter
    explicit operator bool() const { return chosen >= 0; }
};
// A modal filter-and-run overlay (Ctrl+K style). While `open`: the scrim
// captures clicks, the query field takes focus, Up/Down move, Enter chooses,
// Escape closes. Returns the chosen command index (or -1).
palette_result command_palette(ui &u, rect screen, bool &open, palette_state &st,
                               std::span<const palette_command> commands,
                               const palette_props &p = {});

struct section_props
{
    font_handle title_font = FONT_INVALID; // e.g. a bold face; invalid = the theme font
    const section_style *style = nullptr;  // null = the theme slot; else copy-edit-pass
};
// A card with a title (and optional caption) above a divider. Returns the body
// rect to lay content into. Non-interactive: no id needed.
rect section(ui &u, rect area, std::string_view title, std::string_view caption = {},
             const section_props &p = {});
} // namespace comp

// A top input-capturing layer. Construct it (as a prvalue) after the base UI so
// it draws on top; it pops on destruction.
struct popup_scope
{
    ui *u_ = nullptr;
    uiid id_ = 0;
    rect area_{};
    bool open = true;
    bool close_requested = false;

    popup_scope(ui &u, uiid id, rect area, popup_flags flags);
    ~popup_scope();

    popup_scope(const popup_scope &) = delete;
    popup_scope &operator=(const popup_scope &) = delete;
    popup_scope(popup_scope &&) = delete;
    popup_scope &operator=(popup_scope &&) = delete;

    [[nodiscard]] rect content() const { return area_; }
    explicit operator bool() const { return open; }
};

// A draggable floating container. The client area is a region scope, so children
// are ID-scoped and clipped; it pops on destruction.
struct panel_scope
{
    ui *u_ = nullptr;
    uiid id_ = 0;
    rect bounds_{};
    rect content_{};
    region client_;
    bool close_requested = false;
    bool open = true;
    bool layer_pushed = false;

    panel_scope(ui &u, uiid id, rect bounds, rect client, rect content, bool close_clicked,
                bool pushed)
        : u_(&u), id_(id), bounds_(bounds), content_(content), client_(u, id, client, true),
          close_requested(close_clicked), open(true), layer_pushed(pushed)
    {
    }
    ~panel_scope();

    panel_scope(const panel_scope &) = delete;
    panel_scope &operator=(const panel_scope &) = delete;
    panel_scope(panel_scope &&) = delete;
    panel_scope &operator=(panel_scope &&) = delete;

    [[nodiscard]] rect content() const { return content_; }
    explicit operator bool() const { return open; }
};

// Scoped font/size override for text drawn within its lifetime. Restores the
// previous theme font/size on destruction (keep it in a scoped block).
struct text_scope
{
    ui *u_ = nullptr;
    f32 size_ = 0.0f;
    font_handle font_ = FONT_INVALID;
    f32 old_size_ = 0.0f;
    font_handle old_font_ = FONT_INVALID;

    text_scope(ui &u, f32 size, font_handle font = FONT_INVALID);
    ~text_scope();

    text_scope(const text_scope &) = delete;
    text_scope &operator=(const text_scope &) = delete;
    text_scope(text_scope &&) = delete;
    text_scope &operator=(text_scope &&) = delete;
};

// Scrollable viewport (RAII). Clips to `viewport`, offsets its content, applies
// wheel input when the pointer is over it (innermost view wins), and draws a
// draggable scrollbar. Offset and content height are keyed by `id` and collected
// after 5 s of disuse, like animation keys.
//
//   {
//       scroll_view sv = u.scroll(area, "list"_id);
//       column col(sv.content(), 6.0f);
//       for (...) { ... col.next(...) ... }
//       sv.set_content_height(used);
//   }
struct scroll_view
{
    ui *u_ = nullptr;
    uiid id_ = 0;
    rect viewport_{};
    rect content_{};
    f32 content_h_ = 0.0f;
    f32 padding_ = 4.0f;
    u32 flags_ = SCROLL_NONE;
    bool overflows_ = false;
    bool clip_pushed_ = false;
    rect bar_track_{};
    rect bar_thumb_{};
    bool thumb_hovered_ = false;

    scroll_view(ui &u, uiid id, rect viewport, scroll_options ov);
    ~scroll_view();

    scroll_view(const scroll_view &) = delete;
    scroll_view &operator=(const scroll_view &) = delete;
    scroll_view(scroll_view &&) = delete;
    scroll_view &operator=(scroll_view &&) = delete;

    [[nodiscard]] rect content() const { return content_; }
    f32 offset() const;
    f32 content_height() const { return content_h_; }
    bool overflows() const { return overflows_; }
    void set_content_height(f32 height);
    void scroll_to(f32 offset);
    void scroll_by(f32 delta);
    void ensure_visible(rect r, f32 margin = 4.0f);
    // Draws the visible slice of a uniform list. Rows outside the viewport
    // are never submitted this frame — the callback only runs for indices
    // that can paint. The content height is derived from count * item_height
    // (no separate set_content_height call); irregular rows keep using manual
    // slicing with content().
    void virtual_list(i32 count, f32 item_height, function_ref<void(ui &, i32, rect)> fn);
};

// ---------------------------------------------------------------- cursors
struct column
{
    rect bounds_{};
    f32 gap_ = 6.0f;
    explicit column(rect area, f32 gap = 6.0f) : bounds_(area), gap_(gap) {}
    rect next(f32 height = 0.0f);
    [[nodiscard]] rect cut_top(f32 height) &;
    rect cut_top(f32 height) && = delete;
    [[nodiscard]] rect cut_bottom(f32 height) &;
    rect cut_bottom(f32 height) && = delete;
    void space(f32 amount = 6.0f);
    rect remaining() const { return bounds_; }
    column(const column &) = delete;
    column(column &&) = delete;
};

struct row
{
    rect bounds_{};
    f32 gap_ = 6.0f;
    explicit row(rect area, f32 gap = 6.0f) : bounds_(area), gap_(gap) {}
    rect next(f32 width = 0.0f);
    [[nodiscard]] rect cut_left(f32 width) &;
    rect cut_left(f32 width) && = delete;
    [[nodiscard]] rect cut_right(f32 width) &;
    rect cut_right(f32 width) && = delete;
    void space(f32 amount = 6.0f);
    rect remaining() const { return bounds_; }
    row(const row &) = delete;
    row(row &&) = delete;
};

// ---------------------------------------------------------------- tracks & grids
inline constexpr i32 MAX_TRACKS = 16;

enum class track_type
{
    FIXED,
    FLEX,
    RATIO,
    FIT_CONTENT
};

struct track_size
{
    track_type type = track_type::FLEX;
    f32 value = 1.0f;
    f32 min_size = 0.0f;
    f32 max_size = 1e9f;

    static track_size fixed(f32 px) { return {track_type::FIXED, px, px, px}; }
    static track_size flex(f32 weight = 1.0f) { return {track_type::FLEX, weight, 0.0f, 1e9f}; }
    static track_size ratio(f32 fraction) { return {track_type::RATIO, fraction, 0.0f, 1e9f}; }
    static track_size fit_content(f32 fallback_px = 0.0f, f32 max_px = 1e9f)
    {
        return {track_type::FIT_CONTENT, 0.0f, fallback_px, max_px};
    }

    track_size min(f32 m) const
    {
        track_size s = *this;
        s.min_size = m;
        return s;
    }
    track_size max(f32 m) const
    {
        track_size s = *this;
        s.max_size = m;
        return s;
    }
};

// Resolves FIXED / RATIO / FIT_CONTENT tracks against `available`, then shares
// what is left over the FLEX tracks (min/max clamped, redistributing space freed
// by clamped tracks). Writes at most `max_out` sizes; returns the count written.
i32 resolve_track_sizes(f32 available, std::span<const track_size> tracks, f32 *out, i32 max_out);

// Cursor over resolved horizontal tracks — a `row` that knows each column's size.
struct track_row
{
    rect bounds_{};
    f32 gap_ = 0.0f;
    f32 sizes_[MAX_TRACKS]{};
    i32 count_ = 0;
    i32 index_ = 0;

    track_row(rect area, std::span<const track_size> tracks, f32 gap = 0.0f);
    track_row(rect area, std::initializer_list<track_size> tracks, f32 gap = 0.0f)
        : track_row(area, std::span<const track_size>(tracks.begin(), tracks.size()), gap)
    {
    }

    rect next(f32 height = 0.0f);
    rect remaining() const;
    f32 size(i32 i) const { return (i >= 0 && i < count_) ? sizes_[i] : 0.0f; }
    i32 count() const { return count_; }
    i32 index() const { return index_; }
    bool done() const { return index_ >= count_; }
    track_row(const track_row &) = delete;
    track_row(track_row &&) = delete;
};

// Cursor over resolved vertical tracks.
struct track_column
{
    rect bounds_{};
    f32 gap_ = 0.0f;
    f32 sizes_[MAX_TRACKS]{};
    i32 count_ = 0;
    i32 index_ = 0;

    track_column(rect area, std::span<const track_size> tracks, f32 gap = 0.0f);
    track_column(rect area, std::initializer_list<track_size> tracks, f32 gap = 0.0f)
        : track_column(area, std::span<const track_size>(tracks.begin(), tracks.size()), gap)
    {
    }

    rect next(f32 width = 0.0f);
    rect remaining() const;
    f32 size(i32 i) const { return (i >= 0 && i < count_) ? sizes_[i] : 0.0f; }
    i32 count() const { return count_; }
    i32 index() const { return index_; }
    bool done() const { return index_ >= count_; }
    track_column(const track_column &) = delete;
    track_column(track_column &&) = delete;
};

// CSS `repeat(auto-fit, minmax(min_item_w, 1fr))`-style grid cursor. `next()`
// returns the next cell in reading order (a zero-size rect once exhausted);
// `cell(i)` addresses a cell directly.
struct grid_cursor
{
    rect bounds_{};
    f32 gap_ = 8.0f;
    f32 item_w_ = 0.0f;
    f32 item_h_ = 0.0f;
    i32 columns_ = 1;
    i32 rows_ = 1;
    i32 count_ = 0;
    i32 index_ = 0;

    rect next();
    rect cell(i32 i) const;
    bool done() const { return index_ >= count_; }
    i32 count() const { return count_; }
    i32 index() const { return index_; }
    i32 columns() const { return columns_; }
    i32 rows() const { return rows_; }
    f32 item_width() const { return item_w_; }
    f32 item_height() const { return item_h_; }
};

grid_cursor auto_fit_grid(rect container, i32 total_items, f32 min_item_w, f32 item_h,
                          f32 gap = 8.0f);

// Layout-overflow reporting: when enabled, a `column::next`/`row::next` (or
// track/grid slice) that gets clamped because the remaining space is smaller than
// requested reports a *non-fatal* violation ("column slice clamped: requested N
// px > remaining M px") — visible through `set_violation_handler`/`violation_count`,
// so authoring mistakes surface in tests instead of only in screenshots. Off by
// default; clamping itself is unchanged (still silent when disabled).
void set_report_layout_overflow(context *c, bool enabled);
i32 layout_overflow_count(context *c); // clamps counted since enable (or since reset)

// ---- development overlay ----
// The last violation message recorded on this context, exactly as the guard
// wrote it ("" when none). The messages are stable string literals, so tests
// may match on them; they are not localized and never change shape between
// builds.
const char *violation_last(context *c);
// The enumerated code of the last violation (VIOL_NONE when clean) - the
// stable, non-string identity tests may rely on. `violation_code_name`
// renders it.
violation_code violation_last_code(context *c);
const char *violation_code_name(violation_code code);
// Opt out of the debug build's stop-at-the-violating-call behavior (default:
// on in debug builds, only when no handler is installed).
void set_break_on_violation(context *c, bool enabled);

// ---- dock layout persistence ----
// Serialize a dock tree: one line per node — "0 split h 0.60" /
// "1 leaf 0 id1|Name1;id2|Name2" (depth, kind, then the split ratio or the
// active tab plus id|name pairs). Indent-free and line-based, so it round-
// trips through any text storage (settings files, clipboard, defaults).
std::string dock_save_tree(const dock_node &root);
// Restore into caller-owned node storage (`nodes`/`node_cap` — the same
// pattern as a drag-alloc pool; nodes are consumed in order). Every restored
// panel name points into `name_storage`, which must outlive the tree.
// Returns false on malformed input. `out_root` is the new tree (or null for
// an empty serialization).
bool dock_restore_tree(std::string_view text, dock_node *nodes, i32 node_cap,
                       std::string &name_storage, dock_node *&out_root);
// Draws a small "PufferUI: N violation(s) - <last message>" strip into `r`
// when the context has violations (a no-op otherwise, so release frames stay
// clean). Intended as an opt-in development overlay.
void draw_violation_overlay(ui &u, rect r);

// ---------------------------------------------------------------- frame + input
context *create_context(render_device *device = nullptr, render_surface *surface = nullptr);
void destroy_context(context *c);
void set_device(context *c, render_device *device, render_surface *surface);
void set_theme(context *c, const theme &t);

font_handle load_font(context *c, const char *path);

// ---- windows ----
// The first window may always be added; every later window requires
// backend_caps::SHARED_DEVICE (without it the call is a reported violation and
// returns nullptr, never a silent degrade). `handle` is the native window and
// is what event routing matches on.
//
// remove_window compacts the context's window list: every `window*` handed out
// for a window AFTER the removed one is invalidated (it may point past the
// list or at a different window, and using it is a reported violation).
// Re-fetch pointers with `window_at(handle)` after any removal; the first
// window's pointer never moves.
window *add_window(context *c, void *handle, render_surface *surface, rect client);
void remove_window(context *c, window &w);
window *window_at(context *c, void *handle);
window *first_window(context *c);

void set_window_client(window &w, rect client);
void focus_window(context *c, window &w);
window *focused_window(context *c);
rect desktop_rect(context *c);

// Pointer position in desktop coordinates (one pointer, all windows). The value
// MUST use the same coordinate space as the window client rects; prefer feeding
// window-local mouse events (`mouse_move(window&, ...)`), which the core converts
// into the shared space. Platform "global mouse" queries may return a different
// space (SDL3's SDL_GetGlobalMouseState returns unscaled screen pixels while its
// window and event coordinates are content-scaled).
void set_global_mouse(context *c, f32 x, f32 y);

// ---- frames ----
// One window's frame. Windows are framed one after another; the context keeps a
// single draw list, so a window's frame must be closed before the next begins.
void begin_frame(context *c, window &w, f64 now, f64 dt);
void begin_frame(context *c, f64 now, f64 dt, rect screen); // primary window
void end_frame(context *c);
// Conservative "needs redraw" gate for idle sleeping: true when an animation
// is unsettled, a text field is focused (the caret blinks), or a text drag is
// in flight. The bootstrap's `wait_when_idle` uses it per pump.
bool needs_redraw(const context *c);

// ---- input ----
// Per-window: mouse edges and text go to the window that received the event;
// the held state and pointer position are global.
void mouse_move(window &w, f32 x, f32 y);
void mouse_button(window &w, bool down);                   // left button
void mouse_button(window &w, pointer_button b, bool down); // any button
void key_event(window &w, key k, bool down);
// The OS auto-repeat of a key that is already held: raises another key_pressed edge
// (text fields repeat Backspace/arrows, sliders step) without a new key-down.
void key_repeat(window &w, key k);
void text_input_event(window &w, const char *utf8);
void ime_event(window &w, const char *preedit, i32 cursor);
void mods_event(window &w, bool shift, bool ctrl);
void mouse_wheel(window &w, f32 dx, f32 dy);

// Single-window convenience: routes to the primary window.
void mouse_move(context *c, f32 x, f32 y);
void mouse_button(context *c, bool down);
void mouse_button(context *c, pointer_button b, bool down);
void key_event(context *c, key k, bool down);
void text_input_event(context *c, const char *utf8);
void ime_event(context *c, const char *preedit, i32 cursor);
void mods_event(context *c, bool shift, bool ctrl);
void mouse_wheel(context *c, f32 dx, f32 dy);
void set_clipboard(context *c, clipboard *clip);

// ---- native window chrome (borderless windows) ----
// Install the platform's native chrome handling for a borderless window:
// native caption dragging (the system's own Aero Snap, its preview, and
// maximize-on-double-click) and native edge/corner resizing with generous
// borders. Call once per window after add_window; no-op without a backend
// that supports it.
void install_window_chrome(context *c, window &w);

i32 violation_count(context *c);

} // namespace pui

// ============================================================================
// ============================ implementation ================================
// ============================================================================
#if defined(PUFFERUI_IMPLEMENTATION)

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <string>
#include <unordered_map>
#include <fstream>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#if defined(PUFFERUI_ENABLE_SDL3)
#include <SDL3/SDL.h>
#endif

namespace pui
{
// The implementation, in order (one namespace; slices are plain text cuts).
#include "impl/core_state.inl"
#include "impl/draw_context.inl"
#include "impl/input_interact.inl"
#include "impl/layout_text.inl"
#include "impl/dock_style.inl"
#include "impl/widgets.inl"
#include "impl/components.inl"
#include "impl/chrome_scroll.inl"
#include "impl/typed_inputs.inl"
#include "impl/draw_primitives.inl"
#include "impl/sdl3.inl"
} // namespace pui

#endif // PUFFERUI_IMPLEMENTATION