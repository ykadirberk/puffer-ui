#version 450
layout(location=0) in vec2 position;
layout(location=1) in vec2 uv;
layout(location=2) in vec4 color;
layout(location=3) in uint shape_index;
layout(push_constant) uniform Projection { vec2 extent; } projection;
layout(location=0) out vec2 texcoord;
layout(location=1) out vec4 tint;
layout(location=2) flat out uint shape;
void main() {
    gl_Position=vec4(position/projection.extent*2.0-1.0,0.0,1.0);
    texcoord=uv;
    tint=color;
    shape=shape_index;
}
