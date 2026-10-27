// Header immediately follows its children; relative addressing survives batch
// compaction, native prefix replay and retained CPU snapshots unchanged.
vec4 pui_group_source(uint index,vec2 point) {
    Shape header=shapes.values[index];
    uint count=floatBitsToUint(header.radii.x);
    if(count==0u || count>256u || count>index) return vec4(0.0);
    float distance=3.402823466e+38;
    for(uint i=index-count;i<index;++i) distance=min(distance,pui_distance(shapes.values[i],point));
    float fill=clamp(0.5-distance/header.params.x,0.0,1.0);
    float outline=header.params.z>0.0?
        clamp(0.5-(abs(distance)-header.params.z)/header.params.x,0.0,1.0):0.0;
    float glow=header.params.w>0.0?clamp(1.0-max(distance,0.0)/header.params.w,0.0,1.0):0.0;
    glow*=glow;
    vec4 layers[3]=vec4[](pui_cpu_tint(header.corners.z),pui_cpu_tint(header.corners.y),
                           pui_cpu_tint(header.corners.x));
    float coverages[3]=float[](glow,outline,fill);
    vec4 result=vec4(0.0);
    for(uint i=0u;i<3u;++i) {
        float alpha=layers[i].a*coverages[i];
        result.rgb=layers[i].rgb*alpha+result.rgb*(1.0-alpha);
        result.a=alpha+result.a*(1.0-alpha);
    }
    return result.a==0.0?vec4(0.0):vec4(result.rgb/result.a,result.a);
}
