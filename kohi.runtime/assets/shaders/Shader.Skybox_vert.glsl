#version 450

// =========================================================
// Inputs
// =========================================================

const uint KMATERIAL_UBO_MAX_VIEWS = 16;

// Vertex input
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_texcoord;
layout(location = 3) in vec4 in_colour;
layout(location = 4) in vec4 in_tangent;

layout(set = 0, binding = 0) uniform global_ubo_data {
    mat4 views[KMATERIAL_UBO_MAX_VIEWS];
    mat4 projection;
} global_ubo;

layout(push_constant) uniform immediate_data {
    uint view_index;
} immediate;

// =========================================================
// Outputs
// =========================================================

// Data Transfer Object to fragment shader.
layout(location = 0) out dto {
	vec3 tex_coord;
} out_dto;

void main() {
	out_dto.tex_coord = -in_position;
	gl_Position = global_ubo.projection * global_ubo.views[immediate.view_index] * vec4(in_position, 1.0);
} 
