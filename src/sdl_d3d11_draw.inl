// Included in D3D11_RunCommandQueue's GEOMETRY case. Points/lines keep SDL's
// original path; indexed triangles retain submission and primitive order.
{
    const PUI_D3D11_Geometry *record =
        (const PUI_D3D11_Geometry *)((const Uint8 *)vertices + cmd->data.draw.first);
    if (cmd->data.draw.texture)
        D3D11_SetCopyState(renderer, cmd, NULL);
    else
        D3D11_SetDrawState(renderer, cmd, NULL, 0, NULL, 0, NULL, NULL);
    ID3D11DeviceContext_IASetPrimitiveTopology(rendererData->d3dContext,
                                               D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_IASetIndexBuffer(
        rendererData->d3dContext, rendererData->vertexBuffers[rendererData->puiActiveVertexBuffer],
        DXGI_FORMAT_R32_UINT, 0);
    ID3D11DeviceContext_DrawIndexed(
        rendererData->d3dContext, record->index_count, record->index_offset / sizeof(Uint32),
        (INT)(record->vertex_offset / sizeof(D3D11_VertexPositionColor)));
    break;
}
