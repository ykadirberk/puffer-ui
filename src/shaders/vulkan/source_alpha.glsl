// Cache final float alpha, preserving tint multiplication and Slug gamma.
#include "byte_colors.glsl"
layout(set=1,binding=4) uniform usampler2D glyph_alpha;
float pui_source_alpha(Shape s,vec2 point,float alpha,out bool cached) {
    cached=false;
    if(s.params.y==1.0 && s.corners.x!=0u && s.corners.w<256u &&
       floatBitsToUint(alpha)==pui_byte_float_bits[s.corners.w]) {
        int row=int(s.corners.x-1u)*65;
        ivec2 size=textureSize(glyph_alpha,0);
        ivec2 pixel=ivec2(floor(point)-(floor(s.bounds.xy)-vec2(1.0)));
        if(size.x>=64 && row+64<size.y && all(greaterThanEqual(pixel,ivec2(0))) &&
           all(lessThan(pixel,ivec2(64))) &&
           texelFetch(glyph_alpha,ivec2(0,row),0).r==s.corners.y &&
           texelFetch(glyph_alpha,ivec2(1,row),0).r==s.corners.z) {
            cached=true;
            return uintBitsToFloat(texelFetch(glyph_alpha,ivec2(pixel.x,row+1+pixel.y),0).r);
        }
    }
    return alpha*pui_coverage(s,point);
}
float pui_source_alpha(Shape s,vec2 point,float alpha) {
    bool cached;
    return pui_source_alpha(s,point,alpha,cached);
}
