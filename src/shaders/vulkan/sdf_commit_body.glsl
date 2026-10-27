layout(local_size_x=1) in;
void main() {
    uint base=1u+p.limits.w*2u;
    for(uint i=0;i<4;++i) old_meta.words[base+i]=floatBitsToUint(p.clip[i]);
    old_meta.words[base+4u]=p.cache.y;
    old_meta.words[base+5u]=p.cache.z;
#ifdef PUI_NATIVE_PAYLOAD
    old_meta.words[base+8u]=p.limits.x;
#endif
#ifdef PUI_FUNCTION_GENERATION
    old_meta.words[base+6u]=functions.words[9];
    old_meta.words[base+7u]=functions.words[10];
#endif
    old_meta.words[0]=control.words[3]==0u?1u:0u;
}
