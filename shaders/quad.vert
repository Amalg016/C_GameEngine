#version 450

// Vertex attributes from the vertex buffer.
layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec3 in_color;

// Outputs to the fragment shader.
layout(location = 0) out vec3 frag_color;
layout(location = 1) out vec2 frag_uv;

void main() {
    gl_Position = vec4(in_pos, 0.0, 1.0);
    frag_color  = in_color;
    frag_uv     = in_uv;
}
