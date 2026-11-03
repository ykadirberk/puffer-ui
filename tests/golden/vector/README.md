# Native SVG/path fixtures

21 immutable RGBA8 images, each 64 x 64 x 4 bytes, produced by
`pui_vulkan_vector_data_tests --capture-reference NEW_DIRECTORY`.
Seven independent painter scenes at 1x/2x/4x DPI cover solid fill, isolated group
opacity, clip union, evenodd clipping, dashed round strokes, root opacity and
ordered mesh/vector/scissor restoration. `--goldens DIRECTORY` compares them
and independently requires all 84 direct/staged/cache mode images to match.

These are new fixtures for the SVG/path feature. Existing goldens are unchanged.
Exact native raster images qualify the recorded adapter/driver; cross-vendor
identity is not assumed. See `docs/vector_paths.md` and the feature's validation
report for the capture receipt. Never overwrite these to hide a regression.
