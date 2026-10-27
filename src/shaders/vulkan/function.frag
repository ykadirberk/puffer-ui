#version 450
layout(location=0) flat in vec4 source;
layout(location=0) out vec4 output_color;
void main() { output_color=source; }
