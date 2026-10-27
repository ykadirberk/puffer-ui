#version 450
#extension GL_GOOGLE_include_directive : require
#include "byte_colors.glsl"
// The renderer uploads native operands; no CPU RGBA8-to-float expansion.
layout(std430,set=1,binding=3) readonly buffer Vertices { uint words[]; } vertices;
layout(push_constant) uniform Projection {
    vec2 extent; uint routed; uint reserved; uint vertex_words;
} projection;
layout(location=0) out vec2 texcoord;
layout(location=1) out vec4 tint;
layout(location=2) flat out uint shape;
void main() {
    uint at=projection.vertex_words+uint(gl_VertexIndex)*6u;
    vec2 position=uintBitsToFloat(uvec2(vertices.words[at],vertices.words[at+1u]));
    gl_Position=vec4(position/projection.extent*2.0-1.0,0.0,1.0);
    texcoord=uintBitsToFloat(uvec2(vertices.words[at+2u],vertices.words[at+3u]));
    tint=pui_cpu_tint(vertices.words[at+4u]);
    shape=vertices.words[at+5u];
}
