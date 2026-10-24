#version 450
#extension GL_GOOGLE_include_directive : require
#include "shape_common.glsl"
layout(set=0,binding=0) uniform sampler2D picture;
layout(std430,set=1,binding=2) readonly buffer Route { uint words[]; } route;
layout(push_constant) uniform Projection { vec2 extent; uint routed; uint reserved; } projection;
layout(location=0) in vec2 texcoord;
layout(location=1) in vec4 tint;
layout(location=2) flat in uint shape;
layout(location=0) out vec4 output_color;
void main() {
    uint pixel=uint(gl_FragCoord.y)*uint(projection.extent.x)+uint(gl_FragCoord.x);
    if(projection.routed!=0u && (route.words[pixel>>5]&(1u<<(pixel&31u)))!=0u) discard;
    if(shape==0xffffffffu) {
        output_color=texture(picture,texcoord)*tint;
        return;
    }
    float coverage=pui_coverage(shapes.values[shape],gl_FragCoord.xy);
    vec4 color=shapes.values[shape].params.y==2.0 ?
        pui_unpack_bytes(pui_gradient(shapes.values[shape],gl_FragCoord.xy))/255.0 : tint;
    output_color=vec4(color.rgb,color.a*coverage);
}
