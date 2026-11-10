#version 450
layout(location = 0) in vec4 bounds;
layout(location = 1) in vec4 radii;
layout(location = 2) in vec4 top_left;
layout(location = 3) in vec4 top_right;
layout(location = 4) in vec4 bottom_right;
layout(location = 5) in vec4 bottom_left;
layout(location = 6) in vec4 underlay;
layout(location = 7) in uint gradient;
layout(push_constant) uniform Projection { vec2 extent; } projection;

layout(location = 0) out vec2 local_position;
layout(location = 1) flat out vec2 size;
layout(location = 2) flat out vec4 corner_radii;
layout(location = 3) flat out vec4 shade_top_left;
layout(location = 4) flat out vec4 shade_top_right;
layout(location = 5) flat out vec4 shade_bottom_right;
layout(location = 6) flat out vec4 shade_bottom_left;
layout(location = 7) flat out vec4 shade_underlay;
layout(location = 8) flat out uint gradient_fill;

void main() {
    vec2 corner = vec2(gl_VertexIndex & 1, gl_VertexIndex >> 1);
    local_position = corner * (bounds.zw + 2.0) - 1.0;
    vec2 position = bounds.xy + local_position;
    gl_Position = vec4(position / projection.extent * 2.0 - 1.0, 0.0, 1.0);
    size = bounds.zw;
    corner_radii = radii;
    shade_top_left = top_left;
    shade_top_right = top_right;
    shade_bottom_right = bottom_right;
    shade_bottom_left = bottom_left;
    shade_underlay = underlay;
    gradient_fill = gradient;
}
