#version 450
#extension GL_GOOGLE_include_directive : require
#define PUI_FUNCTION_READONLY
#include "function_common.glsl"
layout(push_constant) uniform Extent { vec2 size; } p;
layout(location=0) flat out vec4 source;
void main() {
    const vec2 corners[6]=vec2[](vec2(0,0),vec2(1,0),vec2(1,1),
        vec2(0,0),vec2(1,1),vec2(0,1));
    uint row=functions.words[function_list_base()+gl_InstanceIndex],at=function_row(row);
    vec2 point=vec2(corners[gl_VertexIndex].x*p.size.x,float(row)+corners[gl_VertexIndex].y);
    gl_Position=vec4(point/p.size*2.0-1.0,0,1);
    source=uintBitsToFloat(uvec4(functions.words[at],functions.words[at+1u],
        functions.words[at+2u],functions.words[at+3u]));
}
