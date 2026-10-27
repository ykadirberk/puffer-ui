// GPU ABI shared by coarse/count/scan/emit. Mirrors paint_detail::command.
struct PaintCommand {
    uvec4 header;
    vec4 bounds;
    uvec4 payload;
    uvec4 identity;
};
vec4 rect_geometry(PaintCommand c) { return uintBitsToFloat(uvec4(c.payload.zw,c.identity.zw)); }
#ifdef PUI_NATIVE_PAYLOAD
layout(std430, set=0, binding=0) readonly buffer Commands { uint words[]; } program_tape;
uvec4 program_words(uint at) {
    return uvec4(program_tape.words[at],program_tape.words[at+1u],
        program_tape.words[at+2u],program_tape.words[at+3u]);
}
PaintCommand program_command(uint index) {
    uint at=index*16u;
    return PaintCommand(program_words(at),uintBitsToFloat(program_words(at+4u)),
        program_words(at+8u),program_words(at+12u));
}
#define PUI_COMMAND(index) program_command(index)
#else
layout(std430, set=0, binding=0) readonly buffer Commands { PaintCommand commands[]; } tape;
#define PUI_COMMAND(index) tape.commands[index]
#endif
layout(std430, set=0, binding=1) buffer Coarse { uint words[]; } coarse_bits;
layout(std430, set=0, binding=2) buffer Work { uint words[]; } work;
layout(std430, set=0, binding=3) buffer Indices { uint values[]; } output_list;
layout(std430, set=0, binding=4) buffer Control { uint words[]; } control;
layout(push_constant) uniform Parameters {
    uvec4 extent; // width, height, fine size, coarse size
    uvec4 grid;   // fine columns/rows, coarse columns/rows
    uvec4 limits; // commands, bitset words per coarse, index capacity, fine tiles
#if defined(PUI_SDF_PROGRAM)
    vec4 clip;
    uvec4 cache; // enabled, target generation low/high, fault flags
#endif
} p;
bool intersects_tile(uint command_index, uint tile_index) {
    vec4 b = PUI_COMMAND(command_index).bounds;
    uvec2 tile = uvec2(tile_index % p.grid.x, tile_index / p.grid.x);
    uvec2 low = tile * p.extent.z;
    uvec2 high = min(low + uvec2(p.extent.z), p.extent.xy);
    return b.x < b.z && b.y < b.w && b.z > float(low.x) && b.x < float(high.x) &&
           b.w > float(low.y) && b.y < float(high.y);
}
uint coarse_base(uint tile_index) {
    uvec2 tile = uvec2(tile_index % p.grid.x, tile_index / p.grid.x);
    uvec2 coarse = (tile * p.extent.z) / p.extent.w;
    return (coarse.y * p.grid.z + coarse.x) * p.limits.y;
}
