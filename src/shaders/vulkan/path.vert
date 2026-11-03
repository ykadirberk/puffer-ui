#version 450
layout(location=0) in vec4 bounds;
layout(location=1) in vec4 inverse_x;
layout(location=2) in vec4 inverse_y;
layout(location=3) in vec4 viewport;
layout(location=4) in vec4 color;
layout(location=5) in vec4 current_color;
layout(location=6) in vec4 origins;
layout(location=7) in uvec4 metadata;
layout(push_constant) uniform Projection { vec2 extent; } projection;
layout(location=0) flat out vec4 root_x;
layout(location=1) flat out vec4 root_y;
layout(location=2) flat out vec4 source_clip;
layout(location=3) flat out vec4 tint;
layout(location=4) flat out vec4 current;
layout(location=5) flat out vec4 offsets;
layout(location=6) flat out uvec4 location;
void main() {
    const ivec2 corners[6]=ivec2[](ivec2(0,0),ivec2(1,0),ivec2(1,1),
                                ivec2(0,0),ivec2(1,1),ivec2(0,1));
    ivec2 corner=corners[gl_VertexIndex];
    vec2 position=vec2(corner.x==0 ? bounds.x : bounds.z,
                       corner.y==0 ? bounds.y : bounds.w);
    gl_Position=vec4(position/projection.extent*2.0-1.0,0.0,1.0);
    root_x=inverse_x; root_y=inverse_y; source_clip=viewport;
    tint=color; current=current_color; offsets=origins; location=metadata;
}
