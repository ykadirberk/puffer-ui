#version 450
#extension GL_GOOGLE_include_directive : require
#define PUI_SLUG_SSBO
vec2 pui_slug_ems_per_pixel;
#include "harfbuzz_slug.glsl"
layout(location=0) flat in vec4 baseline_scale_gamma;
layout(location=1) in vec4 tint;
layout(location=2) flat in uint location;
layout(location=0) out vec4 output_color;
void main() {
    vec2 coord=(gl_FragCoord.xy-baseline_scale_gamma.xy)/baseline_scale_gamma.z;
    coord.y=-coord.y;
    pui_slug_ems_per_pixel=vec2(1.0/baseline_scale_gamma.z);
    float coverage=_hb_gpu_slug(coord,vec2(baseline_scale_gamma.z),location);
    coverage=pow(clamp(coverage,0.0,1.0),1.0/max(0.1,baseline_scale_gamma.w));
    output_color=vec4(tint.rgb,tint.a*coverage);
}
