#version 450
layout(location=0) in vec4 bounds;
layout(location=1) in vec4 parameters;
layout(location=2) in vec4 color;
layout(location=3) in uvec4 glyph;
layout(push_constant) uniform Projection { vec2 extent; } projection;
layout(location=0) flat out vec4 baseline_scale_gamma;
layout(location=1) out vec4 tint;
layout(location=2) flat out uvec4 location;
void main() {
    const ivec2 corners[6]=ivec2[](ivec2(0,0),ivec2(1,0),ivec2(1,1),
                                ivec2(0,0),ivec2(1,1),ivec2(0,1));
    ivec2 corner=corners[gl_VertexIndex];
    vec2 position=vec2(corner.x==0 ? bounds.x : bounds.z,
                       corner.y==0 ? bounds.y : bounds.w);
    gl_Position=vec4(position/projection.extent*2.0-1.0,0.0,1.0);
    baseline_scale_gamma=parameters;
    tint=color;
    location=glyph;
}
