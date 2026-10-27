// Copy only dirty tiles; clean cached pixels are never cleared or copied.
#include "bin_common.glsl"
#include "shape_common.glsl"
#include "sdf_history.glsl"
layout(rgba8,set=2,binding=0) uniform readonly image2D scene;
#ifdef PUI_NATIVE_SNAPSHOT
layout(rgba8,set=2,binding=10) uniform writeonly image2D previous_input;
#define PUI_NATIVE_DESTINATION previous_input
#else
#define PUI_NATIVE_DESTINATION saved_pixels
#endif
layout(local_size_x=8,local_size_y=8) in;
void main() {
    uint tile=gl_WorkGroupID.x+gl_WorkGroupID.y*gl_NumWorkGroups.x;
    if(tile>=p.limits.w || tile_state.words[tile]==0u || control.words[3]!=0u) return;
    uvec2 origin=uvec2(tile%p.grid.x,tile/p.grid.x)*p.extent.z;
    for(uint y=gl_LocalInvocationID.y;y<p.extent.z;y+=8u)
        for(uint x=gl_LocalInvocationID.x;x<p.extent.z;x+=8u) {
            uvec2 pixel=origin+uvec2(x,y);
            if(any(greaterThanEqual(pixel,p.extent.xy))) continue;
            imageStore(PUI_NATIVE_DESTINATION,ivec2(pixel),imageLoad(scene,ivec2(pixel)));
        }
}
