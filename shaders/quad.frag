#version 450

layout(location = 0) in vec4 frag_color;
layout(location = 1) in vec2 frag_uv;
layout(location = 2) flat in uint frag_entity_id;

layout(binding = 0) uniform sampler2D tex_sampler;

layout(location = 0) out vec4 out_color;
layout(location = 1) out uint out_entity_id;

void main() {
    vec4 tex_color = texture(tex_sampler, frag_uv);
    out_color = tex_color * frag_color;
    out_entity_id = frag_entity_id;
}
