#version 450
#extension GL_GOOGLE_include_directive : require
#define PUI_SLUG_SSBO
vec2 pui_slug_ems_per_pixel;
#include "harfbuzz_slug.glsl"
layout(location=0) flat in vec4 root_x;
layout(location=1) flat in vec4 root_y;
layout(location=2) flat in vec4 source_clip;
layout(location=3) flat in vec4 tint;
layout(location=4) flat in vec4 current;
layout(location=5) flat in vec4 offsets;
layout(location=6) flat in uvec4 location;
layout(set=0,binding=0) uniform sampler2D source_image;
layout(location=0) out vec4 output_color;
const uint no_clip=0xffffffffu, empty_clip=0xfffffffeu;
uint word(uint index) {
    uvec2 pair=slug_atlas.values[index>>1u];
    return (index&1u)==0u ? pair.x : pair.y;
}
float number(uint index) { return uintBitsToFloat(word(index)); }
vec2 pair(uint index) { return vec2(number(index),number(index+1u)); }
vec4 rgba(uint index) {
    return vec4(number(index),number(index+1u),number(index+2u),number(index+3u));
}
float path_coverage(uint index,vec2 point) {
    if(index==no_clip)return 1.0;
    if(index==empty_clip)return 0.0;
    vec2 scale=pair(index+1u);
    vec2 coord=(point-pair(index+3u))*scale;
    pui_slug_ems_per_pixel=scale*vec2(abs(root_x.x)+abs(root_x.y),
                                    abs(root_y.x)+abs(root_y.y));
    return clamp(_hb_gpu_slug(coord,1.0/max(pui_slug_ems_per_pixel,vec2(1e-30)),word(index)),0.0,1.0);
}
vec4 stop_color(uint index) {
    vec4 result=word(index+1u)!=0u ? current : rgba(index+4u);
    result.a*=number(index+2u);
    return result;
}
vec4 paint_color(uint index,vec2 point) {
    uint kind=word(index), count=word(index+2u);
    if(kind==0u||word(index+26u)==0u)return vec4(0.0);
    if(kind==1u)return (word(index+3u)!=0u ? current : rgba(index+4u))*vec4(1.0,1.0,1.0,number(index+25u));
    if(count==0u)return vec4(0.0);
    point=vec2(dot(rgba(index+8u).xyz,vec3(point,1.0)),
               dot(rgba(index+12u).xyz,vec3(point,1.0)));
    float t;
    if(kind==2u) {
        vec2 start=pair(index+16u), direction=pair(index+18u)-start;
        float magnitude=max(abs(direction.x),abs(direction.y));
        if(magnitude==0.0)return stop_color(index+32u+8u*(count-1u))*vec4(1.0,1.0,1.0,number(index+25u));
        direction/=magnitude;
        t=dot((point-start)/magnitude,direction)/dot(direction,direction);
    } else {
        float radius=number(index+24u);
        if(radius<=0.0)return stop_color(index+32u+8u*(count-1u))*vec4(1.0,1.0,1.0,number(index+25u));
        vec2 focal=pair(index+22u), delta=(point-focal)/radius,
             center=(pair(index+20u)-focal)/radius;
        float a=dot(center,center)-1.0,b=-2.0*dot(delta,center),c=dot(delta,delta);
        if(c==0.0)t=0.0;
        else if(abs(a)<1e-20) {
            if(b>=0.0)return vec4(0.0);
            t=-c/b;
        } else {
            float discriminant=b*b-4.0*a*c;
            if(discriminant<0.0)return vec4(0.0);
            float root=sqrt(discriminant);
            t=max((-b-root)/(2.0*a),(-b+root)/(2.0*a));
            if(t<0.0)return vec4(0.0);
        }
    }
    if(isnan(t)||isinf(t))return vec4(0.0);
    uint spread=word(index+1u);
    if(spread==1u)t-=floor(t);
    else if(spread==2u) {t=mod(t,2.0);if(t>1.0)t=2.0-t;}
    else t=clamp(t,0.0,1.0);
    // upper_bound selects the last stop at duplicate offsets, as in SVG.
    uint low=0u,high=count;
    while(low<high) {
        uint mid=low+(high-low)/2u;
        if(t<number(index+32u+mid*8u))high=mid;
        else low=mid+1u;
    }
    vec4 result;
    if(low==0u)result=stop_color(index+32u);
    else if(low==count)result=stop_color(index+32u+(count-1u)*8u);
    else {
        uint first=index+32u+(low-1u)*8u,second=first+8u;
        float a=number(first),b=number(second);
        result=mix(stop_color(first),stop_color(second),(t-a)/(b-a));
    }
    result.a*=number(index+25u);
    return result;
}
void main() {
    // Integer translations preserve the absolute pixel center in isolated groups.
    vec2 pixel=vec2(ivec2(gl_FragCoord.xy)+ivec2(offsets.xy))+0.5;
    vec2 point=vec2(dot(root_x.xyz,vec3(pixel,1.0)),dot(root_y.xyz,vec3(pixel,1.0)));
    // Keep the existing borrowed-device feature contract: GLSL discard under
    // Vulkan 1.3 can require shaderDemoteToHelperInvocation. A transparent
    // source-over result preserves the attachment without that capability.
    if(any(lessThan(point,source_clip.xy))||any(greaterThanEqual(point,source_clip.zw))) {
        output_color=vec4(0.0);return;
    }
    float coverage=path_coverage(location.z,point);
    vec4 paint;
    if((location.w&1u)!=0u) {
        paint=texelFetch(source_image,ivec2(pixel)-ivec2(offsets.zw),0);
        paint.rgb=paint.a>0.0 ? paint.rgb/paint.a : vec3(0.0);
    } else {
        coverage*=path_coverage(location.x,point);
        paint=paint_color(location.y,point);
    }
    output_color=vec4(paint.rgb*tint.rgb,paint.a*tint.a*coverage);
}
