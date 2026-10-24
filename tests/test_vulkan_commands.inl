// Private command ABI and scalar binning-oracle regressions. No GPU required.
#undef near
#undef far
#undef small
#include "test_util.h"

namespace
{
paint_detail::command packet(rect r = {0, 0, 16, 16})
{
    paint_detail::command c;
    c.operation = static_cast<u32>(paint_detail::opcode::RECT_COMPAT);
    c.x0 = r.x;
    c.y0 = r.y;
    c.x1 = r.right();
    c.y1 = r.bottom();
    return c;
}
} // namespace

PUI_TEST(command_abi_layout_and_full_float_bits)
{
    using namespace paint_detail;
    command_tape a, b;
    CHECK(a.prepare(1, 0, 64) && b.prepare(1, 0, 64));
    auto c = packet();
    c.x0 = -0.0f;
    CHECK(a.append(c, {}));
    c.x0 = 0.0f;
    CHECK(b.append(c, {}));
    CHECK(!same_content(a, 0, b, 0));
    b.commands[0].x0 = -0.0f;
    CHECK(same_content(a, 0, b, 0));
    CHECK(a.commands[0].serial_lo == 1 && a.commands[0].serial_hi == 0);
}
PUI_TEST(command_payload_is_owned_and_offsets_do_not_define_identity)
{
    using namespace paint_detail;
    command_tape a, b;
    CHECK(a.prepare(2, 8, 136) && b.prepare(2, 8, 136));
    u8 bytes[] = {1, 2, 3, 4};
    auto c = packet();
    CHECK(a.append(c, bytes));
    CHECK(b.append(packet({64, 64, 8, 8}), bytes));
    CHECK(b.append(c, bytes));
    bytes[3] = 5;
    CHECK(same_content(a, 0, b, 1));
    b.payload[7] = 5;
    CHECK(!same_content(a, 0, b, 1));
    CHECK(a.commands[0].payload_offset == 0 && b.commands[1].payload_offset == 4);
    CHECK(!same_content(a, 9, b, 1));
    b.commands[1].payload_offset = 0xffffffffu;
    CHECK(!same_content(a, 0, b, 1));
}
PUI_TEST(command_append_is_allocation_free_bounded_and_invalidates_overflow)
{
    using namespace paint_detail;
    command_tape tape;
    CHECK(tape.prepare(2, 8, 136));
    const auto *commands = tape.commands.data();
    const auto *payload = tape.payload.data();
    const u8 bytes[] = {1, 2, 3, 4};
    CHECK(tape.append(packet(), bytes));
    CHECK(tape.append(packet(), std::span<const u8>(tape.payload.data(), 4)));
    CHECK(commands == tape.commands.data() && payload == tape.payload.data());
    CHECK(tape.payload[7] == 4);
    CHECK(!tape.append(packet(), {}));
    CHECK(!tape.valid && tape.commands.size() == 2 && tape.payload.size() == 8);
    CHECK(!tape.append(packet(), {}));
    CHECK(tape.prepare(1, 0, 136) && tape.valid && tape.commands.empty());
    CHECK(tape.next_serial == 1);
    CHECK(!tape.prepare(1, static_cast<usize>(0xffffffffull) + 1, 0xffffffffffffffffull));
    CHECK(tape.valid && tape.command_limit == 1); // rejected preparation leaves old tape intact
}
PUI_TEST(command_corrupt_bounds_and_serial_wrap_cannot_publish_a_valid_tape)
{
    using namespace paint_detail;
    command_tape tape;
    CHECK(tape.prepare(1, 0, 64));
    auto c = packet();
    u32 nan_bits = 0x7fc00000u;
    std::memcpy(&c.x0, &nan_bits, sizeof(nan_bits));
    CHECK(!tape.append(c, {}) && !tape.valid);
    CHECK(tape.prepare(1, 0, 64));
    tape.next_serial = 0;
    CHECK(!tape.append(packet(), {}) && !tape.valid);
    CHECK(tape.prepare(1, 0, 64));
    c = packet();
    c.x1 = -1;
    CHECK(!tape.append(c, {}) && !tape.valid);
}
PUI_TEST(command_content_tracks_clip_resource_pass_and_compatibility)
{
    using namespace paint_detail;
    command_tape a, b;
    CHECK(a.prepare(1, 0, 64) && b.prepare(1, 0, 64));
    CHECK(a.append(packet(), {}) && b.append(packet(), {}));
    CHECK(same_content(a, 0, b, 0));
    u32 *fields[] = {&b.commands[0].resource, &b.commands[0].clip, &b.commands[0].pass,
                     &b.commands[0].compatibility};
    for (auto *field : fields)
    {
        *field = 9;
        CHECK(!same_content(a, 0, b, 0));
        *field = 0;
    }
}
PUI_TEST(command_reference_binning_is_ordered_and_conservative_at_tile_edges)
{
    using namespace paint_detail;
    command_tape tape;
    CHECK(tape.prepare(5, 0, 320));
    CHECK(tape.append(packet({-1, 0, 17, 16}), {}));
    CHECK(tape.append(packet({15.5f, 0, 1, 16}), {}));
    CHECK(tape.append(packet({16, 0, 16, 16}), {}));
    CHECK(tape.append(packet({0, 0, 0, 0}), {}));
    CHECK(tape.append(packet({32, 0, 8, 16}), {}));
    tile_lists result;
    CHECK(bin_reference(tape, 32, 16, 16, 4, result));
    CHECK(result.offsets == std::vector<u32>({0, 2, 4}));
    CHECK(result.indices == std::vector<u32>({0, 1, 1, 2}));
    CHECK(!bin_reference(tape, 32, 16, 16, 3, result));
    CHECK(result.indices.empty() && result.offsets.empty());
    CHECK(!bin_reference(tape, 32, 16, 0, 4, result));
    CHECK(!bin_reference(tape, 0xffffffffu, 0xffffffffu, 1, 4, result));
}
PUI_TEST(command_reference_binning_accounts_for_partial_edge_tiles_and_empty_program)
{
    using namespace paint_detail;
    command_tape tape;
    CHECK(tape.prepare(1, 0, 64));
    tile_lists result;
    CHECK(bin_reference(tape, 17, 17, 16, 0, result));
    CHECK(result.offsets == std::vector<u32>({0, 0, 0, 0, 0}));
    CHECK(tape.append(packet({16, 16, 1, 1}), {}));
    CHECK(bin_reference(tape, 17, 17, 16, 1, result));
    CHECK(result.offsets == std::vector<u32>({0, 0, 0, 0, 1}));
    CHECK(result.indices == std::vector<u32>({0}));
}
PUI_TEST(command_shadow_encoder_captures_visible_primitive_and_legacy_order)
{
    vk_detail::device owner;
    owner.options.command_budget_bytes = 1024 * 1024;
    owner.options.painter = vulkan_painter_mode::COMMAND_SHADOW;
    vk_detail::primitive_recorder recorder;
    recorder.owner = &owner;
    recorder.reset();
    tf_env env;
    env.frame(0,
              [&](ui &u)
              {
                  env.c->dl->painter = &recorder;
                  u.draw_rect({10, 20, 30, 40}, {1, 2, 3, 127});
                  u.draw_rounded_rect({50, 20, 60, 70}, {4, 5, 6, 200}, 8);
                  u.draw_rect({-100, -100, 2, 2}, {9, 9, 9, 255}); // culled scope must unwind
                  const vertex v[] = {{10, 10, 0, 0, {}}, {20, 10, 0, 0, {}}, {10, 20, 0, 0, {}}};
                  const i32 idx[] = {0, 1, 2};
                  u.draw_triangles(nullptr, v, 3, idx, 3);
              });
    CHECK(recorder.depth == 0 && recorder.tape.valid && recorder.tape.commands.size() == 3);
    if (recorder.tape.commands.size() != 3) return;
    CHECK(recorder.tape.commands[0].operation ==
          static_cast<u32>(paint_detail::opcode::RECT_COMPAT));
    CHECK(recorder.tape.commands[1].operation ==
          static_cast<u32>(paint_detail::opcode::ROUNDED_FAN_COMPAT));
    CHECK(recorder.tape.commands[2].operation ==
          static_cast<u32>(paint_detail::opcode::LEGACY_TRIANGLES));
    CHECK(recorder.tape.commands[0].x0 == 10 && recorder.tape.commands[0].y1 == 60);
    u32 meta[32];
    std::memcpy(meta, recorder.tape.payload.data(), sizeof(meta));
    CHECK(meta[0] == 4 && meta[1] == 6 && meta[2] == paint_detail::SCHEMA_VERSION);
    CHECK(meta[26] == (1u | (2u << 8) | (3u << 16) | (127u << 24)));
    vertex first;
    std::memcpy(&first, recorder.tape.payload.data() + sizeof(meta), sizeof(first));
    CHECK(first.x == 10 && first.y == 20 && first.c.a == 127);
}
PUI_TEST(command_shadow_encoder_owns_resource_versions_and_rejects_budget_overflow)
{
    vk_detail::device owner;
    owner.options.command_budget_bytes = 1024;
    owner.options.painter = vulkan_painter_mode::COMMAND_SHADOW;
    vk_detail::texture target, sample;
    target.generation = 0x100000005ull;
    sample.generation = 0x200000007ull;
    sample.revision = 0x300000009ull;
    owner.target = &target;
    owner.has_clip = true;
    owner.clip = {1.5f, -2, 30, 40};
    vk_detail::primitive_recorder recorder;
    recorder.owner = &owner;
    recorder.reset();
    const vertex v[] = {{0, 0, 0, 0, {}}, {16, 0, 0, 0, {}}, {0, 16, 0, 0, {}}};
    const i32 idx[] = {0, 1, 2};
    recorder.mesh(&sample, v, 3, idx, 3);
    CHECK(recorder.tape.valid && recorder.tape.commands.size() == 1);
    u32 meta[32];
    std::memcpy(meta, recorder.tape.payload.data(), sizeof(meta));
    CHECK(meta[4] == 5 && meta[5] == 1 && meta[8] == 7 && meta[9] == 2);
    CHECK(meta[10] == 9 && meta[11] == 3 && meta[13] == 1);
    sample.revision = 99;
    std::memcpy(meta, recorder.tape.payload.data(), sizeof(meta));
    CHECK(meta[10] == 9); // caller's later revision cannot mutate a recorded dependency
    owner.options.command_budget_bytes = 0;
    recorder.reset();
    recorder.mesh(&sample, v, 3, idx, 3);
    CHECK(!recorder.tape.valid && owner.stats.shadow_overflows == 1);
}
PUI_TEST(command_overflow_consumes_shape_marker_before_the_next_batch)
{
    vk_detail::device owner;
    owner.options.painter = vulkan_painter_mode::COMPUTE_SDF;
    owner.options.gpu_command_budget_bytes = sizeof(paint_detail::command);
    vk_detail::primitive_recorder recorder;
    recorder.owner = &owner;
    recorder.reset();
    const vertex vertices[] = {{0, 0, 0, 0, {}}, {16, 0, 0, 0, {}}, {0, 16, 0, 0, {}}};
    const i32 indices[] = {0, 1, 2};
    for (u32 shape = 0; shape < 3; ++shape)
    {
        recorder.pending_shape = shape;
        recorder.mesh(nullptr, vertices, 3, indices, 3);
        CHECK(recorder.pending_shape == 0xffffffffu);
    }
    CHECK(!owner.sdf_tape.valid);
    CHECK(owner.sdf_tape.prepare(1, 0, sizeof(paint_detail::command)));
    recorder.enter({.operation = paint_detail::opcode::RECT_COMPAT});
    recorder.mesh(nullptr, vertices, 3, indices, 3);
    recorder.leave();
    CHECK(owner.sdf_tape.valid && owner.sdf_tape.commands.size() == 1);
    CHECK(owner.sdf_tape.commands[0].operation == 1);
}
int main()
{
    for (const auto &test : test_registry()) test.fn();
    if (!g_failures)
        std::printf("all GPU command regressions passed (%zu tests)\n", test_registry().size());
    return g_failures ? 1 : 0;
}
