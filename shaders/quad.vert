#version 450

// Vertex attributes from the vertex buffer.
layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec3 in_color;

// Push constants — view-projection + per-sprite 2D transform + UV sub-region.
layout(push_constant) uniform PushConstants {
    mat4 view_proj;     // offset 0
    vec2 scale;         // offset 64
    vec2 translate;     // offset 72
    vec4 blend_color;   // offset 80
    vec2 uv_offset;     // offset 96
    vec2 uv_scale;      // offset 104
    uint entity_id;     // offset 112
    float rotation;     // offset 116 (was _pad)
} pc;

// Outputs to the fragment shader.
layout(location = 0) out vec4 frag_color;
layout(location = 1) out vec2 frag_uv;
layout(location = 2) flat out uint frag_entity_id;

void main() {
    // Transform the unit quad to world space with scale first, then rotation, then translation, then apply view-projection.
    float c = cos(pc.rotation);
    float s = sin(pc.rotation);
    vec2 scaled = in_pos * pc.scale;
    vec2 rotated = vec2(scaled.x * c - scaled.y * s,
                        scaled.x * s + scaled.y * c);
    vec2 world_pos = rotated + pc.translate;
    gl_Position = pc.view_proj * vec4(world_pos, 0.0, 1.0);
    frag_color  = vec4(in_color, 1.0) * pc.blend_color;
    frag_uv     = pc.uv_offset + in_uv * pc.uv_scale;
    frag_entity_id = pc.entity_id;
}

