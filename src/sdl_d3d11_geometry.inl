// PufferUI's hash-guarded SDL D3D11 overlay. Keep the original vertex arithmetic,
// shader inputs and triangle order, but upload shared vertices only once.
#include "sdl_d3d11_buffer.h"

typedef struct PUI_D3D11_Geometry
{
    Uint32 vertex_offset, index_offset, index_count, reserved[5];
} PUI_D3D11_Geometry;

static bool D3D11_QueueGeometry(SDL_Renderer *renderer, SDL_RenderCommand *cmd,
                                SDL_Texture *texture, const float *xy, int xy_stride,
                                const SDL_FColor *color, int color_stride, const float *uv,
                                int uv_stride, int num_vertices, const void *indices,
                                int num_indices, int size_indices, float scale_x, float scale_y)
{
    const int count = indices ? num_indices : num_vertices;
    const size_t bytes = pui_d3d11_geometry_bytes(num_vertices, count, renderer->vertex_data_used);
    const bool convert_color = SDL_RenderingLinearSpace(renderer);
    D3D11_TextureData *textureData = texture ? (D3D11_TextureData *)texture->internal : NULL;
    const float u_scale = textureData ? (float)texture->w / textureData->w : 0.0f;
    const float v_scale = textureData ? (float)texture->h / textureData->h : 0.0f;
    PUI_D3D11_Geometry *record;
    D3D11_VertexPositionColor *verts;
    Uint32 *out_indices;
    int i;
    if (!bytes) return SDL_SetError("D3D11 geometry exceeds ByteWidth");
    if (indices && size_indices != 1 && size_indices != 2 && size_indices != 4)
        return SDL_SetError("D3D11 invalid index size");
    record = (PUI_D3D11_Geometry *)SDL_AllocateRenderVertices(renderer, bytes, 32,
                                                              &cmd->data.draw.first);
    if (!record) return false;
    SDL_zero(*record);
    record->vertex_offset = (Uint32)(cmd->data.draw.first + sizeof(*record));
    record->index_offset = record->vertex_offset + (Uint32)num_vertices * 32;
    record->index_count = (Uint32)count;
    verts = (D3D11_VertexPositionColor *)(record + 1);
    out_indices = (Uint32 *)((Uint8 *)record + sizeof(*record) + (size_t)num_vertices * 32);
    for (i = 0; i < num_vertices; ++i)
    {
        const float *xy_ = (const float *)((const char *)xy + (size_t)i * xy_stride);
        verts[i].pos.x = xy_[0] * scale_x;
        verts[i].pos.y = xy_[1] * scale_y;
        verts[i].color = *(const SDL_FColor *)((const char *)color + (size_t)i * color_stride);
        if (convert_color) SDL_ConvertToLinear(&verts[i].color);
        if (texture)
        {
            const float *uv_ = (const float *)((const char *)uv + (size_t)i * uv_stride);
            verts[i].tex.x = uv_[0] * u_scale;
            verts[i].tex.y = uv_[1] * v_scale;
        }
        else
        {
            verts[i].tex.x = 0.0f;
            verts[i].tex.y = 0.0f;
        }
    }
    for (i = 0; i < count; ++i)
        out_indices[i] = !indices            ? (Uint32)i
                         : size_indices == 4 ? ((const Uint32 *)indices)[i]
                         : size_indices == 2 ? ((const Uint16 *)indices)[i]
                                             : ((const Uint8 *)indices)[i];
    cmd->data.draw.count = count;
    return true;
}
