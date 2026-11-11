# Vulkan frame preparation and optional third slot

`vulkan_prepare_window_blur` allocates a registered window's backdrop scratch
before painting. Its first frame records the usual clears. The same scratch
sizes, Gaussian and Kawase passes, clip calculations, and composite arithmetic
are used. A larger hint can grow the scratch, and retired images follow the
Vulkan timeline. The showcase startup callback calls the new API.

Two frames in flight are the default. With three or more slots, a busy next
logical slot can yield to a completed one. When every slot is busy, the surface
waits for the oldest pending logical completion. Command arenas still track
their own submissions, and native presentation uses its existing image-owned
semaphores. In core, `motion_info` reuses the most recent `animate_rect`
lookup when the scoped key matches. Frame startup clears the saved pointer
before expiring old entries.

## 5000-item Spiral

The benchmark renders the actual Showcase Layout lab offscreen at 1360 × 860.
It starts with 5000 size-64 Spiral tiles and performs keyboard Add at frames
90, 120 and 150 over 180 frames with a fixed 1/60 s simulation step. Both
revisions use the same fonts, SVGs, analytic rounded shapes and Release flags.
The baseline is `fc43a11`. Each column uses one warmup per revision and nine
alternating measured pairs on Windows, AMD Radeon RX 9070 XT, Vulkan driver
`8389024`. Timing runs have no readbacks or validation layers.

| Measurement | DIRECT, 2 slots | STAGED, 2 slots | DIRECT, 3 slots | STAGED, 3 slots |
| --- | ---: | ---: | ---: | ---: |
| Setup (ms) | 160.913 → 158.777 | 160.442 → 163.181 | 158.878 → 163.275 | 173.672 → 167.571 |
| Frame 0 (ms) | 6.649 → 3.313 | 6.711 → 3.368 | 7.458 → 3.450 | 6.546 → 3.605 |
| Frame 0 UI build (ms) | 6.458 → 3.109 | 6.252 → 2.866 | 7.129 → 3.042 | 6.317 → 2.931 |
| Setup plus frame 0 (ms) | 167.374 → 162.180 | 167.220 → 166.549 | 166.422 → 167.423 | 180.219 → 170.992 |
| Steady UI build (ms) | 1.297 → 1.252 | 1.305 → 1.227 | 1.305 → 1.233 | 1.326 → 1.233 |
| Mean frame (ms) | 1.446 → 1.405 | 1.464 → 1.386 | 1.491 → 1.383 | 1.529 → 1.420 |
| p99 frame (ms) | 2.187 → 2.158 | 2.280 → 2.403 | 2.250 → 2.291 | 2.598 → 2.511 |
| Completed GPU time (ms) | 116.857 → 116.829 | 112.791 → 112.728 | 116.912 → 116.949 | 112.582 → 112.469 |
| Resident upload pages (MiB) | 9.59 → 9.59 | 17.59 → 17.59 | 13.59 → 13.59 | 25.59 → 25.59 |
| Worst observed frame and time (ms) | 0 · 8.197 → 2 · 4.313 | 0 · 8.293 → 0 · 5.120 | 0 · 9.429 → 2 · 7.003 | 0 · 7.684 → 2 · 7.153 |
| Longest observed slot wait (ms) | 0.473 → 0 | 0.332 → 0 | 0 → 0 | 0 → 0 |

The table uses medians of per-run metrics except the worst frame and longest
wait, which keep the maximum across all nine runs. Steady UI build uses
frames 30 through 179. Frame 0 is faster in all 36 measured pairs. The median
paired steady UI build savings are 0.069 ms DIRECT and 0.074 ms STAGED at
the default depth. Setup plus frame 0 improves in eight of nine DIRECT
pairs and five of nine STAGED pairs at that depth.

The largest candidate frames have zero slot wait. The 7.003 ms DIRECT and
7.153 ms STAGED three-slot peaks are frame 2, with 2.088 and 2.831 ms in UI
construction. Every timed run recorded 180 submissions and zero draw-time
upload buffer allocations. The third slot adds 4 MiB DIRECT or 8 MiB STAGED
in this workload. All measured waits stayed below 1 ms, so these runs
exercise the optional slot and its memory cost without reproducing the
earlier wait spikes.

## Verification

Separate readback runs compare the SHA-256 of every full RGBA framebuffer
against frozen `fc43a11` frames. DIRECT and STAGED each cover two and
three slots with both analytic and mesh rounded shapes. All 1,440 frame
comparisons match, including startup, all Add events and every timed peak
frame index.

The private preflight tests cover window ownership, dirty surfaces, repeated
preparation, second-image allocation failure, deferred clears, slot selection,
and Gaussian and Kawase output at 1×, 2× and 4× DPI with cache off and on.
The extra WSI run exercises an opt-in third slot with resize, overlays,
multiple windows and zero extent. The state regression covers scoped motion
keys, map growth and expiration.

The full Release suite passed all 61 Vulkan cases, the existing SDL golden
scenes and Spiral peak fixtures, installed consumers, example selftests,
API and tutorial checks, and clang-format.
