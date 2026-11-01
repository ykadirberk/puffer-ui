#version 450
layout(set=0,binding=0) uniform sampler2D picture;
layout(location=0) in vec2 texcoord;
layout(location=1) in vec4 tint;
layout(location=0) out vec4 output_color;
layout(constant_id=0) const uint filter_kernel=0u;
layout(push_constant) uniform Blur {
    layout(offset=16) vec2 step_uv;
    vec2 low;
    vec2 high;
    float weights[5];
    float offsets[4];
} blur;
vec4 sample_blur(vec2 offset) {
    return texture(picture,clamp(texcoord+offset,blur.low,blur.high));
}
void main() {
    if (filter_kernel==1u) {
        vec4 sum=sample_blur(vec2(0.0))*blur.weights[0];
        for (int i=0;i<4;++i) {
            if (blur.weights[i+1]>0.0) {
                vec2 delta=blur.step_uv*blur.offsets[i];
                sum+=(sample_blur(delta)+sample_blur(-delta))*blur.weights[i+1];
            }
        }
        output_color=sum;
    } else if (filter_kernel==2u) {
        vec2 d=blur.step_uv;
        vec4 sum=sample_blur(vec2(0.0))*4.0;
        sum+=sample_blur(vec2(-d.x,-d.y));
        sum+=sample_blur(vec2( d.x, d.y));
        sum+=sample_blur(vec2( d.x,-d.y));
        sum+=sample_blur(vec2(-d.x, d.y));
        output_color=sum*0.125;
    } else if (filter_kernel==3u) {
        vec2 d=blur.step_uv;
        vec4 sum=sample_blur(vec2(-2.0*d.x,0.0));
        sum+=sample_blur(vec2(-d.x, d.y))*2.0;
        sum+=sample_blur(vec2(0.0,2.0*d.y));
        sum+=sample_blur(vec2(d.x,d.y))*2.0;
        sum+=sample_blur(vec2(2.0*d.x,0.0));
        sum+=sample_blur(vec2(d.x,-d.y))*2.0;
        sum+=sample_blur(vec2(0.0,-2.0*d.y));
        sum+=sample_blur(vec2(-d.x,-d.y))*2.0;
        output_color=sum/12.0;
    } else {
        output_color=texture(picture,texcoord)*tint;
    }
}
