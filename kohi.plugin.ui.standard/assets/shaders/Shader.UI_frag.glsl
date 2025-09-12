#version 450


// =========================================================
// Inputs
// =========================================================

layout(set = 0, binding = 0) uniform sui_global_ubo {
    mat4 projection;
	mat4 view;
} sui_global_ubo;

layout(set = 1, binding = 0) uniform texture2D atlas_texture;
layout(set = 1, binding = 1) uniform sampler atlas_sampler;

layout(push_constant) uniform immediate_data {
	mat4 model; // 64 bytes
    vec4 diffuse_colour;
} immediate;

// Data Transfer Object from vertex shader
layout(location = 0) in struct dto {
	vec2 tex_coord;
} in_dto;

// =========================================================
// Outputs
// =========================================================
layout(location = 0) out vec4 out_colour;

void main() {
    vec4 samp_colour = texture(sampler2D(atlas_texture, atlas_sampler), in_dto.tex_coord);
    out_colour = immediate.diffuse_colour * samp_colour;
}
