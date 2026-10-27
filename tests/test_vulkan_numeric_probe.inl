#include "vulkan_test_options.h"
// Numerical experiment, not a visual acceptance test. The renderer does not use
// this compute shader. Reports raw native-vs-compute quantization differences.
#undef near
#undef far
#undef small
#include <cstdio>
#include <filesystem>
#include <fstream>
using namespace pui;

namespace
{
constexpr u32 PROBE_SIDE = 256, COUNT = PROBE_SIDE * PROBE_SIDE;
i32 errors = 0;
void numeric_diagnostic(i32 code, const char *message, void *)
{
    std::fprintf(stderr, "Vulkan %d: %s\n", code, message);
    ++errors;
}
u32 packed(const u8 *p)
{
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) | (static_cast<u32>(p[2]) << 16) |
           (static_cast<u32>(p[3]) << 24);
}
} // namespace
int main(int argc, char **argv)
{
    bool all_alpha = false, gradient = false;
    u32 mode = 0;
    u32 shift = 0;
    std::filesystem::path dump;
    std::filesystem::path lookup;
    bool native_lookup = false;
    for (i32 i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--all-alpha") == 0)
            all_alpha = true;
        else if (std::strcmp(argv[i], "--gradient") == 0)
            gradient = true;
        else if (std::strcmp(argv[i], "--mode") == 0 && i + 1 < argc)
            mode = static_cast<u32>(std::atoi(argv[++i]));
        else if (std::strcmp(argv[i], "--dump") == 0 && i + 1 < argc)
            dump = argv[++i];
        else if (std::strcmp(argv[i], "--shift") == 0 && i + 1 < argc)
            shift = static_cast<u32>(std::atoi(argv[++i])) % PROBE_SIDE;
        else if (std::strcmp(argv[i], "--lut") == 0 && i + 1 < argc)
            lookup = argv[++i];
        else if (std::strcmp(argv[i], "--native-lut") == 0)
            native_lookup = true;
        else
            return 1;
    }
    if (!dump.empty() &&
        (std::filesystem::exists(dump) || !std::filesystem::create_directories(dump)))
        return 1;
    vulkan_device_opts options = test_detail::reference_options();
    if (native_lookup && (!lookup.empty() || gradient)) return 1;
    options.validation = true;
    options.error_callback = numeric_diagnostic;
    auto *device = create_vulkan_device(options);
    auto *d = dynamic_cast<vk_detail::device *>(device);
    if (!d) return 1;
    std::vector<u8> source(COUNT * 4), destination(COUNT * 4), expected(COUNT * 4);
    for (u32 i = 0; i < COUNT; ++i)
    {
        const u32 x = (i % PROBE_SIDE + shift) % PROBE_SIDE, y = i / PROBE_SIDE;
        source[i * 4] = static_cast<u8>(x);
        source[i * 4 + 1] = static_cast<u8>(255 - x);
        source[i * 4 + 2] = static_cast<u8>((x * 17 + y * 13) & 255);
        source[i * 4 + 3] = 255;
        destination[i * 4] = static_cast<u8>(y);
        destination[i * 4 + 1] = static_cast<u8>((y * 7 + x * 3) & 255);
        destination[i * 4 + 2] = static_cast<u8>(255 - y);
        destination[i * 4 + 3] = static_cast<u8>(x ^ y);
    }
    auto src = device->create_texture(PROBE_SIDE, PROBE_SIDE, source.data());
    auto dst = device->create_texture(PROBE_SIDE, PROBE_SIDE, destination.data());
    auto target = device->create_target(PROBE_SIDE, PROBE_SIDE);
    if (!src || !dst || !target) return 1;
    paint_detail::command_tape tape;
    if (!tape.prepare(COUNT, 0, COUNT * sizeof(paint_detail::command))) return 1;
    for (u32 i = 0; i < COUNT; ++i)
    {
        paint_detail::command c;
        if (!tape.append(c, {})) return 1;
    }
    vk_detail::gpu_bin_job job;
    if (!job.initialize(*d, tape, PROBE_SIDE, PROBE_SIDE, 32, 256, COUNT)) return 1;
    vk_detail::rgba8_blend_lut calibrated;
    if (native_lookup)
    {
        if (!calibrated.initialize(*d)) return 1;
        VkDescriptorBufferInfo info{calibrated.storage.buffers[1].buffer, 0,
                                    vk_detail::rgba8_blend_lut::BYTES};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = job.descriptor;
        write.dstBinding = 1;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.pBufferInfo = &info;
        d->vkUpdateDescriptorSets(d->native.device, 1, &write, 0, nullptr);
        mode |= 64;
        std::printf("native calibration %.3fms; table %u bytes; no CPU table readback\n",
                    calibrated.preparation_ms, vk_detail::rgba8_blend_lut::BYTES);
    }
    if (!lookup.empty())
    {
        if (gradient || std::filesystem::file_size(lookup) != 16842752) return 1;
        std::vector<u8> bytes(16842752);
        std::ifstream input(lookup, std::ios::binary);
        input.read(reinterpret_cast<char *>(bytes.data()), bytes.size());
        if (!input.good() || !job.upload_storage(1, bytes)) return 1;
        mode |= 64;
    }
    job.parameters[7] = mode;
    const VkPipeline pipeline =
        vk_detail::compute_pipeline(*d, job.layout, vulkan_blend_probe, sizeof(vulkan_blend_probe));
    if (!pipeline) return 1;
    std::vector<u32> alphas;
    if (gradient)
        alphas.push_back(255);
    else if (all_alpha)
        for (u32 i = 0; i < 256; ++i) alphas.push_back(i);
    else
        alphas = {0, 1, 2, 63, 127, 128, 129, 254, 255};
    u64 differences = 0, compared = 0;
    for (u32 alpha : alphas)
    {
        for (u32 i = 0; i < COUNT; ++i)
        {
            source[i * 4 + 3] = static_cast<u8>(alpha);
            auto &c = tape.commands[i];
            c.operation = packed(source.data() + i * 4);
            c.flags = packed(destination.data() + i * 4);
            if (gradient)
            {
                const f32 value = (static_cast<f32>(i % PROBE_SIDE) + 0.5f) / PROBE_SIDE;
                std::memcpy(&c.clip, &value, sizeof(value));
                c.compatibility = 1;
            }
        }
        device->update_texture(src, 0, 0, PROBE_SIDE, PROBE_SIDE, source.data());
        device->blit(dst, nullptr, target, nullptr);
        if (!device->read_pixels(target, 0, 0, PROBE_SIDE, PROBE_SIDE, expected.data()) ||
            expected != destination)
        {
            std::fprintf(stderr, "numeric setup: destination copy changed pixels\n");
            return 1;
        }
        device->set_target(target);
        const color left{255, 255, 255, static_cast<u8>(gradient ? 0 : 255)};
        const color right{255, 255, 255, 255};
        const vertex v[] = {{0, 0, 0, 0, left},
                            {PROBE_SIDE, 0, 1, 0, right},
                            {PROBE_SIDE, PROBE_SIDE, 1, 1, right},
                            {0, PROBE_SIDE, 0, 1, left}};
        const i32 indices[] = {0, 1, 2, 0, 2, 3};
        device->draw(src, v, 4, indices, 6);
        if (!device->read_pixels(target, 0, 0, PROBE_SIDE, PROBE_SIDE, expected.data())) return 1;
        std::memcpy(job.buffers[0].mapped, tape.commands.data(),
                    COUNT * sizeof(paint_detail::command));
        job.buffers[0].used = COUNT * sizeof(paint_detail::command);
        if (!d->flush_page(job.buffers[0]) || !d->commands()) return 1;
        d->end_rendering();
        d->vkCmdBindPipeline(d->frame->command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        d->vkCmdBindDescriptorSets(d->frame->command, VK_PIPELINE_BIND_POINT_COMPUTE, job.layout, 0,
                                   1, &job.descriptor, 0, nullptr);
        d->vkCmdPushConstants(d->frame->command, job.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                              sizeof(job.parameters), job.parameters);
        d->vkCmdDispatch(d->frame->command, (COUNT + 63) / 64, 1, 1);
        job.recorded = true;
        job.serial = d->next_serial;
        job.barrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
        job.serial = d->submit();
        if (!job.serial || !d->wait(job.serial)) return 1;
        if (!job.buffers[3].coherent)
        {
            VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
            range.memory = job.buffers[3].memory;
            range.size = VK_WHOLE_SIZE;
            if (!d->check(d->vkInvalidateMappedMemoryRanges(d->native.device, 1, &range),
                          "numeric invalidate"))
                return 1;
        }
        const auto *actual = reinterpret_cast<const u32 *>(job.buffers[3].mapped);
        u32 changed = 0;
        for (u32 i = 0; i < COUNT; ++i)
            if (actual[i] != packed(expected.data() + i * 4))
            {
                if (!differences && !changed)
                    std::printf("first alpha=%u pixel=(%u,%u) native=%08x compute=%08x\n", alpha,
                                i % PROBE_SIDE, i / PROBE_SIDE, packed(expected.data() + i * 4),
                                actual[i]);
                ++changed;
            }
        differences += changed;
        compared += COUNT;
        if (!dump.empty())
        {
            auto save = [&](const char *name, const void *data, usize bytes)
            {
                const auto path = dump / (std::to_string(alpha) + "-" + name + ".bin");
                std::ofstream output(path, std::ios::binary);
                output.write(static_cast<const char *>(data), bytes);
                return output.good();
            };
            if (!save("source", source.data(), source.size()) ||
                !save("destination", destination.data(), destination.size()) ||
                !save("native", expected.data(), expected.size()) ||
                !save("compute", actual, COUNT * sizeof(u32)))
                return 1;
        }
        std::printf("alpha=%u gradient=%d mode=%u mismatched_pixels=%u/%u\n", alpha, gradient, mode,
                    changed, COUNT);
        std::fflush(stdout);
    }
    job.release();
    calibrated.release();
    d->vkDestroyPipeline(d->native.device, pipeline, nullptr);
    device->destroy_texture(src);
    device->destroy_texture(dst);
    device->destroy_target(target);
    if (!vulkan_wait_idle(device) || errors || d->stats.validation_errors) return 1;
    destroy_vulkan_device(device);
    std::printf(
        "NUMERIC EXPERIMENT: %llu mismatched pixels / %llu; not a renderer acceptance result\n",
        static_cast<unsigned long long>(differences), static_cast<unsigned long long>(compared));
    return 0; // experimental measurements are data, never a golden-test pass claim
}
