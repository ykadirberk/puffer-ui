// Observe actual driver calls, independently of the renderer's state cache.
namespace
{
struct graphics_call_probe;
thread_local graphics_call_probe *graphics_probe = nullptr;
struct graphics_call_probe
{
    struct command_state
    {
        VkCommandBuffer command = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkViewport viewport{};
        VkRect2D scissor{};
        VkPipelineLayout projection_layout = VK_NULL_HANDLE, sample_layout = VK_NULL_HANDLE;
        VkDescriptorSet sample = VK_NULL_HANDLE;
        u32 projection[4]{}, projection_bytes = 0;
        VkShaderStageFlags projection_stages = 0;
        bool viewport_valid = false, scissor_valid = false;
    } states[16];
    vk_detail::device &device;
    PFN_vkBeginCommandBuffer begin;
    PFN_vkCmdBindPipeline pipeline;
    PFN_vkCmdSetViewport viewport;
    PFN_vkCmdSetScissor scissor;
    PFN_vkCmdPushConstants push;
    PFN_vkCmdBindDescriptorSets sets;
    u32 binds = 0, viewports = 0, scissors = 0;
    u32 duplicate_binds = 0, duplicate_viewports = 0, duplicate_scissors = 0;
    u32 duplicate_projections = 0, duplicate_samples = 0;
    bool overflow = false;
    command_state &state(VkCommandBuffer command)
    {
        for (auto &entry : states)
            if (entry.command == command) return entry;
        for (auto &entry : states)
            if (!entry.command)
            {
                entry.command = command;
                return entry;
            }
        overflow = true;
        return states[0];
    }
    static VKAPI_ATTR VkResult VKAPI_CALL on_begin(VkCommandBuffer command,
                                                   const VkCommandBufferBeginInfo *info)
    {
        const auto result = graphics_probe->begin(command, info);
        if (result == VK_SUCCESS) graphics_probe->state(command) = {.command = command};
        return result;
    }
    static VKAPI_ATTR void VKAPI_CALL on_pipeline(VkCommandBuffer command,
                                                  VkPipelineBindPoint point, VkPipeline value)
    {
        auto &probe = *graphics_probe;
        if (point == VK_PIPELINE_BIND_POINT_GRAPHICS)
        {
            auto &state = probe.state(command);
            ++probe.binds;
            probe.duplicate_binds += state.pipeline == value;
            state.pipeline = value;
        }
        probe.pipeline(command, point, value);
    }
    static VKAPI_ATTR void VKAPI_CALL on_viewport(VkCommandBuffer command, u32 first, u32 count,
                                                  const VkViewport *value)
    {
        auto &probe = *graphics_probe;
        auto &state = probe.state(command);
        ++probe.viewports;
        if (first == 0 && count == 1)
        {
            probe.duplicate_viewports +=
                state.viewport_valid && !std::memcmp(&state.viewport, value, sizeof(*value));
            state.viewport = *value;
            state.viewport_valid = true;
        }
        else
            state.viewport_valid = false;
        probe.viewport(command, first, count, value);
    }
    static VKAPI_ATTR void VKAPI_CALL on_scissor(VkCommandBuffer command, u32 first, u32 count,
                                                 const VkRect2D *value)
    {
        auto &probe = *graphics_probe;
        auto &state = probe.state(command);
        ++probe.scissors;
        if (first == 0 && count == 1)
        {
            probe.duplicate_scissors +=
                state.scissor_valid && !std::memcmp(&state.scissor, value, sizeof(*value));
            state.scissor = *value;
            state.scissor_valid = true;
        }
        else
            state.scissor_valid = false;
        probe.scissor(command, first, count, value);
    }
    static VKAPI_ATTR void VKAPI_CALL on_push(VkCommandBuffer command, VkPipelineLayout layout,
                                              VkShaderStageFlags stages, u32 offset, u32 size,
                                              const void *data)
    {
        auto &probe = *graphics_probe;
        if (stages & (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT))
        {
            auto &state = probe.state(command);
            if (!offset && size <= sizeof(state.projection))
            {
                probe.duplicate_projections +=
                    state.projection_layout == layout && state.projection_stages == stages &&
                    state.projection_bytes == size && !std::memcmp(state.projection, data, size);
                std::memcpy(state.projection, data, size);
                state.projection_bytes = size;
                state.projection_layout = layout;
                state.projection_stages = stages;
            }
            else
                state.projection_bytes = 0;
        }
        probe.push(command, layout, stages, offset, size, data);
    }
    static VKAPI_ATTR void VKAPI_CALL on_sets(VkCommandBuffer command, VkPipelineBindPoint point,
                                              VkPipelineLayout layout, u32 first, u32 count,
                                              const VkDescriptorSet *values, u32 dynamic_count,
                                              const u32 *dynamic_offsets)
    {
        auto &probe = *graphics_probe;
        if (point == VK_PIPELINE_BIND_POINT_GRAPHICS && !first && count && !dynamic_count)
        {
            auto &state = probe.state(command);
            probe.duplicate_samples += state.sample_layout == layout && state.sample == values[0];
            state.sample = values[0];
            state.sample_layout = layout;
        }
        probe.sets(command, point, layout, first, count, values, dynamic_count, dynamic_offsets);
    }
    explicit graphics_call_probe(vk_detail::device &value)
        : device(value), begin(value.vkBeginCommandBuffer), pipeline(value.vkCmdBindPipeline),
          viewport(value.vkCmdSetViewport), scissor(value.vkCmdSetScissor),
          push(value.vkCmdPushConstants), sets(value.vkCmdBindDescriptorSets)
    {
        CHECK(!graphics_probe);
        graphics_probe = this;
        device.vkBeginCommandBuffer = on_begin;
        device.vkCmdBindPipeline = on_pipeline;
        device.vkCmdSetViewport = on_viewport;
        device.vkCmdSetScissor = on_scissor;
        device.vkCmdPushConstants = on_push;
        device.vkCmdBindDescriptorSets = on_sets;
    }
    ~graphics_call_probe()
    {
        device.vkBeginCommandBuffer = begin;
        device.vkCmdBindPipeline = pipeline;
        device.vkCmdSetViewport = viewport;
        device.vkCmdSetScissor = scissor;
        device.vkCmdPushConstants = push;
        device.vkCmdBindDescriptorSets = sets;
        graphics_probe = nullptr;
    }
};
void graphics_state_quad(render_device &device, rect bounds, color tint)
{
    const vertex vertices[] = {{bounds.x, bounds.y, 0, 0, tint},
                               {bounds.right(), bounds.y, 1, 0, tint},
                               {bounds.right(), bounds.bottom(), 1, 1, tint},
                               {bounds.x, bounds.bottom(), 0, 1, tint}};
    constexpr i32 indices[] = {0, 1, 2, 0, 2, 3};
    device.draw(nullptr, vertices, 4, indices, 6);
}
} // namespace

PUI_TEST(graphics_state_is_reused_only_within_each_recorded_command_buffer)
{
    for (auto mode : {vulkan_painter_mode::REFERENCE, vulkan_painter_mode::SDF_RASTER,
                      vulkan_painter_mode::COMPUTE_SDF})
        for (bool staged : {false, true})
        {
            program_scene scene(mode, staged);
            if (!scene.win) return;
            auto &device = *scene.base;
            const auto target = scene.surface->scene_target();
            const auto small = device.create_target(73, 55);
            CHECK(small);
            graphics_call_probe probe(*scene.native);
            for (i32 frame = 0; frame < 4; ++frame)
            {
                scene.surface->make_current(device);
                device.set_target(target);
                device.set_clip(nullptr);
                device.clear({static_cast<u8>(17 + frame), 31, 71, 255});
                for (i32 i = 0; i < 13; ++i)
                    graphics_state_quad(device, {3.0f + i * 7, 5, 5, 18}, {31, 61, 131, 255});
                device.set_target(small);
                device.set_clip(nullptr);
                device.clear({13, 37, 61, 255});
                graphics_state_quad(device, {10, 10, 20, 20}, {0, 255, 0, 255});
                const rect destination{20, 20, 73, 55};
                device.blit(small, nullptr, target, &destination);
                device.set_target(target);
                graphics_state_quad(device, {110, 80, 8, 8}, {255, 0, 0, 255});
                scene.surface->present();
                u8 pixel[4]{};
                CHECK(device.read_pixels(target, 1, 1, 1, 1, pixel));
                CHECK(pixel[0] == 17 + frame && pixel[1] == 31 && pixel[2] == 71);
                CHECK(device.read_pixels(target, 35, 35, 1, 1, pixel));
                CHECK(pixel[0] == 0 && pixel[1] == 255 && pixel[2] == 0);
                CHECK(device.read_pixels(target, 114, 84, 1, 1, pixel));
                CHECK(pixel[0] == 255 && pixel[1] == 0 && pixel[2] == 0);
            }
            if (probe.duplicate_binds || probe.duplicate_viewports || probe.duplicate_scissors)
                std::fprintf(stderr,
                             "graphics mode=%d staged=%d duplicate pipeline=%u "
                             "viewport=%u scissor=%u\n",
                             static_cast<i32>(mode), staged, probe.duplicate_binds,
                             probe.duplicate_viewports, probe.duplicate_scissors);
            CHECK(!probe.overflow && probe.binds && probe.viewports && probe.scissors);
            CHECK(!probe.duplicate_binds && !probe.duplicate_viewports &&
                  !probe.duplicate_scissors);
            CHECK(!vulkan_statistics(scene.base).validation_errors);
            CHECK(!probe.duplicate_projections && !probe.duplicate_samples);
            device.destroy_texture(small);
        }
}
