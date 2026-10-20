// Exercise the actual SDL build-overlay upload function with a small COM fake.
// This runs on every platform and checks failure/retry/ownership, not a copy of it.
#include "test_util.h"
#include "../src/sdl_d3d11_buffer.h"

namespace fake_d3d
{
using UINT = unsigned;
using HRESULT = int;
struct D3D11_BUFFER_DESC
{
    UINT ByteWidth, Usage, BindFlags, CPUAccessFlags;
};
struct D3D11_VertexPositionColor
{
    float data[8];
};
struct ID3D11Buffer
{
    std::vector<u8> bytes;
    explicit ID3D11Buffer(usize size) : bytes(size, 0xcd) {}
};
using ID3D11Resource = ID3D11Buffer;
struct ID3D11Device
{
    i32 creates = 0;
    usize fail_above = 0;
    bool fail_all = false;
};
struct ID3D11DeviceContext
{
    i32 maps = 0, unmaps = 0, binds = 0;
    bool fail_map = false;
    ID3D11Buffer *bound = nullptr;
};
struct D3D11_MAPPED_SUBRESOURCE
{
    void *pData;
};
struct D3D11_RenderData
{
    ID3D11Device *d3dDevice;
    ID3D11DeviceContext *d3dContext;
    ID3D11Buffer *vertexBuffers[8]{};
    usize vertexBufferSizes[8]{};
    i32 currentVertexBuffer = 0;
};
struct SDL_Renderer
{
    void *internal;
};
constexpr UINT D3D11_USAGE_DYNAMIC = 1, D3D11_BIND_VERTEX_BUFFER = 2, D3D11_CPU_ACCESS_WRITE = 4,
               D3D11_MAP_WRITE_DISCARD = 8;
static HRESULT ID3D11Device_CreateBuffer(ID3D11Device *d, const D3D11_BUFFER_DESC *desc,
                                         const void *initial, ID3D11Buffer **out)
{
    ++d->creates;
    CHECK(initial == nullptr); // enlarged allocations must not overread input
    if (d->fail_all || (d->fail_above && desc->ByteWidth > d->fail_above)) return -1;
    *out = new ID3D11Buffer(desc->ByteWidth);
    return 0;
}
static HRESULT ID3D11DeviceContext_Map(ID3D11DeviceContext *d, ID3D11Resource *buffer, UINT, UINT,
                                       UINT, D3D11_MAPPED_SUBRESOURCE *mapped)
{
    ++d->maps;
    if (d->fail_map) return -1;
    mapped->pData = buffer->bytes.data();
    return 0;
}
static void ID3D11DeviceContext_Unmap(ID3D11DeviceContext *d, ID3D11Resource *, UINT)
{
    ++d->unmaps;
}
static void ID3D11DeviceContext_IASetVertexBuffers(ID3D11DeviceContext *d, UINT, UINT,
                                                   ID3D11Buffer **buffer, const UINT *,
                                                   const UINT *)
{
    ++d->binds;
    d->bound = *buffer;
}
static bool WIN_SetErrorFromHRESULT(const char *, HRESULT)
{
    return false;
}
static bool SDL_SetError(const char *)
{
    return false;
}
#define FAILED(hr) ((hr) < 0)
#define SDL_zero(x) std::memset(&(x), 0, sizeof(x))
#define SDL_memcpy std::memcpy
#define SDL_arraysize(x) (sizeof(x) / sizeof((x)[0]))
#define SAFE_RELEASE(x)                                                                            \
    do                                                                                             \
    {                                                                                              \
        delete (x);                                                                                \
        (x) = nullptr;                                                                             \
    } while (false)
#include "../src/sdl_d3d11_buffer.inl"
#undef FAILED
#undef SDL_zero
#undef SDL_memcpy
#undef SDL_arraysize
#undef SAFE_RELEASE

PUI_TEST(sdl_d3d11_upload_preserves_bytes_and_reuses_capacity)
{
    ID3D11Device device;
    ID3D11DeviceContext dc;
    D3D11_RenderData data{&device, &dc};
    SDL_Renderer renderer{&data};
    const u8 bytes[] = {13, 28, 37, 254, 119};
    CHECK(D3D11_UpdateVertexBuffer(&renderer, bytes, sizeof(bytes)));
    ID3D11Buffer *first = data.vertexBuffers[0];
    CHECK(data.vertexBufferSizes[0] == 65536);
    CHECK(std::memcmp(first->bytes.data(), bytes, sizeof(bytes)) == 0);
    CHECK(first->bytes[sizeof(bytes)] == 0xcd); // only actual data was uploaded
    CHECK(dc.bound == first && device.creates == 1);
    data.currentVertexBuffer = 0;
    CHECK(D3D11_UpdateVertexBuffer(&renderer, bytes, 2));
    CHECK(data.vertexBuffers[0] == first && device.creates == 1);
    CHECK(dc.maps == 2 && dc.unmaps == 2);
    const i32 binds = dc.binds;
    CHECK(D3D11_UpdateVertexBuffer(&renderer, nullptr, 0));
    CHECK(dc.binds == binds && device.creates == 1);
    delete first;
}

PUI_TEST(sdl_d3d11_upload_fallback_and_failure_keep_old_buffer)
{
    ID3D11Device device;
    ID3D11DeviceContext dc;
    D3D11_RenderData data{&device, &dc};
    SDL_Renderer renderer{&data};
    std::vector<u8> bytes(70000, 0x35);
    CHECK(D3D11_UpdateVertexBuffer(&renderer, bytes.data(), 100));
    ID3D11Buffer *old = data.vertexBuffers[0];
    data.currentVertexBuffer = 0;
    device.fail_all = true;
    CHECK(!D3D11_UpdateVertexBuffer(&renderer, bytes.data(), bytes.size()));
    CHECK(data.vertexBuffers[0] == old && data.vertexBufferSizes[0] == 65536);
    device.fail_all = false;
    dc.fail_map = true;
    CHECK(!D3D11_UpdateVertexBuffer(&renderer, bytes.data(), bytes.size()));
    CHECK(data.vertexBuffers[0] == old && data.vertexBufferSizes[0] == 65536);
    dc.fail_map = false;
    device.fail_above = bytes.size(); // headroom fails, original exact size succeeds
    CHECK(D3D11_UpdateVertexBuffer(&renderer, bytes.data(), bytes.size()));
    CHECK(data.vertexBufferSizes[0] == bytes.size());
    CHECK(std::memcmp(data.vertexBuffers[0]->bytes.data(), bytes.data(), bytes.size()) == 0);
    delete data.vertexBuffers[0];
}
} // namespace fake_d3d
