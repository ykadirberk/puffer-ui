// Private, SDK-free GPU painter command ABI and bounded frame storage.
// Included inside namespace pui by the single implementation translation unit.
// Command contents are immutable after append; offsets are not content identity.
// No primitive uses this experimental tape until its encoder is enabled.
#if defined(PUFFERUI_ENABLE_VULKAN)
namespace paint_detail
{
inline bool sdf_painter(vulkan_painter_mode mode)
{
    return mode == vulkan_painter_mode::SDF_RASTER || mode == vulkan_painter_mode::COMPUTE_SDF ||
           mode == vulkan_painter_mode::INTEGRATED;
}
inline bool compute_painter(vulkan_painter_mode mode)
{
    return mode == vulkan_painter_mode::COMPUTE_SDF || mode == vulkan_painter_mode::INTEGRATED;
}
inline constexpr u32 SCHEMA_VERSION = 1;
enum class opcode : u32
{
    LEGACY_TRIANGLES,
    RECT_COMPAT,
    ROUNDED_FAN_COMPAT,
    RING_COMPAT,
    GLYPH_QUAD_COMPAT,
    IMAGE_QUAD_COMPAT,
    CLEAR,
    COPY,
    ROUNDED_GRADIENT
};
struct alignas(16) command
{
    u32 operation = 0, flags = 0, serial_lo = 0, serial_hi = 0;
    f32 x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    u32 payload_offset = 0, payload_size = 0, resource = 0, clip = 0;
    u32 ordinal_lo = 0, ordinal_hi = 0, pass = 0, compatibility = 0;
};
static_assert(sizeof(command) == 64 && alignof(command) == 16);
static_assert(offsetof(command, x0) == 16 && offsetof(command, payload_offset) == 32 &&
              offsetof(command, ordinal_lo) == 48);

// Capacity is prepared before recording, so append never relocates an arena or
// allocates. Exceeding either limit invalidates the whole tape, not just one draw.
struct command_tape
{
    std::vector<command> commands;
    std::vector<u8> payload;
    usize command_limit = 0, payload_limit = 0;
    u64 next_serial = 1;
    bool valid = false;

    bool prepare(usize count, usize bytes, u64 budget)
    {
        constexpr u64 MAX_OFFSET = 0xffffffffull;
        if (bytes > MAX_OFFSET || count > MAX_OFFSET || count > budget / sizeof(command) ||
            bytes > budget - count * sizeof(command) || count > commands.max_size() ||
            bytes > payload.max_size())
            return false;
        // Release old arenas before replacing them, rather than retaining a
        // transient second copy of a large previous recording during growth.
        if (commands.capacity() < count || payload.capacity() < bytes ||
            static_cast<u64>(commands.capacity()) * sizeof(command) > budget ||
            payload.capacity() > budget - static_cast<u64>(commands.capacity()) * sizeof(command))
        {
            std::vector<command>().swap(commands);
            std::vector<u8>().swap(payload);
            try
            {
                commands.reserve(count);
                payload.reserve(bytes);
            }
            catch (const std::bad_alloc &)
            {
                std::vector<command>().swap(commands);
                std::vector<u8>().swap(payload);
                valid = false;
                return false;
            }
        }
        const u64 allocated =
            static_cast<u64>(commands.capacity()) * sizeof(command) + payload.capacity();
        if (allocated > budget)
        {
            std::vector<command>().swap(commands);
            std::vector<u8>().swap(payload);
            valid = false;
            return false;
        }
        command_limit = count;
        payload_limit = bytes;
        commands.clear();
        payload.clear();
        next_serial = 1;
        valid = true;
        return true;
    }
    bool append(command c, std::span<const u8> data) { return append_parts(c, {data}); }
    bool append_parts(command c, std::initializer_list<std::span<const u8>> parts)
    {
        if (!valid) return false;
        usize bytes = 0;
        for (auto part : parts)
        {
            if (part.size() > payload_limit - bytes)
            {
                valid = false;
                return false;
            }
            bytes += part.size();
        }
        if (!next_serial || commands.size() >= command_limit ||
            bytes > payload_limit - payload.size() || !std::isfinite(c.x0) ||
            !std::isfinite(c.y0) || !std::isfinite(c.x1) || !std::isfinite(c.y1) || c.x1 < c.x0 ||
            c.y1 < c.y0)
        {
            valid = false;
            return false;
        }
        c.serial_lo = static_cast<u32>(next_serial);
        c.serial_hi = static_cast<u32>(next_serial >> 32);
        c.payload_offset = static_cast<u32>(payload.size());
        c.payload_size = static_cast<u32>(bytes);
        const usize first = payload.size();
        payload.resize(first + bytes);
        usize at = first;
        for (auto part : parts)
        {
            if (!part.empty()) std::memmove(payload.data() + at, part.data(), part.size());
            at += part.size();
        }
        commands.push_back(c);
        ++next_serial;
        return true;
    }
    bool payload_valid(const command &c) const
    {
        return c.payload_offset <= payload.size() &&
               c.payload_size <= payload.size() - c.payload_offset;
    }
};

struct primitive_input
{
    opcode operation = opcode::LEGACY_TRIANGLES;
    rect shape{};
    corner_radii radii{};
    color tint{};
    f32 scale = 1, disabled_alpha = 1;
    color corners[4]{};
};
struct primitive_sink
{
    virtual ~primitive_sink() = default;
    virtual void enter(const primitive_input &) = 0;
    virtual void leave() = 0;
    virtual void mesh(texture_handle, const vertex *, i32, const i32 *, i32) = 0;
    virtual bool shape(context *, const primitive_input &) { return false; }
    virtual bool vector_text() const { return false; }
    virtual bool glyph(context *, font_handle, u32, vec2, f32, color) { return false; }
};
primitive_sink *vulkan_primitive_sink(render_device *device);
bool vulkan_vector_text(render_device *device);
struct blur_result
{
    texture_handle image = nullptr;
    f32 scale_u = 0, scale_v = 0;
};
bool vulkan_blur(render_device *, texture_handle, rect, f32, blur_result &);

inline bool same_content(const command_tape &a, usize ai, const command_tape &b, usize bi)
{
    if (!a.valid || !b.valid || ai >= a.commands.size() || bi >= b.commands.size()) return false;
    command x = a.commands[ai], y = b.commands[bi];
    if (!a.payload_valid(x) || !b.payload_valid(y)) return false;
    const u32 ax = x.payload_offset, by = y.payload_offset;
    // Sequence is represented by ordered tile lists. Unrelated preceding
    // commands and physical arena placement are not semantic differences.
    x.serial_lo = x.serial_hi = x.ordinal_lo = x.ordinal_hi = x.payload_offset = 0;
    y.serial_lo = y.serial_hi = y.ordinal_lo = y.ordinal_hi = y.payload_offset = 0;
    if (std::memcmp(&x, &y, sizeof(command)) != 0) return false;
    return !x.payload_size ||
           std::memcmp(a.payload.data() + ax, b.payload.data() + by, x.payload_size) == 0;
}

// Independent scalar oracle for future GPU fine lists. Input bounds already
// include conservative coverage/clip. IDs are emitted in original tape order.
struct tile_lists
{
    std::vector<u32> offsets, indices;
};
inline bool bin_reference(const command_tape &tape, u32 width, u32 height, u32 tile,
                          u64 index_limit, tile_lists &result)
{
    result.offsets.clear();
    result.indices.clear();
    if (!tape.valid || !tile || !width || !height) return false;
    const u64 columns = (static_cast<u64>(width) + tile - 1) / tile;
    const u64 rows = (static_cast<u64>(height) + tile - 1) / tile;
    const u64 count = columns * rows;
    constexpr u64 ORACLE_BUDGET = 64ull * 1024 * 1024;
    if (count >= 0xffffffffull || index_limit > 0xffffffffull ||
        count + 1 > ORACLE_BUDGET / sizeof(u32) ||
        index_limit > ORACLE_BUDGET / sizeof(u32) - (count + 1))
        return false;
    result.offsets.reserve(static_cast<usize>(count + 1));
    for (u64 ty = 0; ty < rows; ++ty)
        for (u64 tx = 0; tx < columns; ++tx)
        {
            result.offsets.push_back(static_cast<u32>(result.indices.size()));
            const u64 left = tx * tile, top = ty * tile;
            const u64 right = left + tile < width ? left + tile : width;
            const u64 bottom = top + tile < height ? top + tile : height;
            for (usize i = 0; i < tape.commands.size(); ++i)
            {
                const auto &c = tape.commands[i];
                if (c.x0 >= c.x1 || c.y0 >= c.y1 || static_cast<double>(c.x1) <= left ||
                    static_cast<double>(c.x0) >= right || static_cast<double>(c.y1) <= top ||
                    static_cast<double>(c.y0) >= bottom)
                    continue;
                if (result.indices.size() >= index_limit)
                {
                    result.offsets.clear();
                    result.indices.clear();
                    return false;
                }
                result.indices.push_back(static_cast<u32>(i));
            }
        }
    result.offsets.push_back(static_cast<u32>(result.indices.size()));
    return true;
}
} // namespace paint_detail
#endif
