#version 450
#extension GL_GOOGLE_include_directive : require
#define PUI_SLUG_SSBO
vec2 pui_slug_ems_per_pixel;
#include "harfbuzz_slug.glsl"
layout(location=0) flat in vec4 baseline_scale_gamma;
layout(location=1) in vec4 tint;
layout(location=2) flat in uvec4 location;
layout(set=0,binding=0) uniform sampler2D coverage_image;
layout(constant_id=0) const bool coverage_pass=false;
layout(location=0) out vec4 output_color;
void main() {
    ivec2 atlas=ivec2(location.y&65535u,location.y>>16u);
    float coverage;
    if (!coverage_pass && (location.x&0x80000000u)!=0u) {
        ivec2 pixel=ivec2(gl_FragCoord.xy)-ivec2(location.zw)+atlas;
        coverage=texelFetch(coverage_image,pixel,0).r;
    } else {
        // Recreate the original absolute pixel center with integer arithmetic.
        // Subpixel phase alone cannot prove identical float subtraction at
        // different absolute baseline magnitudes.
        vec2 pixel=coverage_pass ? vec2(ivec2(gl_FragCoord.xy)-atlas+ivec2(location.zw))+0.5
                                : gl_FragCoord.xy;
        vec2 coord=(pixel-baseline_scale_gamma.xy)/baseline_scale_gamma.z;
        coord.y=-coord.y;
        pui_slug_ems_per_pixel=vec2(1.0/baseline_scale_gamma.z);
        coverage=_hb_gpu_slug(coord,vec2(baseline_scale_gamma.z),location.x);
        coverage=pow(clamp(coverage,0.0,1.0),1.0/max(0.1,baseline_scale_gamma.w));
    }
    output_color=coverage_pass ? vec4(coverage,0.0,0.0,1.0)
                              : vec4(tint.rgb,tint.a*coverage);
}
