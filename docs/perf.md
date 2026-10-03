# PufferUI performance benchmark & log

The benchmark is `pui_bench` (headless, `null_device`): it measures the
**core's** per-frame cost — layout, `interact`, draw-list recording and
flushing — not the renderer. The scene: 2,000 labeled buttons in an auto-fit
grid, 500 text rows, 50 rounded panels, plus a focused text field / checkbox
/ slider so the per-frame state paths stay hot. Each button is a rect + text
+ border loop, which is exactly the draw-batching workload this file tracks.

Output is one line: `frames=N avg=…ms worst=…ms draw_calls=N vertices=N`.
The timing is machine-relative (recorded here: Windows, MSVC, one laptop);
the **draw_calls / vertices** counts are the platform-stable numbers that
commits are judged by. Run:

```sh
out/build/x64-release/Release/pui_bench.exe --frames 300
```

## Baseline (before the batch/storage work)

| Build | avg ms/frame | worst ms | draw_calls | vertices |
| --- | --- | --- | --- | --- |
| Debug  | 105.7 | 117.4 | **4006** | 374,702 |
| Release | 6.4 | 9.2 | **4006** | 374,702 |

Reading: ~2 draw calls per button — every solid rect / outline flushes the
text batch and every text run flushes the geometry batch (`draw_rect` records
with the null texture, glyphs with the atlas texture; the batcher flushes on
that switch).

## History

| Commit | Change | draw_calls | release avg ms | notes |
| --- | --- | --- | --- | --- |
| (r88 baseline) | — | 4006 | 6.4 | the batch break measured |
| r89 (2573dae) | white texel (solid joins the glyph batch) | **2** | 6.4 | 2000× fewer calls |
| r89 | text layout cache (one probe, measure+emit shared) | 2 | 5.0 | -22% frame cost |
| r89 | scratch buffers + rotation recurrence in primitives | 2 | 4.8 | -25% total |
| r89 | SDL zero-copy (strided RenderGeometryRaw) | 2 | 4.8 | renderer-side copy removed |
| r89 | draw-list culling + `is_visible` + `needs_redraw` gate | 2 | 4.8 | -25% total |
| geometric culling | draws wholly outside the client area or the clip are never submitted (shapes, text per run/glyph, blur); early outs before fans are built | 2 | 2.7 | vertices 374,702 -> 221,570 (-41%); release avg 4.0 -> 2.7 ms |
