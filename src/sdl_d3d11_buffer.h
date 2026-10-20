// Private policy shared by the vendored SDL build overlay and its portable tests.
// No renderer, shader, vertex or index changes: only dynamic-buffer capacity.
#ifndef PUFFERUI_SDL_D3D11_BUFFER_H
#define PUFFERUI_SDL_D3D11_BUFFER_H
#include <stddef.h>
#include <limits.h>

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
