#version 450

layout(location = 0) in vec3 frag_color;
layout(location = 1) in vec2 frag_uv;

layout(binding = 0) uniform sampler2D tex_sampler;

layout(location = 0) out vec4 out_color;

void main() {
    vec4 tex_color = texture(tex_sampler, frag_uv);
    out_color = tex_color * vec4(frag_color, 1.0);
}
