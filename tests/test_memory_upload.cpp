// Reuse the existing COM fake and execute its original ownership regressions too.
#include "test_sdl_upload.cpp"

PUI_TEST(memory_large_upload_capacity_is_bounded_and_reused)
{
    CHECK(pui_d3d11_upload_capacity(0, 0) == 0);
    CHECK(pui_d3d11_upload_capacity(65536, 65537) == 262144);
    CHECK(pui_d3d11_upload_capacity(0, 4194304) == pui_d3d11_vertex_capacity(0, 4194304));
    for (usize bytes : {4194305u, 14551104u, 46950464u, 67910784u, 87888768u, 95539552u})
    {
        const usize capacity = pui_d3d11_upload_capacity(0, bytes);
        CHECK(capacity >= bytes + bytes / 4 && capacity <= bytes + bytes / 4 + 65535);
        CHECK(capacity % 65536 == 0);
        CHECK(pui_d3d11_upload_capacity(capacity, bytes) == capacity);
        CHECK(pui_d3d11_upload_capacity(capacity, 192) == capacity);
    }
    CHECK(pui_d3d11_upload_capacity(0, UINT_MAX) == UINT_MAX);
    const usize rounding_edge = static_cast<usize>(UINT_MAX / 5) * 4;
    CHECK(pui_d3d11_upload_capacity(0, rounding_edge) == rounding_edge);
    CHECK(pui_d3d11_upload_capacity(UINT_MAX, 1) == UINT_MAX);
    if (sizeof(usize) > sizeof(unsigned))
    {
        CHECK(pui_d3d11_upload_capacity(0, static_cast<usize>(UINT_MAX) + 1) == 0);
        CHECK(pui_d3d11_upload_capacity(static_cast<usize>(UINT_MAX) + 1, 1) == 0);
    }
}

PUI_TEST(memory_large_upload_failure_keeps_resource_cursor_and_exact_fallback)
{
    using namespace fake_d3d;
    ID3D11Device device;
    ID3D11DeviceContext dc;
    D3D11_RenderData data{&device, &dc};
    SDL_Renderer renderer{&data};
    std::vector<u8> bytes(4194305, 0x73);
    device.fail_above = bytes.size();
    CHECK(D3D11_UpdateVertexBuffer(&renderer, bytes.data(), bytes.size()));
    CHECK(data.vertexBufferSizes[6] == bytes.size());
    CHECK(data.puiLargeVertexCursor == 1);
    CHECK(std::memcmp(dc.bound->bytes.data(), bytes.data(), bytes.size()) == 0);
    device.fail_all = true;
    CHECK(!D3D11_UpdateVertexBuffer(&renderer, bytes.data(), bytes.size()));
    CHECK(data.vertexBuffers[7] == nullptr && data.puiLargeVertexCursor == 1);
    device.fail_all = false;
    dc.fail_map = true;
    CHECK(!D3D11_UpdateVertexBuffer(&renderer, bytes.data(), bytes.size()));
    CHECK(data.vertexBuffers[7] == nullptr && data.puiActiveVertexBuffer == 6);
    CHECK(data.puiLargeVertexCursor == 1);
    for (auto *buffer : data.vertexBuffers) delete buffer;
}

namespace memory_arena
{
struct SDL_Renderer
{
    void *vertex_data = nullptr;
    usize vertex_data_allocation = 0, vertex_data_used = 0;
    ~SDL_Renderer() { std::free(vertex_data); }
};
static bool fail_malloc, fail_realloc;
static usize copied, mallocs, reallocs;
static void *SDL_malloc(usize bytes)
{
    ++mallocs;
    if (fail_malloc) return nullptr;
    void *p = std::malloc(bytes);
    if (p) std::memset(p, 0xcd, bytes);
    return p;
}
static void *SDL_realloc(void *p, usize bytes)
{
    ++reallocs;
    return fail_realloc ? nullptr : std::realloc(p, bytes);
}
static void SDL_free(void *p)
{
    std::free(p);
}
static void SDL_memcpy(void *dst, const void *src, usize bytes)
{
    copied += bytes;
    std::memcpy(dst, src, bytes);
}
static void *SDL_AllocateRenderVertices(SDL_Renderer *r, usize bytes, usize alignment, usize *out)
{
    CHECK(r->vertex_data_allocation >= r->vertex_data_used + bytes + alignment);
    const usize first =
        alignment ? (r->vertex_data_used + alignment - 1) & ~(alignment - 1) : r->vertex_data_used;
    if (out) *out = first;
    r->vertex_data_used = first + bytes;
    return static_cast<u8 *>(r->vertex_data) + first;
}
#include "../src/sdl_d3d11_arena.inl"

PUI_TEST(memory_arena_copies_only_live_prefix_and_preserves_offsets)
{
    for (usize used : {0u, 3u, 37u, 3680u})
    {
        SDL_Renderer r;
        r.vertex_data = std::malloc(4096);
        r.vertex_data_allocation = 4096;
        r.vertex_data_used = used;
        std::memset(r.vertex_data, 0x79, 4096);
        copied = mallocs = reallocs = 0;
        usize offset = 999;
        void *p = PUI_D3D11_AllocateRenderVertices(&r, 5000, 32, &offset);
        CHECK(p && copied == used && mallocs == 1 && reallocs == 0);
        CHECK(offset == (used + 31) / 32 * 32);
        CHECK(p == static_cast<u8 *>(r.vertex_data) + offset);
        for (usize i = 0; i < used; ++i) CHECK(static_cast<u8 *>(r.vertex_data)[i] == 0x79);
        CHECK(static_cast<u8 *>(r.vertex_data)[used] == 0xcd);
        const void *storage = r.vertex_data;
        CHECK(PUI_D3D11_AllocateRenderVertices(&r, 3, 32, nullptr));
        CHECK(r.vertex_data == storage && mallocs == 1 && copied == used);
    }
}

PUI_TEST(memory_arena_fallback_and_failure_preserve_live_commands)
{
    SDL_Renderer r;
    r.vertex_data = std::malloc(1024);
    r.vertex_data_allocation = 1024;
    r.vertex_data_used = 73;
    std::memset(r.vertex_data, 0x65, 1024);
    fail_malloc = fail_realloc = true;
    void *old = r.vertex_data;
    usize offset = 987;
    CHECK(!PUI_D3D11_AllocateRenderVertices(&r, 4096, 32, &offset));
    CHECK(r.vertex_data == old && r.vertex_data_allocation == 1024 && r.vertex_data_used == 73);
    CHECK(offset == 987);
    fail_realloc = false;
    CHECK(PUI_D3D11_AllocateRenderVertices(&r, 4096, 32, &offset));
    CHECK(offset == 96 && r.vertex_data_used == 4192);
    for (usize i = 0; i < 73; ++i) CHECK(static_cast<u8 *>(r.vertex_data)[i] == 0x65);
    fail_malloc = false;
}

PUI_TEST(memory_arena_capacity_rejects_overflow_without_allocating)
{
    const usize limit = static_cast<usize>(-1);
    CHECK(pui_d3d11_arena_capacity(0, 0, 1, 32) == 1024);
    CHECK(pui_d3d11_arena_capacity(4096, 37, 5000, 32) == 8192);
    CHECK(pui_d3d11_arena_capacity(4096, 37, 1, 32) == 4096);
    CHECK(pui_d3d11_arena_capacity(8, 9, 0, 0) == 0);
    CHECK(pui_d3d11_arena_capacity(limit, limit - 1, 2, 0) == 0);
    CHECK(pui_d3d11_arena_capacity(limit, limit - 1, 1, 1) == 0);
    CHECK(pui_d3d11_arena_capacity(limit / 2 + 1, 0, limit - 32, 32) == limit);
}
} // namespace memory_arena
