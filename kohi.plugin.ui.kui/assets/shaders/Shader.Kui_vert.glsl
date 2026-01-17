#version 450

// =========================================================
// Inputs
// =========================================================

// Vertex inputs
layout(location = 0) in vec2 in_position;
layout(location = 1) in vec2 in_texcoord;

layout(set = 0, binding = 0) uniform sui_global_ubo {
    mat4 projection;
	mat4 view;
} global_ubo;

layout(set = 1, binding = 0) uniform texture2D atlas_texture;
layout(set = 1, binding = 1) uniform sampler atlas_sampler;

layout(push_constant) uniform immediate_data {
	mat4 model; // 64 bytes
    vec4 diffuse_colour;
} immediate;

// =========================================================
// Outputs
// =========================================================

// Data Transfer Object to fragment shader
layout(location = 0) out struct dto {
	vec2 tex_coord;
} out_dto;

void main() {
	// NOTE: intentionally flip y texture coorinate. This, along with flipped ortho matrix, puts [0, 0] in the top-left 
	// instead of bottom-left and adjusts texture coordinates to show in the right direction..
	out_dto.tex_coord = vec2(in_texcoord.x, 1.0 - in_texcoord.y);
	gl_Position = global_ubo.projection * global_ubo.view * immediate.model * vec4(in_position, 0.0, 1.0);
}
