// One portable 64-lane workgroup scans bounded contiguous partitions. There is
// no scene-sized serial chain of storage-buffer loads. y propagates overflow;
// sums saturate at the actual list capacity without wrapping uint arithmetic.
shared uvec2 partition_prefix[64];
// Fine counting visits each command at most once, so its total is bounded by
// the command count. It needs a reduction, not all 64 exclusive prefixes.
uint sum_partitions(uint value) {
    uint lane=gl_LocalInvocationIndex;
    partition_prefix[lane]=uvec2(value,0);
    barrier();
    for(uint distance=32u;distance!=0u;distance>>=1u) {
        if(lane<distance) partition_prefix[lane].x+=partition_prefix[lane+distance].x;
        barrier();
    }
    return partition_prefix[0].x;
}
uvec2 bounded_sum(uvec2 a, uvec2 b, uint capacity) {
    bool overflow=(a.y|b.y)!=0u || b.x>capacity || a.x>capacity-b.x;
    return uvec2(overflow?capacity:a.x+b.x,uint(overflow));
}
uvec2 scan_partitions(uvec2 value,uint capacity) {
    uint lane=gl_LocalInvocationIndex;
    partition_prefix[lane]=value;
    barrier();
    for(uint distance=1u;distance<64u;distance<<=1u) {
        uvec2 before=lane>=distance?partition_prefix[lane-distance]:uvec2(0);
        barrier();
        if(lane>=distance)
            partition_prefix[lane]=bounded_sum(partition_prefix[lane],before,capacity);
        barrier();
    }
    return lane==0u?uvec2(0):partition_prefix[lane-1u];
}
