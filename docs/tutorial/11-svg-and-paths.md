# 11. SVG and paths without example shorthand

![The Explore Material Icon and a C++ path on Vulkan](img/vector-manual.png)

Run: `pui_tut_vector_manual` (Vulkan enabled). The complete application is
[`examples/tutorial/vector_manual.cpp`](../../examples/tutorial/vector_manual.cpp).
It includes no `example_common.h` and uses no example bootstrap or gallery
helper. It owns the SDL window, device, surface, context, immutable sources,
model, event loop and teardown. The public `create_sdl3_vulkan_device` factory
supplies SDL's Vulkan platform adapter; chapter 10 shows an application-owned
adapter when integrating a different window system.

Build and run from the repository:

```powershell
cmake --preset x64-release -DPUFFERUI_WITH_VULKAN=ON
cmake --build out/build/x64-release --target pui_tut_vector_manual
& "out/build/x64-release/Release/pui_tut_vector_manual.exe"
```

For an installed library, use `find_package(pufferui CONFIG REQUIRED COMPONENTS
vulkan)`, link your executable to `pufferui::vulkan` and SDL3, and point the file
paths at the SVG and font in your app. Fresh Vulkan builds
enable HarfBuzz/Slug. An explicit HarfBuzz OFF configuration still draws the
same semantic resources through the portable vector painter.

## Load and prepare once

The left preview loads Google's Explore icon from a bundled local file. The
helper reads the XML during initialization and passes it to `load_svg`. The
parser copies the source, so the temporary XML string can go out of scope.
The app checks the structured result and owns the returned handle. See the
[file loader](../../examples/material_icons.h) and
[the icon's source and license](../../assets/icons/material/README.md).

<!-- src: examples/tutorial/vector_manual.cpp -->
```cpp
        const auto svg = example_assets::load_svg_file(example_assets::EXPLORE_SVG);
        if (!svg)
        {
            std::fprintf(stderr, "%s: %s (byte %zu)\n", example_assets::EXPLORE_SVG,
                         svg.error.message, svg.error.offset);
            return false;
        }
        icon = svg.image;
```

The file loader checks the parser's byte limit before allocating the XML buffer.

<!-- src: examples/material_icons.h -->
```cpp
    const svg_load_opts limits{};
    // ...
    const std::streamoff length = file.tellg();
    if (length < 0) return {nullptr, {vector_error_code::INVALID_SVG, 0, "cannot open SVG file"}};
    if (static_cast<u64>(length) > limits.max_bytes)
        return {nullptr, {vector_error_code::LIMIT, 0, "SVG file exceeds the byte limit"}};
    std::string xml(static_cast<usize>(length), '\0');
    if (!file.seekg(0) || !file.read(xml.data(), static_cast<std::streamsize>(length)))
        return {nullptr, {vector_error_code::INVALID_SVG, 0, "cannot read SVG file"}};
    return load_svg(xml, limits);
```

The right preview creates an immutable C++ path from commands and paint options.

<!-- src: examples/tutorial/vector_manual.cpp -->
```cpp
        const path_command commands[] = {{path_verb::MOVE, {16, 96}},
                                         {path_verb::CUBIC, {16, 16}, {112, 16}, {112, 96}},
                                         {path_verb::QUADRATIC, {64, 56}, {16, 96}},
                                         {path_verb::CLOSE}};
        const auto source =
            create_path({.commands = commands,
                         .fill = {.kind = vector_paint_kind::SOLID, .tint = {116, 193, 255, 230}},
                         .stroke = {.kind = vector_paint_kind::SOLID, .tint = {255, 255, 255, 255}},
                         .stroke_options = {.width = 3, .join = path_line_join::ROUND},
                         .view_box = {0, 0, 128, 128}});
```

The commands are copied; the local array does not need to outlive this function.
`prepare_vector` then selects and stores the renderer's representation **before
the first frame clock starts**:

<!-- src: examples/tutorial/vector_manual.cpp -->
```cpp
        const vector_prepare_opts preparation{.max_pixel_size = {256, 256},
                                              .max_instances = 8,
                                              .resource_budget_bytes = 16 * 1024 * 1024,
                                              .style = {.current_color = ICON_COLOR}};
        // Resource setup runs before the first begin_frame. Draws reuse it.
        return static_cast<bool>(u.prepare_vector(icon, preparation)) &&
               static_cast<bool>(u.prepare_vector(path, preparation));
```

Extent is in framebuffer pixels. Include DPI and the maximum transform scale.
The tutorial clamps each preview to the prepared extent after resize/DPI changes.
A zoomable application can request a larger preparation between frames; check
the result before using the new scale. Preparation is loading work and belongs
in startup measurements.

## Submit each frame

The model owns opacity and angle. `draw_vector` submits the destination
rectangle, affine transform and style each frame. The SVG's root
`fill="currentColor"` takes its color from `ICON_COLOR`. Native Vulkan resolves
coverage on the GPU, while the portable renderer reuses its prepared texture.

<!-- src: examples/tutorial/vector_manual.cpp -->
```cpp
            const auto status = u.draw_vector(
                {center.x - size * .5f, center.y - size * .5f, size, size}, i ? path : icon,
                {.transform = rotation,
                 .style = {.current_color = ICON_COLOR, .opacity = opacity}});
```

Always inspect the returned status. A missed preparation is a reported
`NOT_PREPARED` error rather than an allocation or partial drawing hidden inside
the frame. Palette variants used by the portable painter must also be prepared.

## Input and lifetime

The application's SDL event loop calls `sdl3_route`, handles close/minimized
windows and calls `begin_frame`/`end_frame`. VSync is configured once at startup.
Idle input can wait without repeatedly drawing an unchanged screen. All UI
scopes end before `end_frame`.

During shutdown the application releases both painter bindings, destroys its
source owners, then destroys the context, surface, device and SDL window.
Device destruction waits for its owned GPU work. A copied source pointer is not
another owner: use `retain_vector` when ownership really is shared.

The standalone selftest sends a real SDL click to the rotation button, resizes
the window, draws eight frames and requires zero UI/validation violations:

```powershell
& "out/build/x64-debug/Debug/pui_tut_vector_manual.exe" --selftest --frames 8 --resize 800x560 --screenshot vector.bmp
```

Try `pui_ex_svg_icons` or `pui_ex_svg_icons_vulkan` to reuse Favorite at three
sizes and switch between two prepared colors. The gallery adds six Material
Icons, gradient badges, dashed strokes and clipping. See the
[focused example](../../examples/atomic/svg_icons.cpp),
[gallery source](../../examples/vector_gallery.h) and [SVG/path contract](../vector_paths.md).
