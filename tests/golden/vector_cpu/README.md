# Immutable native simple-path frames

`frames.zip` contains all 180 complete 512x512 RGBA8 frames of the unchanged
5000-path vector benchmark: rotation changes every frame and currentColor every
30 frames. The benchmark's source is reused by `pui_vulkan_vector_cpu_tests`;
timing remains in the original independent `pui_vulkan_vector_data_tests`.

The frames were captured with frozen headers from
`a0dbe72c5e642540bcd758c69360d05e449ead9a` before the placement/index fixes.
DIRECT and STAGED captures are byte-identical; one set is retained along with
both hash maps and the frozen executable identity in `receipt.json`.
The recorded adapter is Radeon RX 9070 XT, driver 8389024. Fixed native captures
qualify that adapter/driver; same-device correctness tests remain independent.

Capture accepts only a new directory. Existing references are never regenerated
to accept a backend difference. `tools/compare_vulkan_vector_cpu.py` checks all
simple, composed-SVG and Spiral frames separately from readback-free timings.
