// Only the local D3D11 geometry overlay uses this allocator. SDL owns the arena
// and still assigns offsets/alignment; do not move any queued command bytes.
#include "sdl_d3d11_buffer.h"

static void *PUI_D3D11_AllocateRenderVertices(SDL_Renderer *renderer, size_t bytes,
                                              size_t alignment, size_t *offset)
{
    const size_t capacity = pui_d3d11_arena_capacity(renderer->vertex_data_allocation,
                                                     renderer->vertex_data_used, bytes, alignment);
    if (!capacity) return NULL;
    if (capacity > renderer->vertex_data_allocation)
    {
        void *fresh = SDL_malloc(capacity);
        if (fresh)
        {
            if (renderer->vertex_data_used)
                SDL_memcpy(fresh, renderer->vertex_data, renderer->vertex_data_used);
            SDL_free(renderer->vertex_data);
        }
        else
        {
            // Under memory pressure the original realloc may expand in place.
            // Failure leaves the old allocation and every queued byte intact.
            fresh = SDL_realloc(renderer->vertex_data, capacity);
            if (!fresh) return NULL;
        }
        renderer->vertex_data = fresh;
        renderer->vertex_data_allocation = capacity;
    }
    return SDL_AllocateRenderVertices(renderer, bytes, alignment, offset);
}
