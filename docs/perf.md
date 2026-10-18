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

## text_area (r102)

`pui_bench --textarea`: a read-only, unwrapped `text_area` holding a 10,000-line log
that grows by one line every frame (1400x800 at 1600x1000). The line layout is
rebuilt only when the text changes (here: every frame), and only the visible
rows are drawn.

| Build | avg ms/frame | worst ms | draw_calls | vertices |
| --- | --- | --- | --- | --- |
| Release | 1.0 | 1.2 | 2 | 8,723 |

## Glyph atlas budget (r103)

`pui_bench --atlas [--budget N]`: one of 20 text sizes per frame (24..138 px),
~80 distinct glyphs each, 600 frames. Release.

| Budget | atlas pages at the end | avg ms/frame |
| --- | --- | --- |
| 0 (unlimited, the old behavior) | 10 | 0.004 |
| 16 (default) | 10 | 0.004 |
| 4 | 4 | 0.005 |

The default scenario is unchanged (2 draw calls, 221,570 vertices).

## Large text and long lists (r108)

New scenes (all 1600x1000, release, median of 3 interleaved runs of 200 frames
against the pre-r108 binary on the same machine):

| Scene | before avg ms | after avg ms | what changed |
| --- | --- | --- | --- |
| `--wrap-type` (360 KB wrapped document, model edited every frame) | 961 | 1.8 | one-pass wrap + per-rebuild advance/kerning memo |
| `--wrap-resize` (same document, width changing every frame) | 909 | 1.9 | same |
| `--bigtext` (static 1 MB log, line numbers, scrolled to the end) | 1.68 | 0.068 | no per-frame hash / newline count / model copy |
| `--longlines` (60 static lines x 12,000 chars, no wrap) | 10.5 | 6.7 | runs stop at the right edge |
| `--vlist` (200,000 rows, `heights_version` 0) | 0.49 | 0.47 | unchanged by design |
| `--vlist-kept` (the same with `heights_version` set) | 0.49 | 0.006 | kept row offsets |
| `--a11y` (default scene + 1 MB text area, accessibility on) | 5.7 | 4.0 | node slots reused |
| default scene | 2.80 | 2.83 | within noise |

What is left in `--longlines`: most of the 6.7 ms is the direct-mapped
layout cache - two pairs of the 47 visible 12,000-glyph lines share a slot
and evict each other every frame (about four full rebuilds per frame).

## Glyph checks and overlay replay (r109)

`pui_bench` release, 5 interleaved runs of 400 frames against r108:

| Scene | r108 median ms | r109 median ms |
| --- | --- | --- |
| default | 2.88 | 2.84 (within noise) |
| `--longlines` | 6.69 | 6.80 (within noise) |
| `--atlas` | 0.003 | 0.003 |

The per-glyph liveness check and LRU store were a few nanoseconds per glyph
(~20k glyphs per default frame): below what this benchmark resolves. The
overlay replay change removes one vertex-array copy per open spilling popup
per frame; no scene measures it.
