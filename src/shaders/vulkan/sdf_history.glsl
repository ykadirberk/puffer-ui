// Cache only pixels whose qualified suffix is independent of the destination.
// Unknown legacy geometry can invalidate qualification but can never supply a
// cached color. Shape payloads, order, bounds, tint, clip and target generation
// are compared exactly; hashes are rejection filters only.
#ifdef PUI_NATIVE_PAYLOAD
layout(std430,set=2,binding=1) readonly buffer OldCommands { uint words[]; } old_program;
#else
layout(std430,set=2,binding=1) readonly buffer OldCommands { PaintCommand commands[]; } old_tape;
#endif
layout(std430,set=2,binding=2) readonly buffer OldWork { uint words[]; } old_work;
layout(std430,set=2,binding=3) readonly buffer OldIndices { uint values[]; } old_list;
layout(std430,set=2,binding=4) readonly buffer OldShapes { Shape values[]; } old_shapes;
layout(std430,set=2,binding=5) readonly buffer OldRoute { uint words[]; } old_route;
layout(rgba8,set=2,binding=6) uniform image2D saved_pixels;
layout(std430,set=2,binding=7) buffer TileState { uint words[]; } tile_state;
layout(std430,set=2,binding=8) buffer OldMeta { uint words[]; } old_meta;
#ifdef PUI_NATIVE_PAYLOAD
uvec4 old_program_words(uint at) {
    return uvec4(old_program.words[at],old_program.words[at+1u],
        old_program.words[at+2u],old_program.words[at+3u]);
}
PaintCommand old_program_command(uint index) {
    uint at=index*16u;
    return PaintCommand(old_program_words(at),uintBitsToFloat(old_program_words(at+4u)),
        old_program_words(at+8u),old_program_words(at+12u));
}
bool same_native_payload(PaintCommand a,PaintCommand b) {
    if(a.payload.y!=b.payload.y) return false;
    uint base=1u+p.limits.w*2u;
    uint current=p.limits.x*16u+a.payload.x/4u;
    uint previous=old_meta.words[base+8u]*16u+b.payload.x/4u;
    bool producer=a.payload.y>=96u && program_tape.words[current+16u]!=0u;
    if(producer) {
        if(program_tape.words[current+16u]!=old_program.words[previous+16u]) return false;
        uint state=program_tape.words[current+17u];
        if(program_tape.words[state]!=0u || program_tape.words[state+2u]!=0u) return false;
    }
    for(uint word=0u;word<a.payload.y/4u;++word)
        if(producer && (word==4u || word==5u || word==17u)) continue;
        else
        if(program_tape.words[current+word]!=old_program.words[previous+word]) return false;
    return true;
}
#endif
bool same_shape(Shape a,Shape b) {
#ifdef PUI_NATIVE_PAYLOAD
    if(a.params.y==1.0 && b.params.y==1.0)
        return all(equal(floatBitsToUint(a.bounds),floatBitsToUint(b.bounds))) &&
            all(equal(floatBitsToUint(a.radii),floatBitsToUint(b.radii))) &&
            all(equal(floatBitsToUint(a.params),floatBitsToUint(b.params)));
#endif
    return all(equal(floatBitsToUint(a.bounds),floatBitsToUint(b.bounds))) &&
        all(equal(floatBitsToUint(a.radii),floatBitsToUint(b.radii))) &&
        all(equal(floatBitsToUint(a.params),floatBitsToUint(b.params))) && all(equal(a.corners,b.corners));
}
bool same_command(PaintCommand a,PaintCommand b) {
    if(any(notEqual(a.header.xy,b.header.xy)) || any(notEqual(floatBitsToUint(a.bounds),floatBitsToUint(b.bounds)))) return false;
#ifdef PUI_NATIVE_PAYLOAD
    if(!same_native_payload(a,b)) return false;
#endif
    if(a.header.x==1u) return all(equal(a.payload.zw,b.payload.zw)) && all(equal(a.identity.zw,b.identity.zw));
#ifdef PUI_NATIVE_PAYLOAD
    if(a.header.x!=2u) return true;
    Shape current=shapes.values[a.payload.z],previous=old_shapes.values[b.payload.z];
    if(!same_shape(current,previous)) return false;
    if(current.params.y==3.0) {
        uint count=floatBitsToUint(current.radii.x);
        if(count==0u || count>256u || count>a.payload.z || count>b.payload.z) return false;
        for(uint i=1u;i<=count;++i)
            if(!same_shape(shapes.values[a.payload.z-i],old_shapes.values[b.payload.z-i])) return false;
    }
    return true;
#else
    return a.header.x!=2u || same_shape(shapes.values[a.payload.z],old_shapes.values[b.payload.z]);
#endif
}
