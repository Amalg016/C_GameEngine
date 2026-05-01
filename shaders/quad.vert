#version 450

// Vertex attributes from the vertex buffer.
layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec3 in_color;

// Push constants — view-projection + per-sprite 2D transform.
layout(push_constant) uniform PushConstants {
    mat4 view_proj;
    vec2 scale;
    vec2 translate;
} pc;

// Outputs to the fragment shader.
layout(location = 0) out vec3 frag_color;
layout(location = 1) out vec2 frag_uv;

void main() {
    // Transform the unit quad to world space, then apply view-projection.
    vec2 world_pos = in_pos * pc.scale + pc.translate;
    gl_Position = pc.view_proj * vec4(world_pos, 0.0, 1.0);
    frag_color  = in_color;
    frag_uv     = in_uv;
}
