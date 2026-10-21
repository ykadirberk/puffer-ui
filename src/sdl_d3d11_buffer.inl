// PufferUI's local build overlay for the pinned SDL D3D11 renderer.
// Retain capacity between peaks and isolate small uploads from large geometry.
#include "sdl_d3d11_buffer.h"

static bool D3D11_UpdateVertexBuffer(SDL_Renderer *renderer, const void *vertexData,
                                     size_t dataSizeInBytes)
{
    D3D11_RenderData *rendererData = (D3D11_RenderData *)renderer->internal;
    int vbidx = pui_d3d11_upload_slot(dataSizeInBytes, rendererData->currentVertexBuffer);
    const UINT stride = sizeof(D3D11_VertexPositionColor);
    const UINT offset = 0;
    HRESULT result;
    D3D11_MAPPED_SUBRESOURCE mapped;

    if (dataSizeInBytes == 0) return true;
    if (vbidx == PUI_D3D11_LARGE_FIRST) vbidx += rendererData->puiLargeVertexCursor;
    if (!rendererData->vertexBuffers[vbidx] ||
        rendererData->vertexBufferSizes[vbidx] < dataSizeInBytes)
    {
        const size_t capacity =
            pui_d3d11_upload_capacity(rendererData->vertexBufferSizes[vbidx], dataSizeInBytes);
        D3D11_BUFFER_DESC desc;
        ID3D11Buffer *fresh = NULL;
        if (!capacity) return SDL_SetError("D3D11 vertex buffer exceeds ByteWidth");
        SDL_zero(desc);
        desc.ByteWidth = (UINT)capacity;
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_INDEX_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        // Spare capacity has no source bytes: never pass vertexData as initial
        // data for the enlarged allocation (D3D would read past its end).
        result = ID3D11Device_CreateBuffer(rendererData->d3dDevice, &desc, NULL, &fresh);
        if (FAILED(result) && capacity != dataSizeInBytes)
        {
            // Under memory pressure retain the original exact-size fallback.
            desc.ByteWidth = (UINT)dataSizeInBytes;
            result = ID3D11Device_CreateBuffer(rendererData->d3dDevice, &desc, NULL, &fresh);
        }
        if (FAILED(result))
            return WIN_SetErrorFromHRESULT("ID3D11Device::CreateBuffer [vertex buffer]", result);
        result = ID3D11DeviceContext_Map(rendererData->d3dContext, (ID3D11Resource *)fresh, 0,
                                         D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(result))
        {
            SAFE_RELEASE(fresh);
            return WIN_SetErrorFromHRESULT("ID3D11DeviceContext::Map [new vertex buffer]", result);
        }
        SDL_memcpy(mapped.pData, vertexData, dataSizeInBytes);
        ID3D11DeviceContext_Unmap(rendererData->d3dContext, (ID3D11Resource *)fresh, 0);
        SAFE_RELEASE(rendererData->vertexBuffers[vbidx]);
        rendererData->vertexBuffers[vbidx] = fresh;
        rendererData->vertexBufferSizes[vbidx] = desc.ByteWidth;
    }
    else
    {
        result = ID3D11DeviceContext_Map(rendererData->d3dContext,
                                         (ID3D11Resource *)rendererData->vertexBuffers[vbidx], 0,
                                         D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(result))
            return WIN_SetErrorFromHRESULT("ID3D11DeviceContext::Map [vertex buffer]", result);
        SDL_memcpy(mapped.pData, vertexData, dataSizeInBytes);
        ID3D11DeviceContext_Unmap(rendererData->d3dContext,
                                  (ID3D11Resource *)rendererData->vertexBuffers[vbidx], 0);
    }
    ID3D11DeviceContext_IASetVertexBuffers(rendererData->d3dContext, 0, 1,
                                           &rendererData->vertexBuffers[vbidx], &stride, &offset);
    rendererData->puiActiveVertexBuffer = vbidx;
    if (vbidx < PUI_D3D11_SMALL_BUFFERS)
        rendererData->currentVertexBuffer = (vbidx + 1) % PUI_D3D11_SMALL_BUFFERS;
    else if (vbidx >= PUI_D3D11_LARGE_FIRST)
        rendererData->puiLargeVertexCursor =
            (vbidx - PUI_D3D11_LARGE_FIRST + 1) % PUI_D3D11_LARGE_BUFFERS;
    return true;
}
