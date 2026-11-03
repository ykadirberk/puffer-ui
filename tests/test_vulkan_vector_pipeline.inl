// Independent font-free Slug shader checks, before painter/lifetime integration.
// Included in the actual implementation test TU; existing image oracles stay fixed.
static std::vector<std::vector<u8>> vector_pipeline_pixels(vulkan_upload_mode mode)
{
    using namespace renderer_detail::vector_slug;
    using namespace vk_detail;
    std::vector<std::vector<u8>> captures;
    auto *base =
        create_vulkan_device({.validation = true, .vector_text = false, .upload_mode = mode});
    CHECK(base != nullptr);
    if (!base) return captures;
    auto &dev = *static_cast<device *>(base);
    {
        vector_graphics graphics(dev);
        CHECK(graphics.prepare(VK_FORMAT_R8G8B8A8_UNORM));
        CHECK(dev.text == nullptr);
        const std::string_view paths[] = {"M8 8H56V56H8Z",
                                          "M8 32C8 0 56 0 56 32C56 64 8 64 8 32Z",
                                          "M8 8H56V56H8ZM24 24H40V40H24Z",
                                          "M8 8H56V56H8Z",
                                          "M12 32H52",
                                          "M8 8H56V56H8Z"};
        for (usize test = 0; test < std::size(paths); ++test)
        {
            std::vector<path_command> commands;
            vector_error error;
            CHECK(vector_detail::svg_path(paths[test], commands, 1000, error));
            const gradient_stop stops[] = {{0, {255, 0, 0, 255}}, {1, {0, 0, 255, 255}}};
            path_source source{.commands = commands, .view_box = {0, 0, 64, 64}};
            source.fill.tint = {255, 0, 0, 255};
            if (test == 1) source.fill.current_color = true;
            if (test == 2) source.fill_rule = path_fill_rule::EVENODD;
            if (test == 3)
                source.fill = {.kind = vector_paint_kind::LINEAR,
                               .start = {8, 0},
                               .end = {56, 0},
                               .stops = stops};
            if (test == 4)
            {
                source.fill.kind = vector_paint_kind::NONE;
                source.stroke = {.kind = vector_paint_kind::SOLID, .tint = {255, 0, 0, 255}};
                source.stroke_options = {
                    .width = 12, .cap = path_line_cap::ROUND, .join = path_line_join::ROUND};
            }
            if (test == 5)
                source.fill = {.kind = vector_paint_kind::RADIAL,
                               .center = {32, 32},
                               .focal = {32, 32},
                               .radius = 24,
                               .stops = stops};
            auto image = create_path(source);
            CHECK(image);
            if (!image) continue;
            prepared_scene prepared;
            CHECK(prepare_scene(image.image->source, {}, prepared, error));
            {
                vector_buffer metadata(dev);
                CHECK(metadata.prepare(prepared, graphics));
                auto *target = static_cast<texture *>(dev.create_target(64, 64));
                CHECK(target != nullptr);
                if (target)
                {
                    dev.set_target(target);
                    dev.clear({0, 0, 0, 0});
                    for (const auto &step : prepared.steps)
                    {
                        CHECK(step.type == scene_step::PAINT);
                        if (step.type != scene_step::PAINT) continue;
                        vector_instance instance{};
                        instance.bounds[2] = instance.bounds[3] = 64;
                        instance.inverse_x[0] = instance.inverse_y[1] = 1;
                        instance.viewport[2] = instance.viewport[3] = 64;
                        for (auto &channel : instance.tint) channel = 1;
                        instance.current[1] = instance.current[3] = 1;
                        instance.metadata[0] = step.path;
                        instance.metadata[1] = step.paint;
                        instance.metadata[2] = step.clip;
                        CHECK(graphics.draw(dev.white, metadata.descriptor, &instance, 1,
                                            {{0, 0}, {64, 64}}));
                    }
                    std::vector<u8> pixels(64 * 64 * 4);
                    CHECK(dev.read_pixels(target, 0, 0, 64, 64, pixels.data()));
                    CHECK(pixels[3] == 0);
                    const usize center = (32 * 64 + 32) * 4;
                    CHECK(pixels[center + 3] == (test == 2 ? 0 : 255));
                    if (test == 0 || test == 4)
                        CHECK(pixels[center] == 255 && pixels[center + 1] == 0);
                    if (test == 1) CHECK(pixels[center] == 0 && pixels[center + 1] == 255);
                    if (test == 1)
                    {
                        CHECK(pixels[(8 * 64 + 8) * 4 + 3] == 0);
                        bool fractional = false;
                        for (usize i = 3; i < pixels.size(); i += 4)
                            if (pixels[i] > 0 && pixels[i] < 255) fractional = true;
                        CHECK(fractional);
                    }
                    if (test == 2) CHECK(pixels[(12 * 64 + 12) * 4 + 3] == 255);
                    if (test == 3)
                    {
                        CHECK(std::abs(static_cast<i32>(pixels[center]) - 125) <= 1);
                        CHECK(std::abs(static_cast<i32>(pixels[center + 2]) - 130) <= 1);
                    }
                    if (test == 5)
                    {
                        CHECK(std::abs(static_cast<i32>(pixels[center]) - 247) <= 1);
                        CHECK(std::abs(static_cast<i32>(pixels[center + 2]) - 8) <= 1);
                    }
                    CHECK(dev.text == nullptr && dev.stats.validation_errors == 0);
                    captures.push_back(std::move(pixels));
                    dev.set_target(nullptr);
                    dev.destroy_target(target);
                }
                if (dev.frame && dev.frame->recording) dev.submit();
                CHECK(dev.wait(dev.next_serial - 1));
            }
            destroy_vector(image.image);
        }
    }
    destroy_vulkan_device(base);
    return captures;
}
static void vector_pipeline_gpu_regressions()
{
    const auto direct = vector_pipeline_pixels(vulkan_upload_mode::DIRECT);
    const auto staged = vector_pipeline_pixels(vulkan_upload_mode::STAGED);
    CHECK(direct.size() == 6 && staged.size() == 6 && direct == staged);
    if (!g_failures)
        std::puts("all font-independent Slug shader regressions passed (12 frames, exact "
                  "direct/staged equality)");
}
