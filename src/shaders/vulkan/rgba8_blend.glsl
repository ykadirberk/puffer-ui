// Exact native-calibrated RGBA8 source/destination table, binding 1.
#ifndef PUI_BLEND_WORDS
#define PUI_BLEND_WORDS coarse_bits.words
#endif
uint blend_table_byte(uint at) {
    return (PUI_BLEND_WORDS[at>>2]>>((at&3u)*8))&255u;
}
uint blend_rgba8(uint source, uint destination) {
    uint a=source>>24;
    uint r=blend_table_byte((a<<16)+((destination&255u)<<8)+(source&255u));
    uint g=blend_table_byte((a<<16)+(((destination>>8)&255u)<<8)+((source>>8)&255u));
    uint b=blend_table_byte((a<<16)+(((destination>>16)&255u)<<8)+((source>>16)&255u));
    uint alpha=blend_table_byte(16777216u+(a<<8)+(destination>>24));
    return r|(g<<8)|(b<<16)|(alpha<<24);
}
