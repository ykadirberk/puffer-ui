// Private policy shared by the vendored SDL build overlay and its portable tests.
// Shared size checks and upload selection; no renderer or platform dependencies.
#ifndef PUFFERUI_SDL_D3D11_BUFFER_H
#define PUFFERUI_SDL_D3D11_BUFFER_H
#include <stddef.h>
#include <limits.h>

// Five rotating small buffers, one medium and two large. Never discard a large
// allocation for a tiny blur/copy quad. The cursor belongs to one renderer.
// Two large resources let the preceding geometry upload remain in flight.
enum
{
    PUI_D3D11_SMALL_BUFFERS = 5,
    PUI_D3D11_MEDIUM_BUFFER = 5,
    PUI_D3D11_LARGE_FIRST = 6,
    PUI_D3D11_LARGE_BUFFERS = 2
};
static inline int pui_d3d11_upload_slot(size_t bytes, int small_cursor)
{
    return bytes > 4194304   ? PUI_D3D11_LARGE_FIRST
           : bytes > 1048576 ? PUI_D3D11_MEDIUM_BUFFER
                             : small_cursor;
}

// Every record and vertex starts at a multiple of the 32-byte vertex stride.
// Bound the entire arena as well as the individual record before allocating:
// ByteWidth and DrawIndexed offsets are 32 bit even on a 64-bit host.
static inline size_t pui_d3d11_geometry_bytes(int vertices, int indices, size_t used)
{
    // SDL's arena grows geometrically; on 32-bit hosts its next power of two
    // must also remain representable. Leave room for allocator alignment.
    const size_t limit = sizeof(size_t) > sizeof(unsigned) ? (size_t)UINT_MAX : (size_t)-1 / 2;
    size_t bytes, vertex_bytes, index_bytes;
    if (vertices < 0 || indices < 0 || used > limit - 63) return 0;
    if ((size_t)vertices > (limit - 63) / 32) return 0;
    vertex_bytes = (size_t)vertices * 32;
    if ((size_t)indices > (limit - 63 - vertex_bytes) / 4) return 0;
    index_bytes = (size_t)indices * 4;
    bytes = (32 + vertex_bytes + index_bytes + 31) & ~(size_t)31;
    used = (used + 31) & ~(size_t)31;
    return bytes <= limit - 32 - used ? bytes : 0;
}

static inline size_t pui_d3d11_vertex_capacity(size_t current, size_t required)
{
    const size_t limit = (size_t)UINT_MAX; // D3D11_BUFFER_DESC::ByteWidth
    size_t capacity;
    size_t wanted;
    if (required > limit || current > limit) return 0;
    if (required <= current) return current;
    // A retargeted layout can briefly draw almost twice its settled geometry.
    // Keep one workload's worth of headroom when growing, rather than growing
    // again on each frame of that transition. Reused buffers never shrink.
    wanted = required <= limit / 2 ? required * 2 : required;
    capacity = current ? current : 65536;
    while (capacity < wanted)
    {
        if (capacity > limit / 2) return required;
        capacity *= 2;
    }
    return capacity;
}
#endif
