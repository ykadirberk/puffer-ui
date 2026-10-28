// Scalar reference of the original ordered admission contract. Compare actual
// GPU keys, FIFO stamps, ready flags, request decisions and emitted row order.
#include "vulkan_test_options.h"
#include <array>
#include <bit>

namespace
{
using admission_key = std::array<u32, 4>;
using admission_request = std::array<u32, 8>;
struct admission_row
{
    admission_key key{};
    u32 stamp = 0, ready = 0;
};
u32 admission_hash(const admission_key &key)
{
    u32 h = 0x811c9dc5u;
    for (u32 word : key)
    {
        h = (h ^ word) * 0x01000193u;
        h ^= h >> 16;
    }
    return h;
}
std::vector<u32> scalar_admission(std::vector<admission_row> &rows,
                                  std::vector<admission_request> &requests, u32 epoch)
{
    const u32 sets = static_cast<u32>(rows.size()) / 8;
    for (u32 lane = 0; lane < 64; ++lane)
        for (auto &request : requests)
        {
            admission_key key{request[0], request[1], request[2], request[3]};
            const u32 set = admission_hash(key) & (sets - 1);
            if ((set & 63) != lane) continue;
            u32 chosen = UINT32_MAX, age = 0;
            bool present = false;
            for (u32 way = 0; way < 8; ++way)
            {
                const u32 index = set * 8 + way;
                const auto &row = rows[index];
                if (row.ready && row.key == key)
                {
                    present = true;
                    break;
                }
                if (!row.ready)
                {
                    chosen = index;
                    age = UINT32_MAX;
                }
                else if (row.stamp != epoch && (chosen == UINT32_MAX || epoch - row.stamp > age))
                {
                    chosen = index;
                    age = epoch - row.stamp;
                }
            }
            request[5] = 0;
            if (present || chosen == UINT32_MAX) continue;
            rows[chosen] = {key, epoch, 2};
            request[4] = chosen;
            request[5] = 1;
        }
    std::vector<u32> order;
    for (u32 lane = 0; lane < 64; ++lane)
        for (const auto &request : requests)
        {
            admission_key key{request[0], request[1], request[2], request[3]};
            if (request[5] && ((admission_hash(key) & (sets - 1)) & 63) == lane)
                order.push_back(request[4]);
        }
    for (u32 index : order) rows[index].ready = 1;
    return order;
}
void check_function_admission(u32 row_count)
{
    auto options = test_detail::reference_options();
    options.validation = true;
    options.upload_mode = locality_staged ? vulkan_upload_mode::STAGED : vulkan_upload_mode::DIRECT;
    options.error_callback = [](i32 code, const char *, void *) { CHECK(code == 0); };
    auto *base = create_vulkan_device(options);
    CHECK(base);
    if (!base) return;
    auto &d = *static_cast<vk_detail::device *>(base);
    const auto update = reinterpret_cast<PFN_vkCmdUpdateBuffer>(
        d.vkGetDeviceProcAddr(d.native.device, "vkCmdUpdateBuffer"));
    CHECK(update);
    if (!update)
    {
        destroy_vulkan_device(base);
        return;
    }
    {
        vk_detail::native_function_cache cache;
        const bool initialized = cache.initialize(d, row_count, 256);
        CHECK(initialized && cache.valid);
        if (initialized)
        {
            vk_detail::page readback;
            auto &arena = cache.storage.buffers[0];
            const bool mapped =
                d.allocate_buffer(readback, arena.capacity, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
            CHECK(mapped);
            if (mapped)
            {
                std::vector<admission_row> rows(row_count);
                u32 epoch = 0;
                const u32 request_base = 32 + row_count * 8;
                const u32 list_base = request_base + 256 * 40;
                for (u32 requested :
                     {0u, 1u, 31u, 32u, 33u, 63u, 64u, 65u, 127u, 128u, 255u, 256u, 257u})
                {
                    const u32 count = std::min(requested, 256u);
                    std::vector<admission_request> requests(count);
                    for (u32 index = 0; index < count; ++index)
                    {
                        const u32 seed = (index % 5 ? index : index / 5) + (epoch % 3) * 37;
                        const f32 channels[] = {static_cast<f32>((seed * 17) % 251) / 255,
                                                static_cast<f32>((seed * 43) % 251) / 255,
                                                static_cast<f32>((seed * 97) % 251) / 255,
                                                static_cast<f32>(seed % 7 + 1) / 8};
                        for (u32 channel = 0; channel < 4; ++channel)
                            requests[index][channel] = std::bit_cast<u32>(channels[channel]);
                    }
                    const auto inputs = requests;
                    const auto order = scalar_admission(rows, requests, ++epoch);
                    CHECK(cache.record_begin());
                    cache.barrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                  VK_ACCESS_2_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                  VK_ACCESS_2_TRANSFER_WRITE_BIT);
                    update(d.frame->command, arena.buffer, 0, 4, &requested);
                    if (count)
                        update(d.frame->command, arena.buffer, request_base * 4,
                               count * sizeof(admission_request), inputs.data());
                    cache.barrier(VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                  VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
                    CHECK(cache.record_prepare());
                    cache.barrier(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                  VK_ACCESS_2_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                  VK_ACCESS_2_TRANSFER_READ_BIT);
                    const VkBufferCopy copy{0, 0, arena.capacity};
                    d.vkCmdCopyBuffer(d.frame->command, arena.buffer, readback.buffer, 1, &copy);
                    const u64 serial = d.submit();
                    CHECK(serial && d.wait(serial));
                    if (!readback.coherent)
                    {
                        VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
                        range.memory = readback.memory;
                        range.size = VK_WHOLE_SIZE;
                        CHECK(d.vkInvalidateMappedMemoryRanges(d.native.device, 1, &range) ==
                              VK_SUCCESS);
                    }
                    const auto *words = reinterpret_cast<const u32 *>(readback.mapped);
                    CHECK(words[4] == epoch && words[5] == order.size());
                    CHECK(words[17] == order.size() && words[21] == order.size());
                    for (u32 index = 0; index < row_count; ++index)
                    {
                        const u32 at = 32 + index * 8;
                        for (u32 channel = 0; channel < 4; ++channel)
                            CHECK(words[at + channel] == rows[index].key[channel]);
                        CHECK(words[at + 4] == rows[index].stamp);
                        CHECK(words[at + 5] == rows[index].ready);
                    }
                    for (u32 index = 0; index < count; ++index)
                        for (u32 word = 0; word < 8; ++word)
                            CHECK(words[request_base + index * 8 + word] == requests[index][word]);
                    for (usize index = 0; index < order.size(); ++index)
                        CHECK(words[list_base + index] == order[index]);
                    CHECK(!vulkan_statistics(base).validation_errors);
                }
                d.free_page(readback);
            }
        }
    }
    destroy_vulkan_device(base);
}
} // namespace

PUI_TEST(function_admission_matches_scalar_fifo_at_chunk_capacity_and_collision_boundaries)
{
    for (u32 rows : {8u, 64u, 1024u}) check_function_admission(rows);
}
