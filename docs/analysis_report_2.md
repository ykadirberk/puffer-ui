# PufferUI — Second Analysis: "Can this replace React?"

Scope: `main` at commit `1b9f471` (r95). Previous report: `docs/analysis_report.md`
(r86).

**How I checked.** I re-read the public declarations, the docs added since
r86 (`design`, `limitations`, `perf`, `seams`, `components`, generated `api`),
the git history of r87–r95, and the examples a beginner would copy from. I also
**ran the prebuilt release binaries** in `out/build/x64-release/Release/`
(built minutes before the last commit; I did not rebuild):

| Check | Result |
|---|---|
| `pui_core_tests` | `all core tests passed` |
| `pui_pump_tests` | `all pump tests passed` |
| `pui_golden_tests` | `18 scene(s), 0 failure(s)` |
| `pui_bench --frames 300` (2,000 buttons + 500 text rows + 50 panels, null device) | `avg=4.672ms worst=6.307ms draw_calls=2 vertices=374702` |

I did **not** verify the Linux CI jobs (I can't see their results), did not run
the GUI examples interactively, and did not test on a HiDPI display. Items marked
*[hyp]* are reasoning from the code, not measurements. Effort estimates are my
rough guesses.

---

## 1. Verdict in five lines

1. **The engineering of the core got much better.** Nearly everything I flagged
   as a hot spot or safety issue in r86 was fixed or deliberately deferred with
   a documented reason (§3). Draw calls on the benchmark went 4,006 → 2.
2. **As a C++ immediate-mode toolkit it is now solid.** As a **React replacement
   for a beginner JavaScript programmer it is not close yet**, and the reason
   is structural, not polish: *every widget call needs a rect you computed by
   hand and an ID you invented.* In React you write `<button>Add</button>`.
3. The good news: **immediate mode is conceptually simpler than React** (no
   `setState`, no hooks rules, no stale closures, no dependency arrays — a
   function runs, state is plain variables). The missing piece is an **easy
   layer** on top of the rect-cutting substrate, not a rewrite.
4. A beginner **JS** programmer will not become productive in C++ (build tools,
   compile errors, lifetimes) however good the API is. Matching that goal means
   **embedding JavaScript (e.g. QuickJS) with hot reload** on top of the easy
   layer. §6 lays out the stack and order.
5. **Be clear about what "replace React" means.** For desktop tools, kiosks,
   in-game/embedded UI, internal apps: realistic. For public websites and
   anything needing screen readers, SEO, the npm ecosystem or the DOM: not
   realistic with this architecture (§7).

### Scorecard (r86 → r95)

| Axis | r86 | r95 | Comment |
|---|---|---|---|
| Correctness / safety nets | A− | **A** | thread-local context, fatal asserts honored, input overflow reported, widget-ID duplicate detection |
| Developer experience (contributors) | B | **A−** | static lib, test runner + `--filter`, Linux CI, API reference generated and drift-checked |
| Ease of use for a C++ dev | B− | **B** | options structs, metrics, `scope`; still rect + id everywhere |
| Ease of use for a JS beginner | — | **D** | see §4 |
| Performance | C+ | **B+** | measured; draw-call problem solved; idle sleep exists but is opt-in |
| Flexibility | B | **B** | token layer + components added; still no shaping, images, multiline, DPI |
| Scalability | C | **B−** | growable overlay stacks, hash-set ID check; 10k-line single header, fixed track/dock limits remain |

---

## 2. Current state (what exists now)

- **Size.** `pufferui.h` is **10,007 lines** (declarations ≈ lines 1–2,617,
  implementation from 2,618). The r94 split into header + several `.cpp` files
  was explicitly deferred (`docs/design.md`).
- **Core.** Rect-cutting layout, `column/row/track_*/auto_fit_grid`; `uiid`
  hashing with `region` and the new `ui::scope(key)` / `id_scope`; interaction
  with topmost-wins press claims; animation (`tween`, `spring`, `smooth`,
  `appear`, `animate_color`); popups/panels/docking/multi-window; stb_truetype
  text with a layout cache; violation system with codes.
- **Theme.** Colors + control metrics (`control_h`, `control_h_small`,
  `icon_size`) + semantic `tokens` + per-component slots; `theme_lerp` eases
  between two themes.
- **Component layer (`pui::comp`).** `switch_toggle`, `radio_group`, `segmented`,
  `tab_bar`, `accordion_scope`, `drawer_scope`, `toast_host/toast_draw`, `table`
  (virtualized), `command_palette`. All follow one written convention
  (`docs/components.md`): `props` struct with `.id`, result struct, theme slot,
  keyboard + cursor, no file statics.
- **Verification.** Tests split by area, draw-list snapshot tests, golden
  images, `--selftest` on every example, `pui_bench` with a recorded history.
- **Docs.** README tutorial (1,461 lines), `AGENTS.md`, `CONTRIBUTING.md`,
  generated `docs/api.md`, design/limitations/perf/seams.

---

## 3. Status of the r86 findings

| r86 finding | Now | Notes |
|---|---|---|
| C1 context slot not thread-local | **Fixed** | `thread_local`, test added |
| C3 dangling deferred-menu pointers | **Fixed** | labels copied into context storage |
| C4 128-byte text input truncation | **Fixed** | growable buffer + `VIOL_INPUT_OVERFLOW` |
| C5 glyph-key aliasing | **Fixed** | struct key |
| C6 `PUFFERUI_ASSERT` not fatal | **Fixed** | |
| C7 `/fp:fast` vs goldens | **Fixed** | removed |
| C2 public mutable `context` | **Open** | documented as unstable |
| C8 stable window ids | **Open** | designed in `seams.md` |
| P1 batch breaks (solid vs text) | **Fixed, measured** | white texel; 4,006 → 2 calls |
| P2 text re-measured every call | **Fixed** | shared layout cache; −22% frame cost |
| P3/P4/P5 ellipsis O(n²), ID scan O(n²), per-call vectors | **Fixed** | linear, hash set, scratch buffers |
| P7 SDL vertex copy | **Fixed** | strided `RenderGeometryRaw` |
| P8 culling | **Fixed** | draw-list culling + `ui::is_visible` |
| Idle spinning | **Partly** | `needs_redraw` + `wait_when_idle`, **off by default** |
| P9 glyph atlas never evicts | **Open** | |
| D1 impl compiled ~38× | **Fixed** | static library targets |
| D3 Windows-only CI | **Fixed on paper** | gcc+clang job present; results not seen by me |
| D4/D5/D10 external docs, one big test file, no contributing guide | **Fixed** | |
| D2 header/impl split | **Deferred** | now 10k lines in one header |
| D7 baked asset path | **Open** | |
| E1 label-derived IDs | **Rejected by decision** | revisit — see §5.1 |
| E3 options structs | **Partly** | `button_opts`, `panel_opts`, comp props; core widgets still positional |
| E5 widget set | **Mostly done** | 9 components promoted |
| DPI, shaping, a11y, images, multiline | **Open** | seams designed only |

One piece of process praise: deferrals are written down with reasons
(`design.md`, `limitations.md`) rather than silently dropped. Keep that.

---

## 4. The gap that matters: authoring experience for a beginner

### 4.1 Same app, three ways

**React (what the target user knows):**

```jsx
function Todos() {
  const [todos, setTodos] = useState([]);
  const [draft, setDraft] = useState("");
  return (
    <div className="col">
      <input value={draft} onChange={e => setDraft(e.target.value)} placeholder="What needs doing?" />
      <button onClick={() => { setTodos([...todos, {id: nextId++, title: draft, done: false}]); setDraft(""); }}>Add</button>
      {todos.map(t => (
        <label key={t.id}>
          <input type="checkbox" checked={t.done} onChange={() => toggle(t.id)} /> {t.title}
        </label>
      ))}
    </div>
  );
}
```

**PufferUI today (what the same screen takes, written the way the examples do it):**

```cpp
struct todo { int id; std::string title; bool done = false; };
static std::vector<todo> g_todos; static std::string g_draft; static int g_next = 1;

static void todos_frame(ui &u, example_app &app)
{
    example_page page = example_begin_page(u, app, "Todos", "");
    column col(page.content, example_ui::SECTION_GAP);
    {
        row r(col.next(u.control_h()), u.spacing());
        u.text_field(r.next(300.0f), g_draft, "draft"_id);          // no placeholder support
        if (u.button(r.next(u.button_size("Add").x), "Add", "add"_id) && !g_draft.empty())
        { g_todos.push_back({g_next++, g_draft}); g_draft.clear(); }
    }
    rect list = col.next(300.0f);                                    // you pick the height
    scroll_view sv = u.scroll(list, "list"_id);
    column items(sv.content(), 4.0f);
    for (todo &t : g_todos)
    {
        id_scope s(u, static_cast<uiid>(t.id));                      // or duplicate-ID violation
        u.checkbox(items.next(26.0f), t.title, t.done, u.local("done"));
    }
    sv.set_content_height(g_todos.size() * 30.0f);                   // you compute it
}
```

**What a beginner should be able to write (a design target, not existing code):**

```cpp
ez::app("Todos", 480, 640, [] {
    static state<std::vector<todo>> todos;   // or plain statics
    static std::string draft;
    vstack({.gap = 8, .pad = 16}, [&] {
        hstack([&] {
            input(draft, {.placeholder = "What needs doing?", .grow = 1});
            if (button("Add")) { todos->push_back({next_id++, draft}); draft.clear(); }
        });
        scroll({.grow = 1}, [&] {
            for (auto &t : *todos) key(t.id, [&] { checkbox(t.title, t.done); });
        });
    });
});
```

(and the same thing in JS, §6, which is the actual end goal.)

### 4.2 Why today's version is hard for a beginner

| # | Barrier | Evidence | Effect on a JS beginner |
|---|---|---|---|
| B1 | **Every widget takes a `rect` you must have already cut** | `button(rect, label, id, …)` in `docs/api.md`; every example uses `col.next(26.0f)` | They must learn rect algebra before they can show a button. React has no equivalent concept. |
| B2 | **Every widget takes a manual `uiid`** (`"add"_id`) | components: `.id` "required: identity is explicit" | Forgetting it is not obvious; copy/pasting an ID creates shared state. The r90 duplicate detection (`VIOL_DUP_WIDGET_ID`) catches it only at runtime, in debug, and only if rects differ. I did not check what happens if a component's `.id` is left `0`. |
| B3 | **No content-sized layout** | `column::next(height)` takes pixels; `button_size()`/`text_size()` exist but you call them yourself | Magic numbers everywhere; resizing the window or changing a font breaks layouts. |
| B4 | **Scroll needs `set_content_height`** | `scroll_view` doc + examples | A React dev expects `overflow: auto`. |
| B5 | **No flex/grow/align/justify/wrap** | tracks and `auto_fit_grid` only | "Right-align this", "fill the rest", "center vertically" each need manual cutting. |
| B6 | **State lives in `static` globals in examples** | `g_wifi`, `static i32 tab` throughout | Models a bad habit; no local component state primitive (`useState` equivalent). |
| B7 | **Errors point into the library** | `PUFFERUI_CHECK` expands `__FILE__`/`__LINE__` in the header, so the report names `pufferui.h:NNNN`, not your call site | Beginner can't find their mistake. C++20 `std::source_location` at widget entry points fixes this. |
| B8 | **Violations stop the debugger in debug builds** | `break_on_violation` | Fine for a C++ dev with a debugger; a beginner sees a crash/trap. A visible in-window error panel is friendlier. |
| B9 | **Setup is heavy** | MSVC + "Developer Prompt" + presets + SDL3 submodule build (README) | No "npm create / npm run dev" moment. |
| B10 | **Missing building blocks** | no placeholder/password/multiline text, no `slider_int`, no image loading (only atlas `skin_image`), no icons, no radio/checkbox groups beyond the 9 components | Real apps hit these in the first hour. |
| B11 | **Styling is C++ structs spread over theme slots** | `button_override`, `switch_override`, `theme::tabs`… | Not CSS-shaped; no inheritance, no pseudo-states in one declaration. |
| B12 | **Two ways to do many things** | positional vs options overloads; `rect` vs components | Beginners need one obvious way. |

### 4.3 React concepts → PufferUI today

| React | PufferUI today | Gap |
|---|---|---|
| Component = function returning UI | A function that draws, called every frame | Same idea; **simpler** (no reconciliation) |
| `useState` | App variables / `static`s | No keyed local state primitive |
| `useEffect`, `useMemo`, `useRef` | none (`update_view()` convention) | Need `once`, `on_change`, `memo`, `ref` equivalents |
| Props | function params / `*_props` structs | Good (designated initializers) |
| `key` on list items | `ui::scope(key)` / `id_scope` | Works; needs a friendlier spelling (`key(id, …)`) |
| Context / theming | `theme`, `style_scope` | Good for style; no general context |
| Conditional rendering | `if` | Native |
| Flexbox / CSS | rect-cutting, tracks, grid | **Largest gap** |
| CSS classes, hover/focus states | `button_override`, theme roles | Declarative styling gap |
| Events (`onClick`) | `if (button(...))` | Equivalent, arguably simpler |
| Forms | `text_field`, `number_field`, `checkbox`, `slider_float`, `combo` | No placeholder, validation helpers, multiline, int slider |
| Router | none | Needs a `screen` stack helper (small) |
| Data fetching / async | none | Needs task/future integration with frame loop |
| Animation (framer-motion/react-spring) | `tween`, `spring`, `appear`, `animate_color` | **Strength** — comparable |
| DevTools | violation overlay, golden/snapshot tests | No inspector (layout bounds, ID tree, state) |
| Hot reload | none | **Biggest DX gap for a React user** |
| i18n / RTL | none (no shaping/BiDi) | Blocker for many markets |
| Accessibility | none | Blocker for some products/regulations |
| HiDPI | not handled in metrics (`seams.md` §1) | Likely blocker on modern laptops *[hyp: verify on a 150–200 % display]* |
| Images / SVG / icons | none | Blocker for typical UIs |
| npm ecosystem | none | Cannot be replicated |

---

## 5. Recommendations for ease of use (C++ API)

Goal: a first-time user shows a styled, interactive, responsive screen without
computing a single pixel.

### 5.1 Tier 1 — the "flow" API (implicit layout + default IDs)

Smallest change with the largest payoff. Keep rect-cutting as the escape
hatch; add an implicit layout stack to `ui`:

```cpp
u.vstack({.gap = 8, .pad = 16}, [&] {      // pushes an implicit column
    u.text("Settings", {.size = 24});       // takes content size
    if (u.button("Save")) save();           // rect from the cursor, id from label+scope
    u.checkbox("Dark mode", s.dark);
});
```

- **Implicit cursor stack** (`vstack`/`hstack`/`zstack`/`grid`) with
  content-sized children (`text_size`, `button_size` already exist).
- **Label-derived IDs by default**, with the explicit `{.id=…}` overload kept
  for icons/dynamic labels. The reason I recommended this in r86 and you
  rejected it was silent shared state. That risk is now mitigated by the
  `VIOL_DUP_WIDGET_ID` check you added; extend it to run for *same id, same
  rect, same frame* too (two identical labels in one scope) and have the
  message say "give these widgets distinct labels or wrap them in `key(...)`".
  Also `key(x, …)` instead of `id_scope s(u, static_cast<uiid>(t.id))`.
- **Auto-scroll**: `u.scroll({.grow=1}, [&]{…})` measures the cursor's consumed
  height itself (you already judge overflow from the previous frame's height —
  reuse that, don't make the user call `set_content_height`).
- **Text-first widgets**: `text(str)`, `text(str, style)`, `heading`, `muted`,
  `link`, `label`, `badge`, `icon`; `input` with `placeholder`, `password`,
  `multiline`; `slider(int|float)`; `select(span<string_view>)` replacing
  `const char* const*` + count.
- **One call shape**: `widget(label_or_value, options{})` with C++20 designated
  initializers; drop the positional overloads from the docs.

*Effort ≈ 2–3 weeks. Requires zero solver work; the one-frame-lag trick the
core already uses for overlays handles "right-aligned"/"fill the rest".*

### 5.2 Tier 2 — a real flexbox layout

React users think in flexbox. Options:

| Option | Pros | Cons |
|---|---|---|
| **Embed Yoga** (the C++ flexbox engine behind React Native — from memory; verify license/API before committing) | Exact CSS-flex semantics, mature, familiar mental model | Needs a per-frame node tree; second layout model beside rect-cutting |
| **Clay-style**: record a per-frame element tree, solve at scope close, then replay draw/interact | Stays immediate-mode and allocation-light; one layout model | You write and own the solver |
| **Extend `track_*` with grow/shrink/wrap/align/justify** | Smallest step; reuses current code | Not truly nested/auto-sized without the tree |

Recommended: **record-then-solve for the easy layer** (needed anyway for
`measure-then-place`, wrapped text, `align-items: center`, `justify-content:
space-between`, `flex-wrap`, `min/max-width`, percentages), implemented behind
the Tier 1 API so users never see it. Hit-testing and `interact()` use the
previous frame's solved rects (already how overlays work); document the
one-frame lag and make the first frame run twice at startup/resize to hide it.

*Effort ≈ 4–6 weeks including tests (snapshot tests make this cheap to
verify).*

### 5.3 Tier 3 — style as data, not code in the widget

- A `style` struct with **inherited** properties (`color`, `font`, `size`) and
  non-inherited (`bg`, `radius`, `border`, `pad`, `gap`, `grow`, `align`…).
- **State variants in one declaration**: `{.bg = X, .hover = {.bg = Y}, .active
  = {...}, .disabled = {...}, .focus = {...}}`. Today each widget hard-codes its
  hover/active mapping.
- **Named classes** (`u.define("primary", {...})`; `u.button("Save", {.class_="primary"})`)
  — generalizes `button_role`/`switch_ctrl` into one mechanism for every
  widget and removes per-component theme slots (the `comp::` styles can become
  default class definitions).
- **Responsive**: `u.width()`/`u.breakpoint()` helpers; media-query-like
  `style.when(width < 600, {...})`.

### 5.4 Tier 4 — React-like state and lifecycle helpers

Immediate mode already removes most of React's footguns, so keep these tiny:

```cpp
auto &count = u.state<int>("count", 0);   // keyed by current scope; lives while it's shown
u.once("load", [&]{ start_fetch(); });     // first frame this scope appears
u.on_change("q", query, [&]{ refilter(); });// runs when the value changes
u.every(1.0f, [&]{ poll(); });
auto &task = u.async("users", []{ return fetch_users(); });  // task.loading / task.value / task.error
```

State is garbage-collected when its scope stops being drawn (the animation
store already does 5-second disuse collection — same mechanism). Add a
`screen` stack (`u.push_screen`, `u.pop_screen`) for navigation.

### 5.5 Friendly errors and tooling (cheap, high impact for beginners)

- Capture **caller** location with `std::source_location` default arguments on
  public widget functions so violations say `main.cpp:42` (B7).
- **In-window error panel** with the violation text, the likely fix, and a
  "copy" button; keep the debugger break as an opt-in.
- **Inspector overlay** (toggle with F12): hover shows rect, ID, scope path, style
  that applied, and whether a hit rect is being stolen by another (the #1
  documented trap). Layout-bounds toggle like browser devtools.
- **`pui new myapp`** scaffold + a single `FetchContent` line; prebuilt SDL3
  so a first window needs no VS Developer Prompt (B9). `pui run` rebuilds and
  relaunches on save (a poor-man's hot reload, until §6).
- **Beginner docs track**: a 10-minute "build a todo app" page in JS and C++
  side by side; keep the long README as reference only.

### 5.6 Remaining core gaps that block real apps

| Gap | Why it matters | Effort |
|---|---|---|
| **HiDPI / `window::scale`** (`seams.md` §1) | On 125–200 % displays text and metrics are wrong or blurry *[hyp]* | M |
| **Images** (stb_image or SDL_image, atlas packing, `image()` widget, fit modes) and **icons/SVG** (nanosvg or icon font) | Nearly every UI | M |
| **Multiline / rich text input**, undo, selection, placeholder, password | Forms, notes, chat | M–L |
| **Shaping + BiDi + font fallback** (HarfBuzz behind the designed `text_shaper` seam) | Arabic/Hebrew/Indic and emoji; ligatures; GPOS kerning | L |
| **Accessibility** (AccessKit-style tree or UIA/NSAccessibility adapters; seam `u.a11y`) | Legal/market requirement for many products | L |
| **Clipboard, file dialogs, drag-and-drop files, window icon/tray, notifications** | Desktop-app basics | S–M each |
| **Atlas eviction** (P9) | Long-running apps with many sizes/fonts | S |
| **Stable window ids, narrow `context`** (C8/C2) | Public API stability before 1.0 | M |
| **Header/impl split + amalgamation** | 10k-line single header hurts IDE responsiveness and review | M (mechanical) |

---

## 6. The path to a beginner-friendly JavaScript experience

A C++ API, however friendly, does not meet "a beginner JavaScript programmer
can use it." The stack that does:

```
 JS app code (ES modules)            <- what the beginner writes
   │  declarative builder or JSX-like helper, hooks-lite state
   ▼
 QuickJS (or another embeddable JS engine)   <- ~hundreds of KB, ES2020+
   │  bindings: ez:: easy layer (Tier 1–4)
   ▼
 PufferUI core (rect cutting, draw list, text, anim)
   ▼
 SDL3 / your own backend
```

```js
// app.js — the end goal
import { app, vstack, hstack, text, input, button, checkbox, scroll, state } from "pufferui";

app("Todos", () => {
  const [todos, setTodos] = state([]);
  const [draft, setDraft] = state("");
  vstack({ gap: 8, pad: 16 }, () => {
    hstack(() => {
      input(draft, setDraft, { placeholder: "What needs doing?", grow: 1 });
      if (button("Add")) { setTodos([...todos, { id: Date.now(), title: draft, done: false }]); setDraft(""); }
    });
    scroll({ grow: 1 }, () =>
      todos.forEach(t => key(t.id, () => checkbox(t.title, t.done, v => (t.done = v)))));
  });
});
```

Design choices to make it work:

- **Immediate-mode in JS, not a virtual DOM.** The function runs each frame
  that needs a redraw; no diffing, no reconciliation. This keeps the engine
  tiny and removes React's hardest concepts.
- **Command-buffer bridge.** To avoid thousands of JS→native calls per frame, JS
  emits a compact command buffer (typed array: op codes + string table) and the
  native side replays it; measure before optimizing *[hyp: per-call QuickJS
  overhead is likely acceptable for a few hundred widgets and visible past a few
  thousand; benchmark with the existing `pui_bench` scene ported to JS]*.
- **Redraw only on change.** Make `wait_when_idle` the default for the easy
  layer, so an idle JS app uses ~0 % CPU (React apps in a browser get this
  for free).
- **Hot reload**: re-evaluate the module on file change, keep `state()` values
  keyed by call-site/scope path (like React Fast Refresh), show errors in the
  in-window error panel (§5.5).
- **Async**: `fetch`-like promise API backed by a native HTTP client on a worker
  thread (`u.async` from §5.4); event loop integrated with the frame loop.
- **Security/sandbox**: expose a small, deliberate host API; no raw FS/network
  unless opted in.
- **Packaging**: one `pufferui` executable that runs `app.js` (like `node app.js`),
  and `pufferui build` that bundles script + assets + runtime into a single exe.

Alternatives, honestly weighed:

| Option | When it's right |
|---|---|
| **QuickJS binding (above)** | Goal is literally "JS beginners build native UIs." |
| **Lua binding** | Smaller, faster, easier to embed — but not JavaScript. |
| **Keep C++ only + Tier 1–4** | Your team is writing C++; beginners are not the audience. |
| **Compile to WebAssembly/canvas** (Emscripten + SDL3) | You want the same UI in a browser; accept canvas-rendered UI (no DOM, no native accessibility, no SEO, bigger downloads, own IME/selection handling). |
| **Keep React for web, PufferUI for desktop/embedded** | Most realistic split today. |

*Effort ≈ 8–12 weeks for a usable QuickJS runtime with hot reload and the
Tier 1 API, after Tier 1–2 exist. Do not start it before the easy layer is
stable — the JS API is a thin skin over it.*

---

## 7. Replacing React: what you gain, what you lose

**Gains.** Native frame-accurate rendering and input; deterministic,
headless-testable UI (`null_device`, snapshots, goldens — better than most web
UI test stacks); very small runtime; spring/tween animation first-class; no
bundler/transpiler/dependency churn; the rect-cutting escape hatch for custom
visuals (React needs canvas/WebGL for that); low latency.

**Losses (must be planned for):**

| React/web provides | You must build or accept losing |
|---|---|
| Accessibility tree, screen readers, OS text services | A11y adapter (§5.6) — until then, not suitable for products requiring it |
| SEO, links, deep URLs, browser history/tabs | Not applicable to native; an argument against replacing React for public sites |
| npm packages (charts, editors, markdown, auth SDKs, payments) | Write or bind native equivalents; no drop-in |
| CSS (grid, flex, media queries, transitions, filters) | Tier 2–3 gets you ~80 % of day-to-day CSS, not all |
| Rich text/contenteditable, selection, spellcheck, translation tools | Your own multiline/rich text; no spellcheck |
| HiDPI, zoom, font scaling, dark-mode sync | `window::scale` + OS theme hook |
| DevTools (Elements, Network, React Profiler), HMR | Inspector overlay + hot reload (§5.5/§6) |
| Cross-platform with zero install (the browser) | Per-OS builds, installers, updaters, code signing |
| Massive hiring pool & Stack Overflow answers | Documentation + examples are your support channel |

Practical recommendation: **pick one real product screen set** (e.g. a
settings + list + form + modal app) and make the Tier 1 API able to express it
in under ~40 lines per screen with zero rect math. That app is the acceptance
test; everything below follows from it.

---

## 8. Performance (re-assessed)

**Measured today** (release, this machine, core only, no GPU): 2 draw calls,
374,702 vertices, **4.67 ms avg / 6.31 ms worst** for ~2,550 widgets plus
panels. Typical app screens (50–300 widgets) are therefore a small fraction of a
millisecond of CPU in the core *[hyp, extrapolated]*; this is fine for a React
replacement on the CPU side.

Remaining work, in priority order:

1. **Default to idle-sleep** in the easy layer/bootstrap (`wait_when_idle` is
   currently `false`). Without it a static window burns a core redrawing
   identical frames — the first thing a React user will notice on a laptop.
2. **Retained layers for static subtrees** (cache a region's vertex range
   keyed by an app-supplied version or hash; replay it). Today every frame
   re-emits all geometry (the 374k vertices above); caching makes big static
   UIs nearly free and lowers GPU upload.
3. **Dirty tracking / partial redraw** for animations that touch a small area.
4. **Text**: pre-rasterized per-size glyph pages are fine; add atlas eviction
   (P9), subpixel positioning, and an SDF option for crisp scaling/HiDPI.
5. **Rounded rect/ring geometry**: still tessellated each frame; cache arc
   tables or add a shader path through a `material` hook (`seams.md` §7) when a
   profile of a real app shows it matters. I did not profile this.
6. **JS bridge cost** (if §6 is built): benchmark early; batch with a command
   buffer.
7. **Memory/latency**: add frame-time and allocation counters to the dev overlay
   (draw calls/vertices are already benchmarked, but not surfaced at runtime).

---

## 9. Flexibility and scalability (re-assessed)

**Flexibility.** The `tokens` + component slots + `theme_lerp` are the right
shape, but the component styles are still one bespoke struct each. Move to the
Tier 3 class/variant model so third parties can add components (and themes) without
touching core theme structs. Open the renderer contract with a material hook
only when needed. Plan the text-shaper and pointer-id seams *before* the JS API
freezes — they affect every text and event signature.

**Scalability.**

- The remaining fixed limits (`MAX_TRACKS 16`, `MAX_DOCK_PANELS 8`,
  `MAX_DOCK_DEPTH 8`, `MAX_STYLE_SCOPES 16`, `MAX_ID_DEPTH 64`) should become
  growable like the overlay stacks did in r94, defaults unchanged.
- `context` is still a large public struct — make it opaque before API
  stabilization (C2).
- Split the 10k-line header (declarations / core / text / widgets / components /
  sdl3 backend) and generate the single-header distribution. The deferral reason
  ("mechanical, large") is valid, but it will be harder after the easy layer
  adds another few thousand lines. Do it **before** Tier 1.
- Components with RAII (`accordion_scope`, `drawer_scope`) and table callbacks
  are fine for apps; for large apps add a documented **screen/module** pattern
  (state struct + `view_t` + `draw_<screen>`, already in `model_view.md`) and
  make the easy layer's `state()` compose with it.
- Test scale: keep snapshot tests as the primary behavior check, add property
  tests for the flex solver, and run the ported bench scene in JS on CI to
  catch bridge regressions.

---

## 10. Roadmap (ordered by dependency)

| Phase | Work | Est. | Done when |
|---|---|---|---|
| **A. Structure** | Split header; amalgamation script; growable limits; opaque `context`; stable window ids | 2–3 wks | single-header artifact passes the whole suite |
| **B. Easy layer v1** (Tier 1) | implicit stacks, label IDs + `key`, auto-scroll, text-first widgets, `std::source_location` errors, idle-sleep default, in-window error panel | 2–3 wks | a todo app in ≤ 40 lines, no rect math, no ids; examples rewritten |
| **C. Layout v2** (Tier 2) | record-then-solve flex (grow/shrink/wrap/align/justify/min-max/percent) | 4–6 wks | property-tested; matches a table of CSS-flex cases |
| **D. Platform basics** | HiDPI scale, images, icons/SVG, multiline input + placeholder/password, clipboard/file dialogs, atlas eviction | 6–8 wks | one real app runs correctly at 100/150/200 % |
| **E. Style as data** (Tier 3) | style struct with inheritance + state variants + classes; port `comp::` to it | 3 wks | no per-component theme slots needed for new components |
| **F. State/lifecycle** (Tier 4) | `state`, `once`, `on_change`, `async`, screen stack | 2 wks | async fetch example without static globals |
| **G. JS runtime** | QuickJS binding, command-buffer bridge, hot reload, `pufferui` runner/bundler, devtools inspector | 8–12 wks | the JS todo app in §6 runs with hot reload; bench ≤ 2× native |
| **H. Reach** | shaping/BiDi/fallback, accessibility adapters, wasm target (optional), touch/pen | open-ended | per-feature design docs promoted to implementations |

Risks:
- **Two layout models** (rect-cutting + flex) can confuse; mitigate by making the
  easy layer the only documented path and rect-cutting the "advanced / custom
  widget" chapter.
- **Label-derived IDs** change identity semantics; keep explicit IDs and the
  duplicate checks as the safety net, and test with the identical-label case.
- **Scope**: A–C are tractable in a quarter; D–H together are a multi-quarter
  platform project comparable to a small UI framework. Decide per product
  whether the target is "great tool-building toolkit" (stop around F) or "React
  replacement for a general audience" (needs G and H, and a team).

---

## 11. Acceptance tests for "a beginner can use it"

Use these as release gates for the easy layer:

1. **5-minute first window** from `git clone` on a clean Windows machine (no
   Developer Prompt) using a scaffold command.
2. **Todo app ≤ 40 lines** with no rect math, no IDs, no statics, a scroll
   list, and a text input with placeholder.
3. **Resize the window**: nothing overlaps or clips; a long label wraps or
   ellipsizes without code.
4. **Change the font size/theme** in one line; layout adapts.
5. **Break it on purpose** (duplicate labels, unbalanced scope): the error
   panel names *the user's file and line* and suggests the fix.
6. **Idle CPU ≈ 0 %** with a static window.
7. **Save the file → UI updates in < 1 s keeping state** (hot reload).
8. **Works at 200 % display scaling** with crisp text.
9. A person who knows React but not C++ (or not Lua/JS native bindings)
   completes the todo tutorial **without asking for help**.

*End of report.*
