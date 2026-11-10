#version 450
layout(location = 0) in vec2 local_position;
layout(location = 1) flat in vec2 size;
layout(location = 2) flat in vec4 corner_radii;
layout(location = 3) flat in vec4 shade_top_left;
layout(location = 4) flat in vec4 shade_top_right;
layout(location = 5) flat in vec4 shade_bottom_right;
layout(location = 6) flat in vec4 shade_bottom_left;
layout(location = 7) flat in vec4 shade_underlay;
layout(location = 8) flat in uint gradient_fill;
layout(location = 0) out vec4 output_color;

void main() {
    vec2 half_size = size * 0.5;
    vec2 p = local_position - half_size;
    float radius = p.y < 0.0
        ? (p.x < 0.0 ? corner_radii.x : corner_radii.y)
        : (p.x < 0.0 ? corner_radii.w : corner_radii.z);
    vec2 q = abs(p) - half_size + radius;
    float distance_to_edge = (q.x > 0.0 && q.y > 0.0 ? length(q) : max(q.x, q.y)) - radius;
    // The ramp stays 1.25 framebuffer pixels wide at every window scale.
    float coverage = clamp(0.5 - distance_to_edge / 1.25, 0.0, 1.0);
    if (coverage == 0.0) {
        output_color = vec4(0.0);
        return;
    }

    vec4 front = shade_top_left;
    float under = 0.0;
    if (gradient_fill != 0u) {
        vec2 uv = clamp(local_position / size, 0.0, 1.0);
        front = mix(mix(shade_top_left, shade_top_right, uv.x),
                    mix(shade_bottom_left, shade_bottom_right, uv.x), uv.y);
        // Compose the two legacy tints, then apply the common edge coverage.
        under = shade_underlay.a * (1.0 - front.a);
    }
    output_color = vec4(front.rgb * front.a + shade_underlay.rgb * under,
                        front.a + under) * coverage;
}
