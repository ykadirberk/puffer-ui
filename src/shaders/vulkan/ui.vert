#version 450
layout(location=0) in vec2 position;
layout(location=1) in vec2 uv;
layout(location=2) in uvec4 color;
layout(push_constant) uniform Projection { vec2 extent; } projection;
layout(location=0) out vec2 texcoord;
layout(location=1) out vec4 tint;
layout(constant_id=0) const bool compact_vertices=true;
// Exact binary32 of byte / 255, including its round-up at binary midpoints.
// The repeating-byte expansion avoids implementation-dependent UNORM/FDiv
// rounding and does not need a dynamically indexed 256-element shader array.
float byte_channel(uint value) {
    if (value == 0u) return 0.0;
    if (value == 255u) return 1.0;
    uint top=uint(findMSB(value));
    uint significand=(value*0x01010101u+(1u<<top))>>(top+1u);
    return uintBitsToFloat(((top+119u)<<23u)|(significand&0x007fffffu));
}
void main() {
    gl_Position=vec4(position/projection.extent*2.0-1.0,0.0,1.0);
    texcoord=uv;
    tint=compact_vertices
        ? vec4(byte_channel(color.r),byte_channel(color.g),byte_channel(color.b),byte_channel(color.a))
        : uintBitsToFloat(color);
}
