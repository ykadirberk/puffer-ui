# PufferUI performance baseline & log

The benchmark target is `pui_bench` (headless, `null_device`): it measures the
**core's** per-frame cost — layout, `interact`, draw-list recording and
flushing — not the renderer. The scene: 2,000 labeled buttons in an auto-fit
grid, 500 text rows, 50 rounded cards, one checkbox/slider/text-field. Each
button is a solid rect + label text + ring, which is exactly the solid↔text
alternation the batching plan (r89 P1) targets.

Times are **machine-relative** (this laptop, Windows, MSVC). The stable,
platform-independent metrics are `draw_calls` and `vertices` — those are what
revisions are judged by. Run:

```sh
out/build/x64-release/Release/pui_bench.exe --frames 300
```

## Baseline — r88 (before the batching work)

| Build | avg ms/frame | worst ms | draw_calls | vertices |
| --- | --- | --- | --- | --- |
| Debug | 105.7 | 117.4 | **4006** | 374,702 |
| Release | 6.4 | 9.2 | **4006** | 374,702 |

Reading: ~2 draw calls per button — every solid rect / ring flushes the text
batch and every label flushes the solid batch (`dl_prepare` flushes on
texture change; solid geometry uses `tex = nullptr`, glyphs use the atlas).
Expected after r89's white-texel change: draw_calls ≈ number of clip changes
(order of 10 for this scene), vertices unchanged.

## Log

| Revision | Change | draw_calls | release avg ms | notes |
| --- | --- | --- | --- | --- |
| r88 | baseline | 4006 | 6.4 | the P1 problem, quantified |
