// Cache only pixels whose qualified suffix is independent of the destination.
// Unknown legacy geometry can invalidate qualification but can never supply a
// cached color. Shape payloads, order, bounds, tint, clip and target generation
// are compared exactly; hashes are rejection filters only.
layout(std430,set=2,binding=1) readonly buffer OldCommands { PaintCommand commands[]; } old_tape;
layout(std430,set=2,binding=2) readonly buffer OldWork { uint words[]; } old_work;
layout(std430,set=2,binding=3) readonly buffer OldIndices { uint values[]; } old_list;
layout(std430,set=2,binding=4) readonly buffer OldShapes { Shape values[]; } old_shapes;
layout(std430,set=2,binding=5) readonly buffer OldRoute { uint words[]; } old_route;
layout(rgba8,set=2,binding=6) uniform image2D saved_pixels;
layout(std430,set=2,binding=7) buffer TileState { uint words[]; } tile_state;
layout(std430,set=2,binding=8) buffer OldMeta { uint words[]; } old_meta;
bool same_shape(Shape a,Shape b) {
    return all(equal(floatBitsToUint(a.bounds),floatBitsToUint(b.bounds))) &&
        all(equal(floatBitsToUint(a.radii),floatBitsToUint(b.radii))) &&
        all(equal(floatBitsToUint(a.params),floatBitsToUint(b.params))) && all(equal(a.corners,b.corners));
}
bool same_command(PaintCommand a,PaintCommand b) {
    if(any(notEqual(a.header.xy,b.header.xy)) || any(notEqual(floatBitsToUint(a.bounds),floatBitsToUint(b.bounds)))) return false;
    if(a.header.x==1u) return all(equal(a.payload.zw,b.payload.zw)) && all(equal(a.identity.zw,b.identity.zw));
    return a.header.x!=2u || same_shape(shapes.values[a.payload.z],old_shapes.values[b.payload.z]);
}
