// Compile the real queue and dispatch slices with portable SDL/COM fakes.
// Compare every indexed shader input to the original expanded vertex stream.
#include "test_util.h"
#include "../src/sdl_d3d11_buffer.h"

namespace fake_geometry
{
using Uint8 = u8;
using Uint16 = u16;
using Uint32 = u32;
using INT = int;
struct SDL_FColor
{
    float r, g, b, a;
};
struct D3D11_VertexPositionColor
{
    struct
    {
        float x, y;
    } pos, tex;
    SDL_FColor color;
};
struct D3D11_TextureData
{
    float w, h;
};
struct SDL_Texture
{
    int w, h;
    void *internal;
};
struct SDL_Renderer
{
    usize vertex_data_used = 0;
    bool linear = false, fail_alloc = false;
    int copy_states = 0, draw_states = 0;
    std::vector<u8> bytes;
};
struct SDL_RenderCommand
{
    struct
    {
        struct
        {
            usize first = 0;
            int count = 0;
            SDL_Texture *texture = nullptr;
        } draw;
    } data;
};
static bool SDL_RenderingLinearSpace(SDL_Renderer *r)
{
    return r->linear;
}
static void SDL_ConvertToLinear(SDL_FColor *c)
{
    c->r *= c->r;
    c->g *= c->g;
    c->b *= c->b;
}
static bool SDL_SetError(const char *)
{
    return false;
}
static void *SDL_AllocateRenderVertices(SDL_Renderer *r, usize bytes, usize align, usize *offset)
{
    if (r->fail_alloc) return nullptr;
    *offset = (r->vertex_data_used + align - 1) & ~(align - 1);
    r->vertex_data_used = *offset + bytes;
    r->bytes.resize(r->vertex_data_used, 0xcd);
    return r->bytes.data() + *offset;
}
#define SDL_zero(x) std::memset(&(x), 0, sizeof(x))
#include "../src/sdl_d3d11_geometry.inl"
#undef SDL_zero

struct fake_context
{
    int topology = 0, buffer = -1;
    Uint32 count = 0, first = 0;
    INT base = 0;
};
struct fake_data
{
    fake_context *d3dContext;
    int vertexBuffers[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    int puiActiveVertexBuffer = 7;
};
constexpr int D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST = 4, DXGI_FORMAT_R32_UINT = 42;
static void D3D11_SetCopyState(SDL_Renderer *r, SDL_RenderCommand *, void *)
{
    ++r->copy_states;
}
static void D3D11_SetDrawState(SDL_Renderer *r, SDL_RenderCommand *, void *, int, void *, int,
                               void *, void *)
{
    ++r->draw_states;
}
static void ID3D11DeviceContext_IASetPrimitiveTopology(fake_context *c, int t)
{
    c->topology = t;
}
static void ID3D11DeviceContext_IASetIndexBuffer(fake_context *c, int b, int fmt, int offset)
{
    CHECK(fmt == DXGI_FORMAT_R32_UINT && offset == 0);
    c->buffer = b;
}
static void ID3D11DeviceContext_DrawIndexed(fake_context *c, Uint32 n, Uint32 first, INT base)
{
    c->count = n;
    c->first = first;
    c->base = base;
}
static void dispatch(SDL_Renderer *renderer, SDL_RenderCommand *cmd, const void *vertices,
                     fake_data *rendererData)
{
    switch (0)
    {
    case 0:
#include "../src/sdl_d3d11_draw.inl"
    }
}

PUI_TEST(sdl_d3d11_indexed_stream_matches_original_expansion)
{
    struct input
    {
        float x, y, unused, u, v;
        SDL_FColor color;
    };
    const input data[] = {{2, 3, 99, .2f, .7f, {.1f, .2f, .4f, .6f}},
                          {5, 9, 98, .4f, .3f, {.9f, .8f, .7f, .6f}},
                          {8, 4, 97, .6f, .9f, {.3f, .5f, .7f, .9f}},
                          {7, 1, 96, .8f, .1f, {.8f, .4f, .2f, .5f}}};
    const Uint32 i32s[] = {2, 0, 1, 2, 1, 3};
    const Uint16 i16s[] = {2, 0, 1, 2, 1, 3};
    const Uint8 i8s[] = {2, 0, 1, 2, 1, 3};
    D3D11_TextureData td{16, 32};
    SDL_Texture texture{13, 29, &td};
    for (bool textured : {false, true})
        for (bool linear : {false, true})
            for (int width : {0, 1, 2, 4})
                for (usize prefix : {0u, 3u, 128u})
                {
                    SDL_Renderer r;
                    r.vertex_data_used = prefix;
                    r.linear = linear;
                    SDL_RenderCommand cmd;
                    cmd.data.draw.texture = textured ? &texture : nullptr;
                    const void *indices = width == 4   ? static_cast<const void *>(i32s)
                                          : width == 2 ? static_cast<const void *>(i16s)
                                          : width == 1 ? static_cast<const void *>(i8s)
                                                       : nullptr;
                    CHECK(D3D11_QueueGeometry(&r, &cmd, cmd.data.draw.texture, &data[0].x,
                                              sizeof(input), &data[0].color, sizeof(input),
                                              &data[0].u, sizeof(input), 4, indices, 6, width,
                                              1.75f, .625f));
                    const auto *record = reinterpret_cast<const PUI_D3D11_Geometry *>(
                        r.bytes.data() + cmd.data.draw.first);
                    CHECK(sizeof(*record) == 32 && cmd.data.draw.first % 32 == 0);
                    CHECK(record->vertex_offset % 32 == 0 && record->index_offset % 4 == 0);
                    const auto *vertices = reinterpret_cast<const D3D11_VertexPositionColor *>(
                        r.bytes.data() + record->vertex_offset);
                    const auto *out =
                        reinterpret_cast<const Uint32 *>(r.bytes.data() + record->index_offset);
                    CHECK(cmd.data.draw.count == (indices ? 6 : 4));
                    for (int i = 0; i < cmd.data.draw.count; ++i)
                    {
                        const Uint32 j = indices ? i32s[i] : static_cast<Uint32>(i);
                        D3D11_VertexPositionColor expected;
                        expected.pos.x = data[j].x * 1.75f;
                        expected.pos.y = data[j].y * .625f;
                        expected.color = data[j].color;
                        if (linear) SDL_ConvertToLinear(&expected.color);
                        expected.tex.x = textured ? data[j].u * (13.f / 16.f) : 0;
                        expected.tex.y = textured ? data[j].v * (29.f / 32.f) : 0;
                        CHECK(out[i] == j);
                        CHECK(std::memcmp(&vertices[out[i]], &expected, sizeof(expected)) == 0);
                    }
                    fake_context dc;
                    fake_data gpu{&dc};
                    dispatch(&r, &cmd, r.bytes.data(), &gpu);
                    CHECK(r.copy_states == (textured ? 1 : 0));
                    CHECK(r.draw_states == (textured ? 0 : 1));
                    CHECK(dc.buffer == 7 && dc.topology == D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    CHECK(dc.count == record->index_count && dc.first * 4 == record->index_offset);
                    CHECK(dc.base * 32 == record->vertex_offset);
                }
}

PUI_TEST(sdl_d3d11_geometry_bounds_failures_and_shared_storage)
{
    CHECK(pui_d3d11_upload_slot(1048576, 4) == 4);
    CHECK(pui_d3d11_upload_slot(1048577, 4) == 5);
    CHECK(pui_d3d11_upload_slot(4194304, 4) == 5);
    CHECK(pui_d3d11_upload_slot(4194305, 4) == 6);
    CHECK(pui_d3d11_geometry_bytes(-1, 3, 0) == 0);
    CHECK(pui_d3d11_geometry_bytes(3, -1, 0) == 0);
    CHECK(pui_d3d11_geometry_bytes(INT_MAX, 3, 0) == 0);
    CHECK(pui_d3d11_geometry_bytes(3, INT_MAX, 0) == 0);
    CHECK(pui_d3d11_geometry_bytes(3, 3, UINT_MAX - 31u) == 0);
    CHECK(pui_d3d11_geometry_bytes(3, 3, static_cast<usize>(-1)) == 0);
    // Peak shared topology is substantially smaller than expanded vertices.
    const usize bytes = pui_d3d11_geometry_bytes(1963939, 8351460, 0);
    CHECK(bytes > 0 && bytes < 100000000 && bytes < 8351460ull * 32 / 2);
    SDL_Renderer r;
    SDL_RenderCommand cmd;
    r.fail_alloc = true;
    CHECK(!D3D11_QueueGeometry(&r, &cmd, nullptr, nullptr, 8, nullptr, 16, nullptr, 8, 3, nullptr,
                               0, 0, 1, 1));
    CHECK(r.vertex_data_used == 0 && cmd.data.draw.count == 0);
    r.fail_alloc = false;
    Uint8 index = 0;
    CHECK(!D3D11_QueueGeometry(&r, &cmd, nullptr, nullptr, 8, nullptr, 16, nullptr, 8, 3, &index, 3,
                               3, 1, 1));
    CHECK(r.vertex_data_used == 0);
    CHECK(D3D11_QueueGeometry(&r, &cmd, nullptr, nullptr, 8, nullptr, 16, nullptr, 8, 0, nullptr, 0,
                              0, 1, 1));
    CHECK(cmd.data.draw.count == 0 && r.vertex_data_used == 32);
}
} // namespace fake_geometry
