#define PUI_EXTERNAL_SHAPES
#include "shape_common.glsl"
struct GlyphRequest { Shape shape; uvec4 key; };
layout(std430,set=1,binding=0) readonly buffer Requests { GlyphRequest values[]; } requests;
layout(r32ui,set=1,binding=5) uniform writeonly uimage2D glyph_output;
layout(push_constant) uniform SourceParameters { uvec4 value; } source_parameters;
