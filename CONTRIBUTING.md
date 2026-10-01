# Contributing to PufferUI

## The non-negotiables

1. **Every change lands with tests.** A fix without a regression test is
   incomplete. New behavior needs headless tests in `tests/` (split by area:
   layout, input, text, widgets, dock, render — see `tests/test_util.h`).
   Run the suite with `out/build/<preset>/…/pui_core_tests.exe`; it must
   print `all core tests passed` and exit 0. `--filter NAME` runs one test;
   `--list` lists them.
2. **Violations are never printed.** Tests install a violation handler and
   assert the guard message; suite output stays clean.
3. **All suites green in debug and release** before reporting work as done:
   `pui_core_tests`, `pui_pump_tests`, `pui_golden_tests`, and
   `examples_selftest` (every example runs offscreen and asserts zero
   violations).
4. **Formatting**: `clang-format --dry-run --Werror` must pass for every
   tracked `.h`/`.cpp` outside `vendored/` (CI checks the `git ls-files`
   list, so new files are covered automatically).
5. **Golden images**: `pui_golden_tests` compares against committed BMPs.
   Regenerate with `pui_golden_tests --update` only after eyeballing the
   regenerated images; batching/rendering changes regenerate goldens in one
   reviewed PR.
6. **README snippets are copied from example sources** — when an example
   changes, update the matching snippet in the same change.
7. **README + AGENTS.md are updated in the same revision** that changes what
   they describe.

## House rules (short version)

- Layout is pure rect-cutting: prefer `cut_*`/`column`/`row`/`track_*`/
  `auto_fit_grid` over manual coordinate math; never cut a temporary
  (`r.content().cut_top(h)` is a compile error — the rvalue cuts are
  deleted).
- No smart pointers; raw pointers are non-owning; ownership is explicit.
- `namespace pui`; `lower_snake_case` for everything but macros/enumerators.
- Overlapping interactive rects are a feature with defined semantics
  (topmost wins), not a violation.
- One context per thread (the current-context slot is `thread_local`).
- Identity is explicit: `_id` literals, `id_child`, `u.local`. No
  label-derived IDs (rejected permanently).
- The component convention (see the plan's r91 phase) is the way new
  reusable widgets are added; the definition-of-done checklist lives in the
  plan.

## The plan

Work is sequenced in `~/.opencode/plan/pufferui-roadmap.md` (phases r87–r95,
standing decisions, per-phase deliverables). Read it before starting; update
it when something lands.

## Performance changes

Land with **before/after numbers** in `docs/perf.md` (the benchmark target
`pui_bench` produces them).
