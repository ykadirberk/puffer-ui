// Native replay keeps implicit texture gradients valid across ownership edges.
#include "shape_common.glsl"
#ifdef PUI_SOURCE_ALPHA
#include "source_alpha.glsl"
#include "shape_groups.glsl"
#include "alpha_source.glsl"
#endif
layout(set=0,binding=0) uniform sampler2D picture;
layout(std430,set=1,binding=2) readonly buffer Route { uint words[]; } route;
layout(push_constant) uniform Projection { vec2 extent; uint routed; uint reserved; } projection;
layout(location=0) in vec2 texcoord;
layout(location=1) in vec4 tint;
layout(location=2) flat in uint shape;
layout(location=0) out vec4 output_color;
void main() {
    uint pixel=uint(gl_FragCoord.y)*uint(projection.extent.x)+uint(gl_FragCoord.x);
    bool owned=projection.routed!=0u && (route.words[pixel>>5]&(1u<<(pixel&31u)))!=0u;
#ifndef PUI_PROGRAM_REPLAY
    if(owned) discard;
#endif
    if(shape==0xffffffffu) {
        output_color=texture(picture,texcoord)*tint;
#ifdef PUI_PROGRAM_REPLAY
        if(owned) discard;
#endif
        return;
    }
#ifdef PUI_PROGRAM_REPLAY
    if(owned) discard;
    uint index=shape-projection.reserved;
#else
    uint index=shape;
#endif
#ifdef PUI_SOURCE_ALPHA
    if(shapes.values[index].params.y==4.0) {
        vec4 associated=texelFetch(picture,ivec2(gl_FragCoord.xy),0);
        output_color=pui_alpha_source(associated,shapes.values[index].params.z);
        return;
    }
    if(shapes.values[index].params.y==3.0) {
        output_color=pui_group_source(index,gl_FragCoord.xy);
        return;
    }
#endif
#ifndef PUI_SOURCE_ALPHA
    float coverage=pui_coverage(shapes.values[index],gl_FragCoord.xy);
#endif
    vec4 color=shapes.values[index].params.y==2.0 ?
        pui_unpack_bytes(pui_gradient(shapes.values[index],gl_FragCoord.xy))/255.0 : tint;
#ifdef PUI_SOURCE_ALPHA
    output_color=vec4(color.rgb,pui_source_alpha(shapes.values[index],gl_FragCoord.xy,color.a));
#else
    output_color=vec4(color.rgb,color.a*coverage);
#endif
}
