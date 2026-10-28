// Shared SDF / HarfBuzz Slug coverage. Raster and compute use identical inputs.
#ifndef PUI_CACHED_GLYPHS_ONLY
#define PUI_SLUG_SSBO
vec2 pui_slug_ems_per_pixel;
#include "harfbuzz_slug.glsl"
#endif
struct Shape { vec4 bounds; vec4 radii; vec4 params; uvec4 corners; };
#ifndef PUI_EXTERNAL_SHAPES
layout(std430,set=1,binding=0) readonly buffer Shapes { Shape values[]; } shapes;
#endif
vec4 pui_unpack_bytes(uint c) { return vec4(c&255u,(c>>8)&255u,(c>>16)&255u,c>>24); }
uint pui_gradient(Shape s,vec2 point) {
    vec2 t=clamp((point-s.bounds.xy)/s.bounds.zw,vec2(0.0),vec2(1.0));
    vec4 color=mix(mix(pui_unpack_bytes(s.corners.x),pui_unpack_bytes(s.corners.y),t.x),
        mix(pui_unpack_bytes(s.corners.w),pui_unpack_bytes(s.corners.z),t.x),t.y);
    uvec4 bytes=uvec4(clamp(roundEven(color),vec4(0.0),vec4(255.0)));
    return bytes.x|(bytes.y<<8)|(bytes.z<<16)|(bytes.w<<24);
}
float pui_distance(Shape s, vec2 point) {
    vec2 p=point-(s.bounds.xy+s.bounds.zw*0.5);
    float r=p.y<0.0 ? (p.x<0.0?s.radii.x:s.radii.y) : (p.x<0.0?s.radii.w:s.radii.z);
    vec2 q=abs(p)-s.bounds.zw*0.5+vec2(r);
    return min(max(q.x,q.y),0.0)+length(max(q,vec2(0.0)))-r;
}
float pui_shape_coverage(Shape s,vec2 point) {
    return clamp(0.5-pui_distance(s,point)/s.params.x,0.0,1.0);
}
#ifndef PUI_CACHED_GLYPHS_ONLY
float pui_coverage(Shape s, vec2 point) {
    if(s.params.y==1.0) {
        vec2 coord=(point-s.radii.xy)/s.radii.z;
        coord.y=-coord.y;
        pui_slug_ems_per_pixel=vec2(1.0/s.radii.z);
        float coverage=_hb_gpu_slug(coord,vec2(s.radii.z),floatBitsToUint(s.radii.w));
        return pow(clamp(coverage,0.0,1.0),1.0/max(0.1,s.params.z));
    }
    return pui_shape_coverage(s,point);
}
#endif
