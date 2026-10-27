#include "function_common.glsl"
#ifndef PUI_FUNCTION_TEXTURE_SET
#define PUI_FUNCTION_TEXTURE_SET PUI_FUNCTION_SET
#endif
#ifndef PUI_FUNCTION_TEXTURE_BINDING
#define PUI_FUNCTION_TEXTURE_BINDING 2
#endif
layout(set=PUI_FUNCTION_TEXTURE_SET,binding=PUI_FUNCTION_TEXTURE_BINDING) uniform sampler2D function_pixels;
bool function_blend(vec4 source,uint destination,out uint value) {
    if(!function_finite(source)) return false;
    uvec4 key=floatBitsToUint(source);
    uint first=(function_hash(key)&(function_rows()/PUI_FUNCTION_WAYS-1u))*PUI_FUNCTION_WAYS;
    for(uint way=0u;way<PUI_FUNCTION_WAYS;++way) {
        uint row=first+way,at=function_row(row);
        if(functions.words[at+5u]!=1u || !function_key(at,key)) continue;
        uvec4 bytes=uvec4(destination&255u,(destination>>8)&255u,
            (destination>>16)&255u,destination>>24);
        for(uint c=0u;c<4u;++c)
            bytes[c]=uint(roundEven(texelFetch(function_pixels,ivec2(bytes[c],row),0)[c]*255.0));
        value=bytes.x|(bytes.y<<8)|(bytes.z<<16)|(bytes.w<<24);
        return true;
    }
    function_request(key);
    return false;
}
