# PufferUI — Boilerplate & Systems Audit

Scope: `main` at `1b9f471` (r95).
Goal (yours): keep PufferUI plain, C-level, rect-cutting, explicit IDs — and
find the repeated code and redundant systems that make it harder to maintain
and scale. **No new paradigms.** Every fix below is a small helper, a shared
struct, or a deletion. I am not proposing declarative layout, hooks, label-derived
IDs, reflection, or template machinery (the earlier React-oriented report is
superseded by this one).

**Method.** Static reading plus `grep` counts over `include/pufferui/pufferui.h`
(10,007 lines), `tests/`, `examples/`, `CMakeLists.txt`, `docs/`, `tools/`.
Counts are exact for the pattern I searched, not for "all similar code".
Nothing was compiled or changed in the source. Line estimates for savings are
my rough guesses. Findings marked **[verify]** come from reading and need a
five-minute confirmation before acting.

---

## Implementation status (r96)

Every item below was implemented in one pass and verified in debug **and**
release: `pui_core_tests`, `pui_pump_tests`, `pui_golden_tests` (goldens
**unchanged**, so the refactors are pixel-neutral), all 31 `--selftest`
examples, `clang-format` 22.1.3, `gen_api -Check`, and `pui_bench` (still 2
draw calls / 374,702 vertices; frame time equal to the pre-change commit
within noise when both are built with the same flags).

| # | Item | Result |
|---|---|---|
| 2 | `pui_gprobe` target | Confirmed: a clean configure failed. Deleted. |
| 1 | Style/override/ladder/wiring/lerp | Components take `const <name>_style *style`; 9 override structs and ~60 ladder lines deleted. `theme_lerp` now interpolates every slot (it did not before; test added). **Deviation:** `theme_apply_tokens()` was dropped — `default_dark` slot colors are not all derivable from the 10 tokens, so it would have changed the default look. `button`/`panel`/`card` keep `opt<T>` overrides. |
| 3 | Widget skeleton | `ui::consume_key`, `detail::key_activate`, `detail::widget_activate`; migrated button, checkbox, switch, radio, segmented, tabs, accordion, table. |
| 4 | Input/state duplication | `window_input` + `interaction_state` structs; `context` derives from both, so frames copy whole structs and the 8 input handlers have one body each. **Deviation:** inheritance instead of embedding, so public names (`c->focus`, `c->key_pressed[...]`) did not change. A mid-frame IME update now persists to the next frame (it was lost before). |
| 5 | Stores | `ctx_stores` (one allocation), typed `focus_list`/`blur_store` pointers, `sweep_unused`; 22 casts and the lazy `new`s for keyed stores gone. |
| 6 | Tests | `PUI_TEST` self-registration (`test_list.h` and the runner table deleted), stronger `tf_env` (installs the capture, asserts zero violations on exit, `frame()`); 23 tests and 5 frame lambdas converted — the rest convert as files are touched. |
| 7 | Examples | `ui::textf` (printf-style, format-checked on GCC/clang), 57 `snprintf`+`text` pairs converted (a few shared-buffer cases were left alone); `comp::section` + `theme::section` replace `example_section`'s literals; page-header numbers named; `sdl3_system_clipboard()` replaces three copies of the SDL clipboard bridge. **Not done:** moving example statics into a per-example state struct. |
| 8 | `gen_api` | Multi-line declarations, `namespace comp`, coverage guard; 86 → 123 entries. |
| 9 | CMake | `pui_add_library` / `pui_add_exe` / `pui_add_selftest_exe`; 284 → 201 lines, no per-target `set_property`. |
| 10 | Docs | One source per rule (AGENTS/CONTRIBUTING/design/components updated). **Not done:** marker-based README snippet splicing. |
| 11 | Header split | `pufferui.h` is 2,671 lines (declarations + includes); the implementation is 11 `impl/*.inl` slices. `tools/amalgamate.ps1` rebuilds the single header, verified byte-identical to the pre-split file and compiled as a TU. |

## 1. Ranked summary

| # | Hotspot | Evidence | Risk if left | Fix size | Savings (est.) |
|---|---|---|---|---|---|
| 1 | **A component field is written 4–5 times** (style struct, override struct, merge ladder, theme default wiring, `theme_lerp`) | 13 `*_override` structs, 65 `p.style.x.set` lines, 76-line `default_dark` slot wiring | New field forgotten in one place; already causes a likely bug (§2.1) | M | ~250–300 lines, 13 structs |
| 2 | **Fresh clones likely fail to configure** | `CMakeLists.txt:110` `pui_gprobe` → `out/probe/gprobe2.cpp`; `out/` is gitignored and untracked | CI/new contributor blocked **[verify]** | S (delete 5 lines) | — |
| 3 | **Widget interaction skeleton copy-pasted** | Enter/Space consume ×10, hover cursor ×8, focus ring ×10, `p.enabled` ×24 | Inconsistent keyboard/disabled behavior between widgets | S–M | ~80 lines |
| 4 | **Input handlers written twice + context/window state mirrored** | 8 handlers each with a "current window" branch and a "queued" branch; ~35-line copy in `begin_frame`, 8-field save/restore in `end_frame` | Every new input/field is a 3–4 place edit | M | ~150 lines |
| 5 | **Seven `void*` stores with casts, lazy `new`, manual delete, own GC** | 22 `static_cast<*_store*>`, 12 lazy `new`, hand-written teardown, two copies of the 5 s sweep | Leaks/missed cleanup on adding a store | M | ~60 lines + type safety |
| 6 | **Tests: setup repeated, helpers under-used, hand registry** | 9 private `frame` lambdas though `tf_env/tf_frame` exist; 112-line `test_list.h`; r88 found "three never-registered tests" | Tests silently not running | S–M | ~25% of test LOC |
| 7 | **Examples: formatting, statics, magic numbers** | 98 `snprintf`, ~39 `char buf[]`, 102 file statics, `example_section` hard-codes 10–38 px | Drift from the "metrics from theme" rule; verbosity | S–M | ~150 lines |
| 8 | **Generated API reference covers a fraction of the API** | `docs/api.md` has 0 hits for `slider_float`, `combo`, `dock_space`, `titlebar`, every `comp::*` except `toast_draw` | Docs claim completeness they don't have | S | — |
| 9 | **Build file repetition** | 24 `set_property(TARGET…)`, 11 `target_compile_definitions` across ~12 near-identical target blocks | Edits miss a target | S | ~80 lines |
| 10 | **Docs repeat the same rules in 4 files** | 30 matching verification/format lines across AGENTS/CONTRIBUTING/README; 39 hand-synced README `cpp` blocks | Contradictions over time | S–M | — |
| 11 | **One 10k-line header** | declarations ≈ 1–2,617, impl from 2,618 | Every item above edits the same file | M (mechanical) | — |

Do them in this order: **2 → 1 → 3 → 4 → 5 → 6 → 8/9/10 → 7 → 11.** Reasoning in §4.

---

## 2. Findings and fixes

### 2.1 One field, five places (style / override / ladder / wiring / lerp)

**What the code does.** For every component, `switch_style` declares fields, a
parallel `switch_override` declares the same fields as `opt<T>`, the component
copies them with one `if (p.style.x.set) s.x = p.style.x.value;` line each,
`default_dark()` wires theme colors into the slot, and `theme_lerp` is supposed to
interpolate. Take `track_on`: it appears at header lines 596, 606, 1041, 5784
and in use. Multiply by 13 style/override pairs and ~65 ladder lines
(`grep "\.style\.[a-z_]*\.set"`).

**Maintenance cost.** Adding one field to a component = 4 edits; forgetting
the ladder line silently ignores the user's override; forgetting the wiring
leaves a hard-coded dark default in a light theme.

**Probable existing bug [verify].** `theme_lerp` (≈ line 1083) starts with
`theme r = a;` and then lerps only top-level colors, `tokens`, and four metrics.
The component slots (`switch_ctrl`, `radio`, `tabs`, `table`, `palette`,
`button`, `panel`, …) are never lerped, so easing between a dark and a light
theme animates the page but leaves components at theme `a`. A 6-line test
confirms: lerp two themes at `t = 1`, compare `r.switch_ctrl.track_on` with `b`'s.

**Fix, in C-level terms (two small moves):**

1. **Override = the style itself, passed by pointer.** No `opt<>`, no override
   struct, no ladder.

   ```cpp
   struct switch_props { uiid id = 0; bool enabled = true; const switch_style *style = nullptr; };

   // component:
   const switch_style &s = p.style ? *p.style : u.th().switch_ctrl;

   // caller (plain C idiom: copy, edit, pass):
   switch_style mine = u.th().switch_ctrl;
   mine.track_on = u.th().tokens.danger;
   comp::switch_toggle(u, r, "Armed", on, {.id = "arm"_id, .style = &mine});
   ```

   Removes 12 override structs and ~60 ladder lines at once. The pointer only
   needs to outlive the call. Keep `opt<>` + `merge_style` **only for `button`**,
   the one widget whose theme → role → scope → instance cascade genuinely needs
   partial merging.

2. **Derive slots from tokens in one function.** Replace the 76 lines of
   per-slot wiring with:

   ```cpp
   inline void theme_apply_tokens(theme &t);   // fills every component slot from t.tokens + metrics
   // default_dark(): set palette + tokens, then theme_apply_tokens(t)
   // theme_lerp():   lerp tokens + metrics, then theme_apply_tokens(r)
   ```

   Then `theme_lerp` shrinks from ~25 lines to ~8, the drift bug disappears by
   construction, and an app that customizes a slot does so *after* the call.

**Options I considered and rejected:** X-macro field lists (one list generates
struct + merge + wiring) — classic C and compact, but they hide fields from
grep/IDE/`gen_api.ps1` and make diffs hard to read; a reflection/`tuple` merge —
exactly the clever syntax you don't want.

**Safety net.** `test_components.cpp` + goldens already cover each component;
add the one `theme_lerp` test above first (it should fail today if I'm right).

### 2.2 The `pui_gprobe` target (fresh clone breakage) **[verify]**

```cmake
# TEMP probe (delete after the r89 P7 bisection)
add_executable(pui_gprobe "out/probe/gprobe2.cpp")      # CMakeLists.txt:110
```

`out/` is in `.gitignore` and `git ls-files out` is empty, so on a clean checkout
the source does not exist and CMake should fail at generate time. It works on
your machine only because `out/probe/` exists. Delete lines 109–114. To confirm
first: `git clone` to a temp dir and run `cmake --preset x64-debug`.

### 2.3 The widget skeleton is pasted into every widget

Every interactive widget re-types the same sequence:
`interact` → hover cursor if enabled → keyboard activation (check
Enter/Space, **write `false` back into two `key_pressed` slots**) → click →
focus ring. Counts in the header: consume-Enter/Space ×10, cursor ×8,
`focus_ring(` ×10, `p.enabled` ×24, and the raw cast form
`key_pressed[static_cast<i32>(…)]` ×57.

Look at `switch_toggle` and `radio_group` side by side: the same 8 lines
appear twice, one with a local named `keyboard`, one without. That is how
subtle differences creep in (e.g. `ui::button` does not take an `enabled`
flag at all).

**Fix: one helper, plain struct return** (inside `namespace detail`):

```cpp
struct activation { interaction in; bool activated; };   // click, or Enter/Space on focus
inline activation widget_activate(ui &u, uiid id, rect area, bool enabled = true)
{
    activation a{u.interact(id, area, enabled), false};
    if (!enabled) return a;
    if (a.in.hovered) u.set_cursor(u.th().button_cursor);
    if (a.in.focused && (u.key_pressed(key::ENTER) || u.key_pressed(key::SPACE)))
    { u.consume_key(key::ENTER); u.consume_key(key::SPACE); a.activated = true; }
    if (a.in.clicked) a.activated = true;
    return a;
}
```

Plus `ui::consume_key(key)` so no component touches `ctx->key_pressed[...]`
or casts. `button`, `checkbox`, `switch_toggle`, `radio_group` rows,
`segmented`, `tab_bar`, `accordion`, `table` rows and `command_palette` rows
then start with `const activation a = widget_activate(...)`. Migrate **one widget
per commit**; the existing keyboard tests (`test_components.cpp`,
`test_widgets.cpp`) are the net. A new component's "interaction" code becomes one line,
which is the best boilerplate cut for scalability.

### 2.4 Input is handled twice; context mirrors window state

- Each of `mouse_move / mouse_button / mouse_wheel / key_event / text_input_event /
  ime_event / mods_event` is written as: *if this window's frame is open, write
  the context's mirror fields; else write `w.in`* (see `key_event(window&)`,
  `mouse_wheel(window&)`). Eight handlers, two bodies each.
- `begin_frame` then copies keys (3 arrays), text, IME, shift/ctrl and 8
  widget-state fields **into** the context (~35 lines); `end_frame` copies
  them **back** (8 fields + key arrays), and swaps `focus_store`/`blur` pointers.
- The 8 persistent widget-state fields (`active, focus, focus_request,
  dragging_panel, drag_dx, drag_dy, last_click_id, last_click_time`) are
  declared in both `window` and `context` and copied by name in two places.

**Fix (two mechanical steps):**

1. Group the 8 fields: `struct widget_state { uiid active, focus, focus_request,
   dragging_panel; f32 drag_dx, drag_dy; uiid last_click_id; f64 last_click_time; };`
   Embed one in `window` and one in `context`; `begin_frame` does `c->ws = w.ws;`
   and `end_frame` does `w.ws = c->ws;`. Adding a field is one line, copied
   automatically. (~25 lines saved, one failure class removed.)
2. Give `context` a `window_input *in` that points at the **current window's**
   queue during a frame; delete the mirror arrays and the copy block. The
   handlers always write `w.in`, so "applies to the open frame" holds
   automatically, because the frame reads the same memory. Edge clearing stays
   in `end_frame`. This removes the double branch in all eight handlers and
   ~35 lines from `begin_frame`. It touches ~118 sites that read
   `c->key_pressed[...]` etc. (a rename, best done together with the
   `consume_key` helper from §2.3 so the raw array accesses disappear
   entirely).

Risk: medium (hot path, input timing). Net: `test_input`, `test_pump`, golden
scenes, and the multi-window tests. Add one test that feeds an event *during*
an open frame and asserts the same-frame visibility the current code promises.

### 2.5 Seven stores, each hand-managed

`edit_store`, `anim_store`, `scroll_store`, `split_store`, `defer_store`,
`focus_store`, `blur_store` are each: a struct in the impl, a `void*`/pointer
in `context`/`window`, a lazy `if (!c->x) c->x = new …` (12 sites), a
`static_cast<…_store *>` at every use (22 sites; `focus_store` alone ×7), a
line in `destroy_context`, and (for blur) a separate `free_blur_store`.
`anim_store` and `scroll_store` each carry their own copy of the "unused for 5
s → erase" sweep.

**Fix (no templates required):**

```cpp
// impl section
struct ctx_stores { edit_store edits; anim_store anim; scroll_store scrolls;
                    split_store splits; defer_store defers; /* ... */ };
// declaration section: one opaque handle instead of seven void*
struct ctx_stores; ... ctx_stores *st = nullptr;     // new in create_context, delete in destroy_context
```

- No lazy allocation (empty `unordered_map`s cost nothing), no casts (the
  compiler checks types), one creation line and one deletion line.
- Per-window stores (`focus`, `blur`) get the same treatment in `struct
  window_stores`, which also removes the pointer-swap dance at the end of
  `end_frame`.
- Share the sweep: `template <class M> void sweep_unused(M &m, f64 now, f64 ttl)`
  (these maps are all `last_used` + `now`) — a 6-line helper used twice today
  and by any future keyed store.

Net: the `docs/limitations.md` item "context has public mutable fields" gets
smaller for free, and adding a store becomes a one-line struct member.

### 2.6 Tests: helpers exist but are not used

Counts: `create_context(` appears 97 times across the area files; each test
re-creates `null_device`, installs `capture_violation`, zeroes
`g_violation_events`, defines its own `frame` lambda (9 in
`test_components.cpp`, 4 elsewhere), ends with
`CHECK(violation_count(c) == 0); destroy_context(c);`. A `tf_env` RAII helper and
`tf_frame/tf_click/tf_key` already exist in `test_util.h` but only
`test_text.cpp` uses them (48 uses; none elsewhere).

Registration is manual in three places per test: define, declare in
`test_list.h` (112 lines), add to the list. r88's commit message says the split
found "three never-registered tests" — the failure mode has already happened once.

**Fix:**

- Make `tf_env` the default: constructor creates the context, installs the
  capture handler, resets counters; destructor **asserts zero violations
  unless the test opted out** (`env.expect_violations()`), then destroys. Add
  `env.frame(t, fn)`, `env.click(x, y, fn)`, `env.key(k, fn)` (thin wrappers over what
  `tf_*` do). A typical component test shrinks by ~10 lines.
- Replace the manual list with a self-registering macro (a static registrar
  struct is plain C++98):
  `TEST(switch_toggle) { tf_env env; ... }`. `--list/--filter` already exist; they just
  read the registry. Impossible to forget a test, and `test_list.h` is deleted.
- Convert area files opportunistically (touch only when editing); don't
  mass-rewrite — your own `AGENTS.md` warns against scripted full-file rewrites.

### 2.7 Examples: formatting, statics, magic numbers

- **Formatting boilerplate:** 98 `snprintf` calls and ~39 `char buf[N]` locals
  just to draw a number. A tiny C-style helper fits your rules:
  `u.textf(rect, color, align, "clicks: %d", n);` (printf-format, stack buffer
  inside, `__attribute__((format))`/`_Printf_format_string_` for checking). One
  declaration; each call site loses 2–3 lines.
- **Statics:** 102 file-scope statics (`g_wifi`, `static i32 tab`) because every
  frame callback is a free function. Acceptable for tiny demos, but they are
  copy-paste fodder. Give `example_app` a `void *user` (or have `example_run`
  take a state pointer) so an example owns a `struct state` like
  `pui_counter.cpp` does — it already demonstrates the pattern.
- **Magic numbers contradict your own rule.** `AGENTS.md`: "examples and
  components never hard-code 26–30px row heights." Yet `example_section` and
  `example_begin_page` in `example_common.h` contain 38, 24, 20, 16, 10, 34,
  14, 6, 3 px literals, and every example inherits them. Move these into theme
  metrics (`section_head_h`, `section_gap`, `page_pad`) or a small
  `layout_metrics` struct next to `control_h`.
- **They are already components.** `example_section` is called 58 times and
  `example_begin_page` 25 times. Promote them into `comp::` (e.g. `comp::section`,
  `comp::page`) following your convention; examples and the tour share one
  implementation and the headline widget set gets two useful items.
- `example_clipboard` wraps SDL's clipboard in the example header. The SDL3
  glue in the library is the natural owner (it already installs the window host);
  moving it means every app gets working copy/paste with `sdl3_app_init`.

### 2.8 Generated API reference is incomplete

`tools/gen_api.ps1` matches only single-line declarations that start with a
fixed list of return types. Result: `docs/api.md` has 86 bullets and **zero**
mentions of `slider_float`, `combo`, `dock_space`, `titlebar`,
`split_horizontal_interactive`, `measure_text`, `switch_toggle`, `radio_group`,
`tab_bar`, `table`, or `command_palette` (checked with `grep -c`). It states
"Every declaration here is public," which is true, but a reader will assume the
reverse. The CI drift check only proves the file matches the (incomplete)
generator output.

Fix: join continuation lines until the `;`, accept any return type, include
`namespace comp`, and **fail the check if a declared `ui::`/`comp::` function
isn't listed** (a count comparison is enough). Keep it a plain script, as it is.
Also stop duplicating signatures by hand in `docs/components.md` (props tables
are copied from the header); generate those rows or link to the generated
reference.

### 2.9 CMake: eleven copies of the same target

Each example/test target repeats `add_executable`, `target_compile_definitions
(PUFFERUI_ASSET_DIR=…)`, `target_link_libraries(... pufferui_warnings)`, and two
`set_property(CXX_STANDARD…)` lines (24 `set_property`, 11
`target_compile_definitions`). Fix with one function:

```cmake
function(pui_add_exe name)           # pui_add_exe(pui_bench tests/bench.cpp LINK pufferui)
  cmake_parse_arguments(A "" "" "SOURCES;LINK" ${ARGN})
  add_executable(${name} ${A_SOURCES})
  target_link_libraries(${name} PRIVATE ${A_LINK} pufferui_warnings)
  target_compile_definitions(${name} PRIVATE PUFFERUI_ASSET_DIR="${PUFFERUI_ASSET_DIR}")
  set_target_properties(${name} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
endfunction()
```

Set `CMAKE_CXX_STANDARD 20` once at the top and the property lines disappear
everywhere (including the two library targets). The two library targets
(`pufferui`, `pufferui_sdl3`) also duplicate include/standard blocks; a second
small function removes that. Also: the example list has an unindented
`overflow` entry (cosmetic, but a sign the list is hand-edited).

### 2.10 Docs say the same thing in several places

The verification checklist (build, run tests, goldens, clang-format) appears in
`AGENTS.md`, `CONTRIBUTING.md` and the README (30 matching lines).
`AGENTS.md` and `docs/design.md` still point at a plan file in
`~/.opencode/plan/` that contributors cannot see (same issue I raised in the
first report; `design.md` now repeats it). README holds 39 `cpp` blocks copied by
hand from examples, with the rule "update the snippet in the same change."

Fix: one source of truth per rule (CONTRIBUTING owns the checklist; AGENTS links
to it); mark snippets in examples (`// [snippet:ids_scope]` … `// [/snippet]`) and
let a script (same style as `gen_api.ps1`) splice them into README and fail CI on
drift — the tooling pattern already exists in the repo.

### 2.11 One header, many reasons to touch it

Everything above edits `pufferui.h`. Split along the existing banner
comments (`rect algebra`, `animation`, `theme`, `rendering`, `windows`,
`interaction`, `text`, `widgets`, `components`, `sdl3`), keeping one declaration
header for users and generating the single-header distribution — the deferral in
`docs/design.md` is reasonable *now*, but once §2.1–§2.5 land the file shrinks
by an estimated ~600 lines and the split becomes cheaper and safer. Do it after
those, not before.

---

## 3. What I would not change

- Rect cutting, `column/row/track_*`, `region`/`scope`, explicit IDs, no smart
  pointers, `lower_snake_case`, `namespace pui` — all consistent and not
  boilerplate; they are the design.
- The `render_device` contract: the virtual defaults already keep backends short.
- `PUFFERUI_CHECK`/violation codes: small macros, high value.
- The `context*` convenience input overloads (8 one-liners): they are thin
  wrappers and cheap; leave them.
- The generated `docs/api.md` approach: right idea, just complete it (§2.8).

---

## 4. Order of work and safety

| Step | Change | Why this order |
|---|---|---|
| 1 | Delete `pui_gprobe` (2.2) | unblocks clean clones/CI, 5 lines |
| 2 | Add the `theme_lerp` test; build `theme_apply_tokens` (2.1b) | proves/fixes the likely bug; shrinks `default_dark` |
| 3 | `widget_activate` + `consume_key`, migrate one widget per commit (2.3) | removes the copy-paste *before* more components appear |
| 4 | Style-by-pointer for the 12 non-button components (2.1a) | mechanical once the component bodies are shorter |
| 5 | `widget_state` struct (2.4 step 1), then `window_input *in` (step 2) | isolates the risky change from the easy one |
| 6 | `ctx_stores` / `window_stores` + `sweep_unused` (2.5) | typed, one-line teardown |
| 7 | Test `tf_env` + `TEST()` registrar; convert as files are touched (2.6) | prevents silent un-run tests |
| 8 | Complete `gen_api` + snippet splicing + dedupe docs (2.8, 2.10) | docs stop drifting |
| 9 | CMake function (2.9), `textf` + `comp::section/page` (2.7) | cosmetic/DX |
| 10 | Header split (2.11) | now cheap |

Guardrails: one commit per step; each must leave `pui_core_tests`,
`pui_pump_tests`, `pui_golden_tests` (goldens **unchanged**: these are
behavior-neutral refactors, so a changed golden means a bug) and
`examples_selftest` green, and `pui_bench` draw calls/vertices unchanged
(`docs/perf.md` baseline: 2 calls, 374,702 vertices). Measure line counts before
and after (`git ls-files | xargs wc -l`) and record them in the commit message so
the "boilerplate budget" is visible.

**A rule worth adding to `AGENTS.md`:** *a field, a store, an input, or a test must
be addable by editing one place.* If a change requires touching the same name in
more than two spots, the system needs a shared struct or helper first.

---

## 5. Appendix — counts (reproducible)

| Pattern | Count | Command (from repo root) |
|---|---|---|
| `if (o.x.set)` ladder lines (core styles) | 25 | `grep -c "if (o\.[a-z_]*\.set)" include/pufferui/pufferui.h` |
| `p.style.x.set` lines (components) | 65 | `grep -c "\.style\.[a-z_]*\.set"` |
| `*_style / *_override / *_props / *_result` structs | 41 | `grep -c "^struct [a-z_]*_\(style\|override\|props\|result\)"` |
| Lazy `new` store creation | 12 | `grep -c "if (!c->[a-z_]*) c->[a-z_]* = new"` (+ window variants) |
| `static_cast<*_store *>` | 22 | `grep -c "static_cast<[a-z_]*_store \*>"` |
| Enter/Space consumed by hand | 10 | `grep -c "key_pressed\[static_cast<i32>(key::ENTER)\] = false"` |
| `focus_ring(` call sites | 10 | `grep -c "focus_ring("` |
| Raw `key_pressed[...]` accesses | 61 (57 via `c->`/`ctx->`) | `grep -c "key_pressed\["` |
| `static_cast` in the header | 356 | `grep -c static_cast` |
| `create_context(` in tests | 97 | `grep -c "create_context(" tests/*.cpp` |
| Test files using `tf_env/tf_frame` | 1 of 7 (48 uses) | `grep -c "tf_frame\|tf_env" tests/*.cpp` |
| `snprintf` in examples | 98 | `grep -c snprintf examples/atomic/*.cpp examples/*.cpp` |
| File-scope `g_` statics in atomic examples | 102 | `grep -c "^static .* g_"` |
| `example_section(` / `example_begin_page(` calls | 58 / 25 | `grep -c …` |
| `set_property(TARGET` / `target_compile_definitions` in CMake | 24 / 11 | `grep -c …` |
| `docs/api.md` bullets | 86 | `grep -c "^- " docs/api.md` |
