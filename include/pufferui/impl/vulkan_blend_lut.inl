// Exact finite-domain compatibility for RGBA8 source/destination blending.
// Calibrates from the existing native pipeline; no guessed blend precision.
// RGB8/alpha8 only. Float coverage and textured products remain unqualified.
// Included inside namespace pui by the single implementation translation unit.
#if defined(PUFFERUI_ENABLE_VULKAN)
namespace vk_detail
{
struct rgba8_blend_lut
{
    static constexpr u32 SIDE = 256, BYTES = 16842752;
    gpu_bin_job storage;
    bool valid = false;
    f64 preparation_ms = 0;
    bool initialize(device &d)
    {
        if (storage.dev || d.current_surface) return false;
        const f64 start = wall_ms();
        paint_detail::command_tape empty;
        if (!empty.prepare(0, 0, 0) || !storage.initialize(d, empty, SIDE, SIDE, 32, 256, 0) ||
            !storage.resize_storage(0, SIDE * SIDE * 4) || !storage.resize_storage(1, BYTES))
            return false;
        const VkPipeline pipeline = compute_pipeline(d, storage.layout, vulkan_blend_calibrate,
                                                     sizeof(vulkan_blend_calibrate));
        if (!pipeline) return false;
        std::vector<u8> source(SIDE * SIDE * 4), destination(SIDE * SIDE * 4);
        for (u32 y = 0; y < SIDE; ++y)
            for (u32 x = 0; x < SIDE; ++x)
            {
                const u32 i = (y * SIDE + x) * 4;
                source[i] = source[i + 1] = source[i + 2] = static_cast<u8>(x);
                source[i + 3] = 255;
                destination[i] = destination[i + 1] = destination[i + 2] = destination[i + 3] =
                    static_cast<u8>(y);
            }
        auto src = d.create_texture(SIDE, SIDE, source.data());
        auto dst = d.create_texture(SIDE, SIDE, destination.data());
        auto target = d.create_target(SIDE, SIDE);
        bool success = src && dst && target && d.commands();
        if (success)
        {
            d.end_rendering();
            d.vkCmdFillBuffer(d.frame->command, storage.buffers[4].buffer, 0, VK_WHOLE_SIZE, 0);
            storage.barrier(VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                            VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
            storage.recorded = true;
            storage.serial = d.next_serial;
        }
        for (u32 alpha = 0; success && alpha < 256; ++alpha)
        {
            d.blit(dst, nullptr, target, nullptr);
            d.set_target(target);
            const color tint{255, 255, 255, static_cast<u8>(alpha)};
            const vertex v[] = {{0, 0, 0, 0, tint},
                                {SIDE, 0, 1, 0, tint},
                                {SIDE, SIDE, 1, 1, tint},
                                {0, SIDE, 0, 1, tint}};
            const i32 indices[] = {0, 1, 2, 0, 2, 3};
            d.draw(src, v, 4, indices, 6);
            d.end_rendering();
            d.transition(static_cast<texture *>(target), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            storage.barrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT,
                            VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
            VkBufferImageCopy copy{};
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.imageExtent = {SIDE, SIDE, 1};
            d.vkCmdCopyImageToBuffer(d.frame->command, static_cast<texture *>(target)->image,
                                     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                     storage.buffers[0].buffer, 1, &copy);
            storage.barrier(VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
            storage.barrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                            VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
            storage.parameters[7] = alpha;
            d.vkCmdBindPipeline(d.frame->command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
            d.vkCmdBindDescriptorSets(d.frame->command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                      storage.layout, 0, 1, &storage.descriptor, 0, nullptr);
            d.vkCmdPushConstants(d.frame->command, storage.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                 sizeof(storage.parameters), storage.parameters);
            d.vkCmdDispatch(d.frame->command, 256, 1, 1);
            success = d.result == VK_SUCCESS;
        }
        if (success)
        {
            storage.barrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
            storage.serial = d.submit();
            success = storage.serial && d.wait(storage.serial);
            if (success && !storage.buffers[4].coherent)
            {
                VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
                range.memory = storage.buffers[4].memory;
                range.size = VK_WHOLE_SIZE;
                success = d.check(d.vkInvalidateMappedMemoryRanges(d.native.device, 1, &range),
                                  "blend calibration status");
            }
            if (success) success = reinterpret_cast<const u32 *>(storage.buffers[4].mapped)[3] == 0;
        }
        d.vkDestroyPipeline(d.native.device, pipeline, nullptr);
        if (src) d.destroy_texture(src);
        if (dst) d.destroy_texture(dst);
        if (target) d.destroy_target(target);
        preparation_ms = wall_ms() - start;
        valid = success;
        return success;
    }
    void release()
    {
        storage.release();
        valid = false;
    }
};
} // namespace vk_detail
#endif
